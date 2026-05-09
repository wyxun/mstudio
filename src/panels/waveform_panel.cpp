#include "waveform_panel.h"
#include "../shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include "implot.h"
#include <cstdio>

void WaveformPanel::Render() {
    auto& state = state_;
    ImGui::Begin("Waveform");

    if (ImPlot::BeginPlot("##WaveformPlot", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time(s)", "Value", 0, 0);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0.02, 60.0);
        if (!state.paused_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, state.time_last_ - state.history_window_, state.time_last_, ImGuiCond_Always);
        }

        if (ImPlot::IsPlotHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
            state.history_window_ *= (1.0f - ImGui::GetIO().MouseWheel * 0.1f);
            if (state.history_window_ < 0.02f) state.history_window_ = 0.02f;
            if (state.history_window_ > 60.0f) state.history_window_ = 60.0f;
        }

        const auto& channels = state.parser_.GetChannels();
        for (auto& pair : state.ch_buffers_) {
            int ch_idx = pair.first;
            std::string name = (ch_idx < (int)channels.size()) ? channels[ch_idx].name : "CH" + std::to_string(ch_idx);
            if (state.ch_buffers_[ch_idx].XData.size() > 0) {
                ImPlot::PlotLineG(name.c_str(), [](int idx, void* data) {
                    ScrollingBuffer* buf = (ScrollingBuffer*)data;
                    int real_idx = (buf->Offset + idx) % (int)buf->XData.size();
                    return ImPlotPoint(buf->XData[real_idx], buf->YData[real_idx]);
                }, &state.ch_buffers_[ch_idx], (int)state.ch_buffers_[ch_idx].XData.size());
            }
        }

        // Measurement logic
        if (ImPlot::IsPlotHovered()) {
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            ImVec2 mouse_pixel = ImPlot::PlotToPixels(mouse);
            ImDrawList* draw_list = ImPlot::GetPlotDrawList();
            ImVec2 plot_min = ImPlot::GetPlotPos();
            ImVec2 plot_max = ImVec2(plot_min.x + ImPlot::GetPlotSize().x, plot_min.y + ImPlot::GetPlotSize().y);

            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                state.measure_active_ = !state.measure_active_;
                state.measure_x_ = mouse.x;
                state.measure_y_ = mouse.y;
            }

            auto draw_dashed_h = [&](float x1, float x2, float y, ImU32 col) {
                for (float x = x1; x < x2; x += 10.0f) draw_list->AddLine(ImVec2(x, y), ImVec2((x + 5.0f < x2 ? x + 5.0f : x2), y), col, 1.0f);
            };
            auto draw_dashed_v = [&](float x, float y1, float y2, ImU32 col) {
                for (float y = y1; y < y2; y += 10.0f) draw_list->AddLine(ImVec2(x, y), ImVec2(x, (y + 5.0f < y2 ? y + 5.0f : y2)), col, 1.0f);
            };

            draw_dashed_v(mouse_pixel.x, plot_min.y, plot_max.y, IM_COL32(255, 255, 0, 100));
            draw_dashed_h(plot_min.x, plot_max.x, mouse_pixel.y, IM_COL32(255, 255, 0, 100));

            if (state.measure_active_) {
                ImVec2 ref_pixel = ImPlot::PlotToPixels(state.measure_x_, state.measure_y_);
                draw_dashed_v(ref_pixel.x, plot_min.y, plot_max.y, IM_COL32(255, 0, 0, 150));
                draw_dashed_h(plot_min.x, plot_max.x, ref_pixel.y, IM_COL32(255, 0, 0, 150));
                draw_list->AddRectFilled(ref_pixel, mouse_pixel, IM_COL32(255, 0, 0, 30));
            }

            ImGui::BeginTooltip();
            ImGui::Text("Current X: %.4f s", mouse.x);
            ImGui::Text("Current Y: %.2f", mouse.y);
            if (state.measure_active_) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Ref X: %.4f s", state.measure_x_);
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Ref Y: %.2f", state.measure_y_);
                double dx = mouse.x - state.measure_x_;
                double abs_dx = (dx < 0 ? -dx : dx);
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "dX: %.4f s (%.2f Hz)", dx, (abs_dx > 1e-6 ? 1.0 / abs_dx : 0));
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "dY: %.2f", mouse.y - state.measure_y_);
            }
            ImGui::EndTooltip();
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

