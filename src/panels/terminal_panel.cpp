#include "terminal_panel.h"
#include "../shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include <cstring>
void TerminalPanel::Render() {
    auto& state = state_;
    ImGui::Begin("Shell Terminal", nullptr, ImGuiWindowFlags_NoScrollbar);

    ImGui::Checkbox("Auto-scroll", &state.auto_scroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        state.term_log_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(state.term_log_.c_str());
    }

    ImGui::Separator();

    state.term_filter_.Draw("Filter (\"incl,-excl\")", 180);
    ImGui::Separator();

    const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (state.term_filter_.IsActive()) {
        const char* line_start = state.term_log_.c_str();
        const char* log_end = line_start + state.term_log_.length();
        while (line_start < log_end) {
            const char* line_end = strchr(line_start, '\n');
            if (!line_end) line_end = log_end;
            if (state.term_filter_.PassFilter(line_start, line_end)) {
                ImGui::TextUnformatted(line_start, line_end);
            }
            line_start = line_end + 1;
        }
    } else {
        ImGui::TextUnformatted(state.term_log_.c_str());
    }

    if (ImGui::BeginPopupContextWindow()) {
        if (ImGui::MenuItem("Copy All")) {
            ImGui::SetClipboardText(state.term_log_.c_str());
        }
        ImGui::EndPopup();
    }

    if (state.auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::Separator();

    bool reclaim_focus = false;
    ImGui::PushItemWidth(-ImGui::GetStyle().ItemSpacing.x * 7);
    if (ImGui::InputText("##Input", state.term_input_buf_, IM_ARRAYSIZE(state.term_input_buf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (state.term_input_buf_[0]) {
            std::string cmd = std::string(state.term_input_buf_) + "\r\n";
            NetworkMgr::GetInstance().SendToCh0(cmd);
            std::memset(state.term_input_buf_, 0, sizeof(state.term_input_buf_));
        }
        reclaim_focus = true;
    }
    ImGui::PopItemWidth();

    ImGui::SetItemDefaultFocus();
    if (reclaim_focus)
        ImGui::SetKeyboardFocusHere(-1);

    ImGui::End();
}
