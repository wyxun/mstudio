#include "variable_panel.h"
#include "imgui.h"
#include <algorithm>
#include <exception>

VariablePanel::VariablePanel(SharedState& s) : Panel(s) {
    last_refresh_ = std::chrono::steady_clock::now();
}

static const char* TypeName(uint32_t size) {
    switch (size) {
        case 1: return "u8";
        case 2: return "u16";
        case 4: return "u32";
        case 8: return "u64";
        default: return "";
    }
}

void VariablePanel::LoadElf() {
    vars_.clear();
    values_.clear();
    if (elf_.Load(elf_path_)) {
        vars_ = elf_.GetVariables();
        // Sort by address
        std::sort(vars_.begin(), vars_.end(),
                  [](const ElfSymbol* a, const ElfSymbol* b) {
                      return a->address < b->address;
                  });
    }
}

uint32_t VariablePanel::ReadVarValue(uint32_t addr, uint32_t size) {
    uint32_t val = ocd_.ReadMem32(addr);
    if (size < 4) {
        uint32_t mask = (size == 1) ? 0xFFu : 0xFFFFu;
        val &= mask;
    }
    values_[addr] = val;
    return val;
}

void VariablePanel::RefreshValues() {
    last_refresh_ = std::chrono::steady_clock::now();
    for (auto* v : vars_) {
        if (favorites_.count(v->name)) {
            ReadVarValue(v->address, v->size);
        }
    }
}

void VariablePanel::ToggleFavorite(const std::string& name) {
    auto it = favorites_.find(name);
    if (it != favorites_.end()) {
        favorites_.erase(it);
    } else {
        favorites_.insert(name);
    }
}

void VariablePanel::Render() {
    ImGui::Begin(Name());

    bool show_table = false;
    try {
    // ── ELF Load Bar ──
    ImGui::PushItemWidth(200);
    ImGui::InputText("ELF", elf_path_, sizeof(elf_path_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        LoadElf();
    }
    ImGui::SameLine();
    if (elf_.IsLoaded()) {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Loaded %zu vars", vars_.size());
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "No ELF");
    }

    // ── OpenOCD Bar ──
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    if (!ocd_connected_) {
        if (ImGui::Button("Connect OCD")) {
            ocd_connected_ = ocd_.Connect();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "OCD off");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "OCD on");
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            ocd_.Disconnect();
            ocd_connected_ = false;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-##var", &auto_refresh_);
        ImGui::SameLine();
        if (ImGui::Button("Read All")) {
            RefreshValues();
        }
    }

    ImGui::Separator();

    if (!elf_.IsLoaded() || vars_.empty()) {
        ImGui::TextUnformatted("Load an ELF file to view variables.");
    } else {
        show_table = true;
    }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown error in VariablePanel");
    }

    if (show_table) {
    // ── Search ──
    ImGui::PushItemWidth(200);
    ImGui::InputText("Search", search_buf_, sizeof(search_buf_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("%zu vars, %zu favs", vars_.size(), favorites_.size());

    // ── Auto-refresh ──
    if (ocd_connected_ && auto_refresh_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_refresh_).count();
        if (elapsed > 1000) {
            RefreshValues();
        }
    }

    // ── Variable Table ──
    ImGui::Separator();
    if (ImGui::BeginTable("Vars", 7,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Value (Hex)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Value (Dec)");
        ImGui::TableHeadersRow();

        std::string search = search_buf_;
        for (auto* v : vars_) {
            if (!search.empty()) {
                std::string lower = v->name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                std::string slow = search;
                std::transform(slow.begin(), slow.end(), slow.begin(), ::tolower);
                if (lower.find(slow) == std::string::npos) continue;
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool is_fav = favorites_.count(v->name) > 0;
            if (is_fav) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.9f, 0.3f, 1));
            }
            ImGui::PushID(v->name.c_str());
            if (ImGui::SmallButton(is_fav ? "*" : " ")) {
                ToggleFavorite(v->name);
            }
            ImGui::PopID();
            if (is_fav) ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(v->name.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%08X", v->address);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(TypeName(v->size));

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%u", v->size);

            ImGui::TableSetColumnIndex(5);
            auto vit = values_.find(v->address);
            if (vit != values_.end()) {
                ImGui::Text("0x%08X", vit->second);
            } else if (ocd_connected_) {
                ImGui::PushID((int)v->address);
                if (ImGui::SmallButton("Read")) {
                    ReadVarValue(v->address, v->size);
                }
                ImGui::PopID();
            } else {
                ImGui::TextUnformatted("-");
            }

            ImGui::TableSetColumnIndex(6);
            if (vit != values_.end()) {
                uint32_t val = vit->second;
                if (v->size <= 4) {
                    ImGui::Text("%d (0x%X)", (int32_t)(val & ((1u << (v->size * 8)) - 1)), val);
                } else {
                    ImGui::Text("%u", val);
                }
            }
        }
        ImGui::EndTable();
    }
    }

    ImGui::End();
}
