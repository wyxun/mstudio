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
    ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Hex",      ImGuiTableColumnFlags_WidthFixed, 110.0f);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// 架构检测：根据寄存器名特征推断 ISA
// OpenOCD 对 RISC-V 目标会返回 "x0"..."x31" 或 ABI 别名 "zero","ra","sp"...
// Cortex-M 目标会返回 "r0"..."r12","sp","lr","pc","xpsr" 等
// ─────────────────────────────────────────────────────────────────────────────
static bool DetectRISCV(const std::vector<RegEntry>& regs) {
    for (const auto& r : regs) {
        const std::string& n = r.name;
        // ABI 别名：RISC-V 专有
        if (n == "zero" || n == "ra" || n == "gp" || n == "tp") return true;
        // x0..x31 格式
        if (n.size() >= 2 && n[0] == 'x' && n[1] >= '0' && n[1] <= '9') return true;
        // M 模式 CSR
        if (n == "mstatus" || n == "mepc" || n == "mcause" ||
            n == "mtvec"   || n == "mtval" || n == "mie" || n == "mip")
            return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// RISC-V 寄存器分组与排序
// GPR: x0(zero)..x31(t6) + pc
// CSR: mstatus/mepc/mcause/mtvec 等 M 模式寄存器
// Other: 其余
// ─────────────────────────────────────────────────────────────────────────────
static void GroupAndSortRISCV(const std::vector<RegEntry>& regs,
                               std::vector<RegEntry>& gpr,
                               std::vector<RegEntry>& csr,
                               std::vector<RegEntry>& other) {
    // ABI 名到 x 编号映射，顺序即为 x0..x31
    static const char* kABINames[32] = {
        "zero","ra","sp","gp","tp",
        "t0","t1","t2",
        "s0","s1",
        "a0","a1","a2","a3","a4","a5","a6","a7",
        "s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
        "t3","t4","t5","t6"
    };
    auto abiIndex = [](const std::string& n) -> int {
        for (int i = 0; i < 32; i++) {
            if (n == kABINames[i]) return i;
        }
        // x0..x31 直接提取编号
        if (n.size() >= 2 && n[0] == 'x') {
            try { return std::stoi(n.substr(1)); } catch (...) {}
        }
        return 100;
    };

    for (const auto& r : regs) {
        const std::string& n = r.name;
        int idx = abiIndex(n);
        if (idx < 32 || n == "pc") {
            gpr.push_back(r);
        } else if (n.size() >= 2 && (n[0] == 'm' || n[0] == 's') && n != "sp") {
            csr.push_back(r);
        } else {
            other.push_back(r);
        }
    }

    // GPR 排序：zero(x0) → ... → t6(x31) → pc
    std::sort(gpr.begin(), gpr.end(), [&](const RegEntry& a, const RegEntry& b) {
        int ia = (a.name == "pc") ? 32 : abiIndex(a.name);
        int ib = (b.name == "pc") ? 32 : abiIndex(b.name);
        return ia < ib;
    });

    // CSR 字母排序
    std::sort(csr.begin(), csr.end(), [](const RegEntry& a, const RegEntry& b) {
        return a.name < b.name;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Cortex-M 寄存器分组与排序
// Core: r0-r12, sp, lr, pc, xpsr
// Special: msp/psp/primask/basepri/faultmask/control
// FPU: d0-d15, fpscr 等
// ─────────────────────────────────────────────────────────────────────────────
static void GroupAndSortCortexM(const std::vector<RegEntry>& regs,
                                 std::vector<RegEntry>& core,
                                 std::vector<RegEntry>& special,
                                 std::vector<RegEntry>& fpu) {
    for (const auto& r : regs) {
        const std::string& n = r.name;
        bool is_special = false, is_fpu = false;

        if (n == "msp" || n == "psp" || n == "primask" || n == "basepri" ||
            n == "faultmask" || n == "control") {
            is_special = true;
        } else if (!n.empty() && n[0] == 'd') {
            is_fpu = true;
        } else if (n.size() >= 2 && n[0] == 'f' && n[1] == 'p') {
            is_fpu = true;
        }

        if (is_fpu)          fpu.push_back(r);
        else if (is_special) special.push_back(r);
        else                 core.push_back(r);
    }

    // 核心寄存器排序: r0-r12(0-12), sp(20), lr(21), pc(22), xpsr(23)
    std::sort(core.begin(), core.end(), [](const RegEntry& a, const RegEntry& b) {
        auto order = [](const std::string& n) -> int {
            if (n.size() >= 2 && n[0] == 'r' && n[1] >= '0' && n[1] <= '9') {
                if (n.length() == 2) return n[1] - '0';
                return 10 + (n[2] - '0');
            }
            if (n == "sp")   return 20;
            if (n == "lr")   return 21;
            if (n == "pc")   return 22;
            if (n[0] == 'x') return 23; // xpsr
            return 99;
        };
        return order(a.name) < order(b.name);
    });
}

void RegisterPanel::Render() {
    ImGui::Begin(Name());

    bool show_groups = false;
    try {
    // Connection bar
    if (!ocd_.IsConnected() && !ocd_.IsConnecting()) {
        if (ImGui::Button("Connect to OpenOCD")) {
            ocd_.ConnectAsync();
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Disconnected");
    } else if (ocd_.IsConnecting()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Connecting...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ocd_.DisconnectAsync();
        }
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Connected");

        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            ocd_.DisconnectAsync();
            connected_ = false;
            regs_.clear();
        }

        ImGui::SameLine();
        if (ImGui::Button("Halt & Read")) {
            ocd_.HaltAsync();
            ocd_.TriggerRefreshRegs();
        }

        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            ocd_.ResumeAsync();
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            ocd_.TriggerRefreshRegs();
        }

        // Auto-refresh every 1s when connected
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_refresh_).count();
        if (elapsed > 1000) {
            last_refresh_ = now;
            ocd_.TriggerRefreshRegs();
        }
    }

    ImGui::Separator();

    // Fetch latest regs
    std::vector<RegEntry> new_regs;
    if (ocd_.FetchNewRegs(new_regs)) {
        for (auto& r : regs_) {
            prev_vals_[r.name] = r.value;
        }
        regs_ = std::move(new_regs);
    }

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
        if (DetectRISCV(regs_)) {
            // ── RISC-V 寄存器视图 ──────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.7f, 1.0f), "[RISC-V]");
            ImGui::Spacing();

            std::vector<RegEntry> gpr, csr, other;
            GroupAndSortRISCV(regs_, gpr, csr, other);

            if (!gpr.empty())   DrawRegGroup("General Purpose Registers (GPR)", gpr,   prev_vals_);
            ImGui::Spacing();
            if (!csr.empty())   DrawRegGroup("Control & Status Registers (CSR)", csr,   prev_vals_);
            ImGui::Spacing();
            if (!other.empty()) DrawRegGroup("Other Registers",                  other, prev_vals_);
        } else {
            // ── Cortex-M 寄存器视图 ────────────────────────────────────────
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[Cortex-M]");
            ImGui::Spacing();

            std::vector<RegEntry> core, special, fpu;
            GroupAndSortCortexM(regs_, core, special, fpu);

            if (!core.empty())    DrawRegGroup("Core Registers",    core,    prev_vals_);
            ImGui::Spacing();
            if (!special.empty()) DrawRegGroup("Special Registers", special, prev_vals_);
            ImGui::Spacing();
            if (!fpu.empty())     DrawRegGroup("FPU Registers",     fpu,     prev_vals_);
        }
    }

    ImGui::End();
}
