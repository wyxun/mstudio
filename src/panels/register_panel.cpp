#include "register_panel.h"
#include "imgui.h"
#include <algorithm>
#include <exception>

RegisterPanel::RegisterPanel(SharedState& s) : Panel(s) {
    last_refresh_ = std::chrono::steady_clock::now();
}

void RegisterPanel::DrawRegGroup(const char* title,
                                  const std::vector<RegEntry>& regs,
                                  const std::map<std::string, uint32_t>& prev) {
    ImGui::TextUnformatted(title);
    ImGui::Separator();

    if (!ImGui::BeginTable(title, 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Decimal");
    ImGui::TableHeadersRow();

    for (auto& r : regs) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(r.name.c_str());

        ImGui::TableSetColumnIndex(1);

        auto it = prev.find(r.name);
        bool changed = (it != prev.end() && it->second != r.value);
        if (changed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        }

        ImGui::Text("0x%08X", r.value);

        if (changed) {
            ImGui::PopStyleColor();
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", r.value);
    }
    ImGui::EndTable();
}

void RegisterPanel::RefreshRegs() {
    last_refresh_ = std::chrono::steady_clock::now();

    for (auto& r : regs_) {
        prev_vals_[r.name] = r.value;
    }

    regs_ = ocd_.GetRegs();
}

void RegisterPanel::Render() {
    ImGui::Begin(Name());

    bool show_groups = false;
    try {
    // Connection bar
    if (!ocd_.IsConnected()) {
        if (ImGui::Button("Connect to OpenOCD")) {
            connected_ = ocd_.Connect();
            if (connected_) {
                RefreshRegs();
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Disconnected");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Connected");

        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            ocd_.Disconnect();
            connected_ = false;
            regs_.clear();
        }

        ImGui::SameLine();
        if (ImGui::Button("Halt & Read")) {
            ocd_.Halt();
            RefreshRegs();
        }

        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            ocd_.Resume();
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            RefreshRegs();
        }

        // Auto-refresh every 1s when connected
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_refresh_).count();
        if (elapsed > 1000) {
            RefreshRegs();
        }
    }

    ImGui::Separator();

    if (regs_.empty()) {
        ImGui::TextUnformatted("No register data. Connect to OpenOCD and refresh.");
    } else {
        show_groups = true;
    }

    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown error in RegisterPanel");
    }

    if (show_groups) {
        // Split registers into groups
        std::vector<RegEntry> core_regs;
        std::vector<RegEntry> special_regs;
        std::vector<RegEntry> fpu_regs;

        for (auto& r : regs_) {
            const std::string& n = r.name;
            bool is_special = false, is_fpu = false;

            if (n == "msp" || n == "psp" || n == "primask" || n == "basepri" ||
                n == "faultmask" || n == "control") is_special = true;
            else if (n[0] == 'd') is_fpu = true;
            else if (n[0] == 'f' && n[1] == 'p') is_fpu = true;

            if (is_fpu) fpu_regs.push_back(r);
            else if (is_special) special_regs.push_back(r);
            else core_regs.push_back(r);
        }

        // Sort core regs: r0-r12, sp, lr, pc, xpsr
        std::sort(core_regs.begin(), core_regs.end(),
                  [](const RegEntry& a, const RegEntry& b) {
                      auto order = [](const std::string& n) -> int {
                          if (n[0] == 'r') {
                              if (n.length() == 2) return n[1] - '0';
                              return 10 + (n[2] - '0');
                          }
                          if (n == "sp") return 20;
                          if (n == "lr") return 21;
                          if (n == "pc") return 22;
                          if (n[0] == 'x') return 23;
                          return 99;
                      };
                      return order(a.name) < order(b.name);
                  });

        if (!core_regs.empty()) DrawRegGroup("Core Registers", core_regs, prev_vals_);
        ImGui::Spacing();
        if (!special_regs.empty()) DrawRegGroup("Special Registers", special_regs, prev_vals_);
        ImGui::Spacing();
        if (!fpu_regs.empty()) DrawRegGroup("FPU Registers", fpu_regs, prev_vals_);
    }

    ImGui::End();
}
