#include "waveform_panel.h"
#include "../shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include "implot.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

// ─── FOC 通道配色（示波器传统色）────────────────────────────────────────
static const ImVec4 kChannelColors[] = {
    ImVec4(1.00f, 0.86f, 0.20f, 1.0f), // CH0  Id   → 黄
    ImVec4(0.20f, 0.86f, 1.00f, 1.0f), // CH1  Iq   → 青
    ImVec4(0.20f, 1.00f, 0.39f, 1.0f), // CH2  速度 → 绿
    ImVec4(1.00f, 0.31f, 0.78f, 1.0f), // CH3  角度 → 洋红
    ImVec4(1.00f, 0.50f, 0.20f, 1.0f), // CH4+ 平衡 → 橙（循环）
};
static constexpr int kNumChannelColors = (int)(sizeof(kChannelColors) / sizeof(kChannelColors[0]));

// ─── 虚线绘制辅助函数（Render 与 RenderOfflineViewers 共用）──────────────────
static void DrawDashedH(ImDrawList* dl, float x1, float x2, float y, ImU32 col) {
    for (float x = x1; x < x2; x += 10.0f)
        dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 5.0f, x2), y), col, 1.0f);
}
static void DrawDashedV(ImDrawList* dl, float x, float y1, float y2, ImU32 col) {
    for (float y = y1; y < y2; y += 10.0f)
        dl->AddLine(ImVec2(x, y), ImVec2(x, std::min(y + 5.0f, y2)), col, 1.0f);
}

// 测量叠加层：实时视图和离线查看器共用
static void RenderMeasurementOverlay(
    ImDrawList* dl, ImVec2 plot_min, ImVec2 plot_max, ImPlotPoint mouse,
    bool measure_active, double measure_x, double measure_y)
{
    ImVec2 mp = ImPlot::PlotToPixels(mouse);
    DrawDashedV(dl, mp.x, plot_min.y, plot_max.y, IM_COL32(255, 255,  0, 100));
    DrawDashedH(dl, plot_min.x, plot_max.x, mp.y,  IM_COL32(255, 255,  0, 100));

    if (measure_active) {
        ImVec2 rp = ImPlot::PlotToPixels(measure_x, measure_y);
        DrawDashedV(dl, rp.x, plot_min.y, plot_max.y, IM_COL32(255,   0,   0, 150));
        DrawDashedH(dl, plot_min.x, plot_max.x, rp.y,  IM_COL32(255,   0,   0, 150));
        dl->AddRectFilled(rp, mp, IM_COL32(255, 0, 0, 30));
    }
}

