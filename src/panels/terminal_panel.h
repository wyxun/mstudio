#ifndef TERMINAL_PANEL_H
#define TERMINAL_PANEL_H

#include "../panel_base.h"

struct TerminalPanel : Panel {
    explicit TerminalPanel(SharedState& s) : Panel(s) {}
    const char* Name() const override { return "Shell Terminal"; }
    void Render() override;
};

#endif // TERMINAL_PANEL_H
