#ifndef REGISTER_PANEL_H
#define REGISTER_PANEL_H

#include "panel_base.h"
#include "utils/ocd_client.h"
#include <vector>
#include <map>
#include <chrono>

class RegisterPanel : public Panel {
public:
    explicit RegisterPanel(SharedState& s);
    const char* Name() const override { return "Registers"; }
    void Render() override;

private:
    void DrawRegGroup(const char* title, const std::vector<RegEntry>& regs,
                      const std::map<std::string, uint32_t>& prev);
    void RefreshRegs();

    OcdClient ocd_;
    bool connected_ = false;
    std::vector<RegEntry> regs_;
    std::map<std::string, uint32_t> prev_vals_;
    std::chrono::steady_clock::time_point last_refresh_;
};

#endif // REGISTER_PANEL_H
