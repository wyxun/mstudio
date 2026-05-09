#ifndef GUI_LAYER_H
#define GUI_LAYER_H

#include "shared_state.h"
#include "panel_base.h"
#include <vector>
#include <memory>

class GuiLayer {
public:
    GuiLayer();
    void Render();

private:
    void SetupTheme();

    SharedState state_;
    std::vector<std::unique_ptr<Panel>> panels_;
};

#endif // GUI_LAYER_H
