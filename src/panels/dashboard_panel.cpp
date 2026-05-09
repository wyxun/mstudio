#include "dashboard_panel.h"
#include "../shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

void DashboardPanel::Render() {
    auto& state = state_;
    ImGui::Begin("Dashboard");
    auto& net = NetworkMgr::GetInstance();

    ImGui::Text("Connections:");
    ImGui::SameLine();
    ImGui::TextColored(net.IsCh0Connected() ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), "[Ch0 Shell: %s]", net.IsCh0Connected() ? "ON" : "OFF");
    ImGui::SameLine();
    ImGui::TextColored(net.IsCh1Connected() ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), "[Ch1 Wave: %s]", net.IsCh1Connected() ? "ON" : "OFF");
    ImGui::SameLine();
    ImGui::Text("| Sample Rate: %.1f Hz", 1.0 / state.smoothed_period_);

    ImGui::Separator();

    // Wave Control
    if (ImGui::Button(state.paused_ ? "Resume" : "Pause")) state.paused_ = !state.paused_;
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("Clear")) {
        for (auto& pair : state.ch_buffers_) {
            pair.second.XData.clear();
            pair.second.YData.clear();
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("Window", &state.history_window_, 0.1f, 60.0f, "%.1fs");

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120.0f);

    if (state.is_recording_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Stop CSV")) {
            state.is_recording_ = false;
            if (state.record_file_.is_open()) state.record_file_.close();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.2f, 1.0f));
        if (ImGui::Button("Start CSV")) {
            std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            std::stringstream ss;
            ss << "rec_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".csv";
            state.recording_filename_ = ss.str();

            state.record_file_.open(state.recording_filename_);
            if (state.record_file_.is_open()) {
                state.is_recording_ = true;
                state.record_file_ << "Time";
                const auto& channels = state.parser_.GetChannels();
                for (const auto& ch : channels) state.record_file_ << "," << ch.name;
                state.record_file_ << "\n";
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Offline Analysis
    ImGui::Text("Data Analysis:");
    ImGui::SameLine();
    if (ImGui::Button("Open CSV Viewer")) {
        std::string path = state.OpenFileDialog();
        if (!path.empty()) {
            state.offline_sessions_.erase(
                std::remove_if(state.offline_sessions_.begin(), state.offline_sessions_.end(),
                [](const OfflineSession& s) { return !s.open; }),
                state.offline_sessions_.end());

            OfflineSession session;
            session.id = state.next_session_id_++;
            session.open = true;
            strncpy(session.filepath, path.c_str(), sizeof(session.filepath)-1);
            state.LoadCSV(session);
            state.offline_sessions_.push_back(session);
        }
    }

    ImGui::Separator();

    // Macro Manager UI
    ImGui::Text("Quick User Macros (Ch0):");
    ImGui::SameLine();
    if (ImGui::Button("Load Macros...")) {
        std::string path = state.OpenIniFileDialog();
        if (!path.empty()) {
            if (state.macro_mgr_.LoadFromFile(path)) {
                state.macro_mgr_.PersistCurrentPath();
            }
        }
    }
    ImGui::SameLine();
    const std::string& cur_path = state.macro_mgr_.GetCurrentPath();
    if (cur_path.empty()) {
        ImGui::TextDisabled("(No macros loaded)");
    } else {
        size_t slash = cur_path.find_last_of("/\\");
        std::string fname = (slash != std::string::npos) ? cur_path.substr(slash + 1) : cur_path;
        ImGui::TextDisabled("%s", fname.c_str());
    }

    const auto& macros = state.macro_mgr_.GetMacros();
    if (!macros.empty()) {
        ImGui::Separator();
        for (size_t i = 0; i < macros.size(); ++i) {
            char btn_id[128];
            snprintf(btn_id, sizeof(btn_id), "%s##macro%zu", macros[i].label.c_str(), i);
            if (ImGui::Button(btn_id)) {
                net.SendToCh0(macros[i].command + "\n");
            }
            if (i + 1 < macros.size()) ImGui::SameLine();
        }
    }

    ImGui::End();
}
