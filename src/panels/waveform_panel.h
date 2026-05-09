#ifndef WAVEFORM_PANEL_H
#define WAVEFORM_PANEL_H

#include "../panel_base.h"

struct WaveformPanel : Panel {
    explicit WaveformPanel(SharedState& s) : Panel(s) {}
    const char* Name() const override { return "Waveform"; }
    void Render() override;
    void RenderOfflineViewers();
};

#endif // WAVEFORM_PANEL_H
