#include "gui_layer.h"
#include "panels/dashboard_panel.h"
#include "panels/waveform_panel.h"
#include "panels/terminal_panel.h"
#include "panels/register_panel.h"
#include "panels/variable_panel.h"
#include "panels/map_panel.h"
#include "imgui.h"

GuiLayer::GuiLayer() {
    state_.macro_mgr_.RestoreLastSession();

    panels_.push_back(std::make_unique<DashboardPanel>(state_));
    panels_.push_back(std::make_unique<WaveformPanel>(state_));
    panels_.push_back(std::make_unique<TerminalPanel>(state_));
    panels_.push_back(std::make_unique<RegisterPanel>(state_));
    panels_.push_back(std::make_unique<VariablePanel>(state_));
    panels_.push_back(std::make_unique<MapPanel>(state_));
}

void GuiLayer::SetupTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.07f, 0.09f, 0.94f);
    colors[ImGuiCol_Header]                 = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.15f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.18f, 0.30f, 0.45f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.10f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.15f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.35f, 0.50f, 1.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.05f, 0.07f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
}

void GuiLayer::Render() {
    state_.FetchNetworkData();

    // Setup Main DockSpace
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    SetupTheme();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("MainDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Render all docked panels
    for (auto& panel : panels_) {
        panel->Render();
    }

    ImGui::End(); // MainDockSpace

    // Render offline viewer windows (separate, non-docked windows)
    // Find WaveformPanel to call its offline viewer renderer
    for (auto& panel : panels_) {
        if (auto* wp = dynamic_cast<WaveformPanel*>(panel.get())) {
            wp->RenderOfflineViewers();
            break;
        }
    }
}
