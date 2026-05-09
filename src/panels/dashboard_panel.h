#ifndef DASHBOARD_PANEL_H
#define DASHBOARD_PANEL_H

#include "../panel_base.h"

struct DashboardPanel : Panel {
    explicit DashboardPanel(SharedState& s) : Panel(s) {}
    const char* Name() const override { return "Dashboard"; }
    void Render() override;
};

#endif // DASHBOARD_PANEL_H
