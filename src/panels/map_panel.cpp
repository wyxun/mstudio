#include "map_panel.h"
#include "imgui.h"
#include <algorithm>
#include <exception>
#include <map>

MapPanel::MapPanel(SharedState& s) : Panel(s) {}

void MapPanel::LoadMap() {
    parser_ = MapParser();
    selected_section_ = -1;
    parser_.Load(map_path_);
}

void MapPanel::Render() {
    ImGui::Begin(Name());

    ImGui::PushItemWidth(220);
    ImGui::InputText("##mappath", map_path_, sizeof(map_path_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Load")) LoadMap();

    if (!parser_.IsLoaded()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("No map loaded");
        ImGui::End();
        return;
    }

    const auto& sections = parser_.GetSections();
    uint32_t flash_used = parser_.GetTotalFlash();
    uint32_t ram_used = parser_.GetTotalRam();

    ImGui::SameLine();
    ImGui::Text("| %zu sections", sections.size());

    // Flash/RAM progress bars
    float bar_w = ImGui::GetContentRegionAvail().x - 60;
    uint32_t total_usage = flash_used + ram_used;
    if (total_usage == 0) total_usage = 1;
    auto DrawBar = [&](const char* label, uint32_t used, ImU32 color) {
        ImGui::Text("%s", label);
        ImGui::SameLine(60);
        float frac = (float)used / total_usage;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(bar_w * frac, ImGui::GetFrameHeight());
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color);
        ImGui::SameLine();
        if (used >= 1024)
            ImGui::Text("%u.%02u KB", used / 1024, (used % 1024) * 100 / 1024);
        else
            ImGui::Text("%u B", used);
    };
    DrawBar("Flash:", flash_used, IM_COL32(0x47, 0x8F, 0xFF, 0xFF));
    DrawBar("RAM:",   ram_used,   IM_COL32(0x47, 0xCC, 0x8F, 0xFF));

    ImGui::Separator();

    // Side-by-side: Sections | Files
    float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w < 100.0f) avail_w = 100.0f;
    float left_w  = avail_w * 0.48f;
    float right_w = avail_w - left_w - ImGui::GetStyle().ItemSpacing.x;

    // Left: Sections table
    if (ImGui::BeginChild("SecPane", ImVec2(left_w, 200), true)) {
        if (ImGui::BeginTable("SecTbl", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Section");
            ImGui::TableSetupColumn("VMA");
            ImGui::TableSetupColumn("Size");
            ImGui::TableSetupColumn("Type");
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)sections.size(); i++) {
                auto& sec = sections[i];
                ImGui::TableNextRow();
                bool sel = (i == selected_section_);
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(sec.name.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns))
                    selected_section_ = i;
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%08X", sec.vma);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", sec.size);
                ImGui::TableSetColumnIndex(3);
                bool is_flash = (sec.name == ".text" || sec.name == ".rodata" ||
                                 sec.name == ".isr_vector" || sec.name.find(".ARM") == 0 ||
                                 sec.name.find(".init") == 0 || sec.name.find(".fini") == 0 ||
                                 sec.name == ".data");
                bool is_ram = (sec.name == ".bss" || sec.name == ".noinit" ||
                               sec.name == "._user_heap_stack" || sec.name == ".data");
                if (is_flash && is_ram) ImGui::TextUnformatted("Flash+RAM");
                else if (is_flash) ImGui::TextUnformatted("Flash");
                else if (is_ram) ImGui::TextUnformatted("RAM");
                else ImGui::TextUnformatted("-");
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    ImGui::SameLine();

    // Right: Files in selected section, or Top Files
    if (ImGui::BeginChild("FilePane", ImVec2(right_w, 200), true)) {
        if (selected_section_ >= 0 && selected_section_ < (int)sections.size()) {
            auto& sec = sections[selected_section_];
            auto sorted = sec.files;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            if (ImGui::BeginTable("FileTbl", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("File");
                ImGui::TableSetupColumn("Size");
                ImGui::TableHeadersRow();
                int shown = 0;
                for (auto& f : sorted) {
                    if (++shown > 10) break;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(f.first.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", f.second);
                }
                ImGui::EndTable();
            }
        } else {
            // Top files across all sections
            std::map<std::string, uint32_t> totals;
            for (auto& sec : sections)
                for (auto& f : sec.files)
                    totals[f.first] += f.second;
            std::vector<std::pair<std::string, uint32_t>> sorted(totals.begin(), totals.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            if (ImGui::BeginTable("TopTbl", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("File");
                ImGui::TableSetupColumn("Size");
                ImGui::TableHeadersRow();
                int shown = 0;
                for (auto& f : sorted) {
                    if (++shown > 10) break;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(f.first.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", f.second);
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    // Symbol Search
    ImGui::Separator();
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    ImGui::InputText("##symsrch", search_buf_, sizeof(search_buf_));
    ImGui::PopItemWidth();

    {
        std::string search = search_buf_;
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);
        // build name->(addr,size) map
        std::map<std::string, std::pair<uint32_t, uint32_t>> sym_info;
        for (auto& s : parser_.GetSymbols()) sym_info[s.name] = {s.address, s.size};

        if (ImGui::BeginTable("XRefTbl", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                        ImVec2(-1, 120))) {
            ImGui::TableSetupColumn("Symbol");
            ImGui::TableSetupColumn("Size");
            ImGui::TableSetupColumn("Section");
            ImGui::TableSetupColumn("Referenced By");
            ImGui::TableHeadersRow();

            for (auto& xr : parser_.GetCrossRefs()) {
                std::string lower = xr.symbol;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(search) == std::string::npos) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(xr.symbol.c_str());
                ImGui::TableSetColumnIndex(1);
                auto it = sym_info.find(xr.symbol);
                if (it != sym_info.end()) {
                    ImGui::Text("%u", it->second.second);
                    ImGui::TableSetColumnIndex(2);
                    auto* sec = parser_.FindSectionByAddr(it->second.first);
                    ImGui::TextUnformatted(sec ? sec->name.c_str() : "-");
                } else {
                    ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableSetColumnIndex(3);
                std::string files;
                for (size_t i = 0; i < xr.files.size(); i++) {
                    if (i) files += ", ";
                    files += xr.files[i];
                }
                ImGui::TextUnformatted(files.c_str());
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}
