#ifndef VARIABLE_PANEL_H
#define VARIABLE_PANEL_H

#include "panel_base.h"
#include "utils/elf_parser.h"
#include "utils/ocd_client.h"
#include <vector>
#include <map>
#include <set>
#include <chrono>

class VariablePanel : public Panel {
public:
    explicit VariablePanel(SharedState& s);
    const char* Name() const override { return "Variables"; }
    void Render() override;

private:
    void LoadElf();
    uint32_t ReadVarValue(uint32_t addr, uint32_t size);
    void RefreshValues();
    void ToggleFavorite(const std::string& name);

    ElfParser elf_;
    OcdClient ocd_;
    bool ocd_connected_ = false;

    // All variables from ELF
    std::vector<const ElfSymbol*> vars_;

    // Favorites
    std::set<std::string> favorites_;

    // Cached values (addr -> value)
    std::map<uint32_t, uint32_t> values_;

    // ELF path
    char elf_path_[256] = "build/template.elf";

    // Search filter
    char search_buf_[64] = {0};

    // Auto-refresh
    bool auto_refresh_ = true;
    std::chrono::steady_clock::time_point last_refresh_;
};

#endif // VARIABLE_PANEL_H