void WaveformPanel::RenderOfflineViewers() {
    auto& state = state_;

    for (auto it = state.offline_sessions_.begin(); it != state.offline_sessions_.end(); ) {
        if (!it->open) {
            it = state.offline_sessions_.erase(it);
            continue;
        }

        auto& session = *it;
        char title[256];
        sprintf(title, "Offline Viewer [%d] - %s###Offline%d", session.id, session.filepath, session.id);
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        float hue = (float)(session.id % 5) * 0.1f;
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, (ImVec4)ImColor::HSV(0.6f + hue, 0.6f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, (ImVec4)ImColor::HSV(0.6f + hue, 0.4f, 0.2f));

        if (ImGui::Begin(title, &session.open)) {
            ImGui::PopStyleColor(2);
            if (session.data.time.size() > 0) {
                ImGui::Text("Points: %zu | Path: %s", session.data.time.size(), session.filepath);

                if (session.first_frame) {
                    if (!session.data.time.empty()) {
                        session.x_min = session.data.time.front();
                        session.x_max = session.data.time.back();
                    }
                    session.first_frame = false;
                }

                if (ImPlot::BeginPlot("##OfflinePlot", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("Time(s)", "Value", 0, 0);
                    ImPlot::SetupAxisLimits(ImAxis_X1, session.x_min, session.x_max, ImGuiCond_Always);

                    if (ImPlot::IsPlotHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
                        double mouse_x = ImPlot::GetPlotMousePos().x;
                        double zoom_factor = (ImGui::GetIO().MouseWheel > 0) ? 0.9 : 1.1;
                        session.x_min = mouse_x - (mouse_x - session.x_min) * zoom_factor;
                        session.x_max = mouse_x + (session.x_max - mouse_x) * zoom_factor;
                    }

                    if (ImPlot::IsPlotHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                        double drag_x = ImGui::GetIO().MouseDelta.x * (session.x_max - session.x_min) / ImPlot::GetPlotSize().x;
                        session.x_min -= drag_x;
                        session.x_max -= drag_x;
                    }

                    for (const auto& kv : session.data.channels) {
                        int ch = kv.first;
                        std::string name = session.data.channel_names.count(ch) ? session.data.channel_names[ch] : "CH" + std::to_string(ch);
                        if (kv.second.size() == session.data.time.size()) {
                            ImPlot::PlotLine(name.c_str(), session.data.time.data(), kv.second.data(), (int)session.data.time.size());
                        }
                    }

                    if (ImPlot::IsPlotHovered()) {
                        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                        ImVec2 mouse_pixel = ImPlot::PlotToPixels(mouse);
                        ImDrawList* draw_list = ImPlot::GetPlotDrawList();
                        ImVec2 plot_min = ImPlot::GetPlotPos();
                        ImVec2 plot_max = ImVec2(plot_min.x + ImPlot::GetPlotSize().x, plot_min.y + ImPlot::GetPlotSize().y);

                        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                            session.measure_active = !session.measure_active;
                            session.measure_x = mouse.x;
                            session.measure_y = mouse.y;
                        }

                        auto draw_dashed_h = [&](float x1, float x2, float y, ImU32 col) {
                            for (float x = x1; x < x2; x += 10.0f) draw_list->AddLine(ImVec2(x, y), ImVec2((x + 5.0f < x2 ? x + 5.0f : x2), y), col, 1.0f);
                        };
                        auto draw_dashed_v = [&](float x, float y1, float y2, ImU32 col) {
                            for (float y = y1; y < y2; y += 10.0f) draw_list->AddLine(ImVec2(x, y), ImVec2(x, (y + 5.0f < y2 ? y + 5.0f : y2)), col, 1.0f);
                        };

                        if (session.measure_active) {
                            ImVec2 ref_pixel = ImPlot::PlotToPixels(session.measure_x, session.measure_y);
                            draw_dashed_v(ref_pixel.x, plot_min.y, plot_max.y, IM_COL32(255, 0, 0, 150));
                            draw_dashed_h(plot_min.x, plot_max.x, ref_pixel.y, IM_COL32(255, 0, 0, 150));
                            draw_list->AddRectFilled(ref_pixel, mouse_pixel, IM_COL32(255, 0, 0, 30));
                        }

                        draw_dashed_v(mouse_pixel.x, plot_min.y, plot_max.y, IM_COL32(255, 255, 0, 100));
                        draw_dashed_h(plot_min.x, plot_max.x, mouse_pixel.y, IM_COL32(255, 255, 0, 100));

                        ImGui::BeginTooltip();
                        ImGui::Text("Current X: %.4f s", mouse.x);
                        ImGui::Text("Current Y: %.2f", mouse.y);
                        if (session.measure_active) {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Ref X: %.4f s", session.measure_x);
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Ref Y: %.2f", session.measure_y);
                            ImGui::TextColored(ImVec4(0, 1, 1, 1), "dX: %.4f s (%.2f Hz)", mouse.x - session.measure_x, 1.0 / (mouse.x - session.measure_x));
                            ImGui::TextColored(ImVec4(0, 1, 1, 1), "dY: %.2f", mouse.y - session.measure_y);
                        } else {
                            ImGui::TextDisabled("(Press Space to measure)");
                        }
                        ImGui::EndTooltip();
                    }

                    ImPlot::EndPlot();
                }
            } else {
                ImGui::Text("Failed to load or file empty.");
            }
        }
        ImGui::End();
        ++it;
    }
}