void WaveformPanel::Render() {
    auto& state = state_;
    ImGui::Begin("Waveform");

    if (ImPlot::BeginPlot("##WaveformPlot", ImVec2(-1, -1))) {

        // ── 1. 集中完成所有 Setup API（绝对不可被 IsPlotHovered 等锁定 API 打断）
        ImPlotAxisFlags y_flags = ImPlotAxisFlags_None;
        if (state.request_fit_y_) {
            y_flags |= ImPlotAxisFlags_AutoFit;
            state.request_fit_y_ = false;
        }

        ImPlot::SetupAxes("Time(s)", "Value", ImPlotAxisFlags_None, y_flags);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 0.001, 120.0); // 1ms ~ 120s

        if (state.waveform_auto_scroll_) {
            // Run/Roll 模式：平滑实时追踪最新接收到的数据点，动态观察无卡顿
            if (state.time_last_ > 0.0) {
                state.display_x_max_ = state.time_last_;
                state.display_x_min_ = state.time_last_ - (double)state.history_window_;
            }
            ImPlot::SetupAxisLimits(ImAxis_X1, state.display_x_min_, state.display_x_max_, ImGuiCond_Always);
        } else {
            // Free 模式：维持用户当前画面，缩放看细节
            ImPlot::SetupAxisLimits(ImAxis_X1, state.display_x_min_, state.display_x_max_, ImGuiCond_Always);
        }

        // ── 2. Setup 结束，进行交互检测与模式切换 ─────────────────────────────
        bool is_hovered   = ImPlot::IsPlotHovered();
        bool user_panning = is_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        float wheel       = (ImGui::GetIO().MouseWheel != 0.0f) ? ImGui::GetIO().MouseWheel : ImGui::GetIO().MouseWheelH;
        bool user_zooming = is_hovered && (wheel != 0.0f) && !ImGui::GetIO().KeyCtrl;

        if ((user_panning || user_zooming) && state.waveform_auto_scroll_) {
            // 用户主动触碰 → 脱离 Roll 模式，进入 Free 模式
            state.waveform_auto_scroll_ = false;
        }

        // Roll 模式下 Ctrl+Wheel 调整 history_window_
        if (state.waveform_auto_scroll_ && is_hovered && ImGui::GetIO().KeyCtrl && wheel != 0.0f) {
            state.history_window_ *= (1.0f - wheel * 0.1f);
            state.history_window_  = std::clamp(state.history_window_, 0.001f, 120.0f);
        }

        // ── 3. 取得当前视口边界，并在 Free 模式下把 ImPlot 真实拖拽/缩放出的 Limits 反向同步 ──
        ImPlotRect plot_limits = ImPlot::GetPlotLimits();
        if (!state.waveform_auto_scroll_) {
            state.display_x_min_ = plot_limits.X.Min;
            state.display_x_max_ = plot_limits.X.Max;
        }

        float  plot_w_px  = ImPlot::GetPlotSize().x;
        double view_width = plot_limits.X.Max - plot_limits.X.Min;

        const auto& channels = state.parser_.GetChannels();
        for (auto& pair : state.ch_buffers_) {
            int   ch_idx = pair.first;
            auto& buf    = pair.second;
            if (buf.XData.empty()) continue;

            std::string name = (ch_idx < (int)channels.size())
                ? channels[ch_idx].name
                : "CH" + std::to_string(ch_idx);

            // FOC 配色 + 2px 线宽 + 散点切换
            ImVec4 col = kChannelColors[ch_idx % kNumChannelColors];

            ImPlotSpec spec;
            spec.LineColor  = col;    // ImVec4
            spec.LineWeight = 2.0f;

            // 散点切换：pts/pixel < 2 时加圆点标记，每个 1KHz 采样点清晰可见
            if (state.smoothed_period_ > 1e-9 && plot_w_px > 0) {
                double pts_in_view = view_width / state.smoothed_period_;
                if (pts_in_view / plot_w_px < 2.0) {
                    spec.Marker          = ImPlotMarker_Circle;
                    spec.MarkerSize      = 3.0f;
                    spec.MarkerFillColor = col;   // ImVec4
                }
            }

            ImPlot::PlotLineG(name.c_str(), [](int idx, void* data) {
                ScrollingBuffer* b = (ScrollingBuffer*)data;
                if (!b || b->XData.empty()) return ImPlotPoint(0.0, 0.0);
                int sz = (int)b->XData.size();
                int ri = (b->Offset + idx) % sz;
                if (ri < 0 || ri >= sz) return ImPlotPoint(0.0, 0.0);
                return ImPlotPoint(b->XData[ri], b->YData[ri]);
            }, &buf, (int)buf.XData.size(), spec);
        }

        // ── 5. 测量叠加层 ────────────────────────────────────────────────
        if (ImPlot::IsPlotHovered()) {
            ImPlotPoint mouse    = ImPlot::GetPlotMousePos();
            ImVec2      plot_min = ImPlot::GetPlotPos();
            ImVec2      plot_max = ImVec2(plot_min.x + plot_w_px,
                                          plot_min.y + ImPlot::GetPlotSize().y);
            ImDrawList* dl       = ImPlot::GetPlotDrawList();

            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                state.measure_active_ = !state.measure_active_;
                state.measure_x_      = mouse.x;
                state.measure_y_      = mouse.y;
            }

            RenderMeasurementOverlay(dl, plot_min, plot_max, mouse,
                state.measure_active_, state.measure_x_, state.measure_y_);

            ImGui::BeginTooltip();
            ImGui::Text("X: %.4f s", mouse.x);
            ImGui::Text("Y: %.3f",   mouse.y);
            if (state.measure_active_) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1,0,0,1), "Ref X: %.4f s", state.measure_x_);
                ImGui::TextColored(ImVec4(1,0,0,1), "Ref Y: %.3f",   state.measure_y_);
                double dx     = mouse.x - state.measure_x_;
                double abs_dx = std::abs(dx);
                ImGui::TextColored(ImVec4(0,1,1,1), "dX: %.4f s  (%.1f Hz)",
                    dx, abs_dx > 1e-6 ? 1.0 / abs_dx : 0.0);
                ImGui::TextColored(ImVec4(0,1,1,1), "dY: %.3f", mouse.y - state.measure_y_);
            }
            ImGui::EndTooltip();
        }

        // ── 6. 右上角 HUD：pts/pixel 比（告知用户是否在看原始数据）────────
        if (state.smoothed_period_ > 1e-9 && plot_w_px > 0) {
            double pts_in_view = view_width / state.smoothed_period_;
            double ppp         = pts_in_view / plot_w_px;
            char   hud[128];
            snprintf(hud, sizeof(hud), "%.0f pts | %.2f pts/px  [Shift:缩X  Alt:缩Y  在轴上:单轴缩放]", pts_in_view, ppp);
            ImVec2 hud_pos = ImPlot::GetPlotPos();
            hud_pos.x += plot_w_px - ImGui::CalcTextSize(hud).x - 8.0f;
            hud_pos.y += 6.0f;
            ImPlot::GetPlotDrawList()->AddText(hud_pos, IM_COL32(180,180,180,200), hud);
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
                        ImPlotPoint mouse    = ImPlot::GetPlotMousePos();
                        ImDrawList* dl       = ImPlot::GetPlotDrawList();
                        ImVec2      plot_min = ImPlot::GetPlotPos();
                        ImVec2      plot_max = ImVec2(plot_min.x + ImPlot::GetPlotSize().x,
                                                      plot_min.y + ImPlot::GetPlotSize().y);

                        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                            session.measure_active = !session.measure_active;
                            session.measure_x      = mouse.x;
                            session.measure_y      = mouse.y;
                        }

                        // 复用公共测量叠加层（不再重复 lambda）
                        RenderMeasurementOverlay(dl, plot_min, plot_max, mouse,
                            session.measure_active, session.measure_x, session.measure_y);

                        ImGui::BeginTooltip();
                        ImGui::Text("X: %.4f s", mouse.x);
                        ImGui::Text("Y: %.3f",   mouse.y);
                        if (session.measure_active) {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1,0,0,1), "Ref X: %.4f s", session.measure_x);
                            ImGui::TextColored(ImVec4(1,0,0,1), "Ref Y: %.3f",   session.measure_y);
                            double dx     = mouse.x - session.measure_x;
                            double abs_dx = std::abs(dx);
                            ImGui::TextColored(ImVec4(0,1,1,1), "dX: %.4f s  (%.1f Hz)",
                                dx, abs_dx > 1e-6 ? 1.0 / abs_dx : 0.0);
                            ImGui::TextColored(ImVec4(0,1,1,1), "dY: %.3f", mouse.y - session.measure_y);
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

