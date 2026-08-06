#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "protocol_parser.h"
#include "macro_mgr.h"
#include "imgui.h"
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>
#endif

struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    std::vector<double> XData;
    std::vector<float> YData;

    ScrollingBuffer(int max_size = 500000) {
        MaxSize = max_size;
        Offset  = 0;
        XData.reserve(MaxSize);
        YData.reserve(MaxSize);
    }
    void AddPoint(double x, float y) {
        if ((int)XData.size() < MaxSize) {
            XData.push_back(x);
            YData.push_back(y);
        } else {
            XData[Offset] = x;
            YData[Offset] = y;
            Offset = (Offset + 1) % MaxSize;
        }
    }
    void Erase() {
        if (XData.size() > 0) {
            XData.shrink_to_fit();
            YData.shrink_to_fit();
            XData.clear();
            YData.clear();
            Offset = 0;
        }
    }
};

struct OfflineData {
    std::vector<double> time;
    std::map<int, std::vector<double>> channels;
    std::map<int, std::string> channel_names;
};

struct OfflineSession {
    int id;
    bool open = true;
    bool first_frame = true;
    double x_min = 0, x_max = 10;
    bool measure_active = false;
    double measure_x = 0;
    double measure_y = 0;
    char filepath[256] = {0};
    OfflineData data;
};

struct SharedState {
    ProtocolParser parser_{16};

    // Waveform buffers
    std::map<int, ScrollingBuffer> ch_buffers_;
    std::map<int, bool> ch_visibility_;
    bool paused_ = false;
    double time_last_ = 0.0;
    float history_window_ = 10.0f;

    // Waveform 交互状态（与 terminal auto_scroll_ 区分）
    bool   waveform_auto_scroll_     = true;  // true=X轴跟随最新数据, false=用户自由控制
    bool   request_fit_y_            = false; // 一键自适应 Y 轴量程
    bool   sine_only_mode_           = false; // 只显示 TestSin，验证正弦绘制

    // 显示节流（Roll 模式下限制 X 轴更新频率）
    double display_x_min_            = 0.0;
    double display_x_max_            = 10.0;
    double last_display_update_time_ = 0.0;

    // Recording
    bool is_recording_ = false;
    std::ofstream record_file_;
    std::string recording_filename_;
    bool csv_forward_fill_ = false;
    std::map<int, float> last_csv_values_;

    // Terminal
    std::string term_log_;
    char term_input_buf_[256] = {0};
    bool auto_scroll_ = true;
    ImGuiTextFilter term_filter_;

    // Adaptive clock
    double virtual_clock_   = 0.0;
    double smoothed_period_ = 0.001;
    double last_window_time_ = 0.0;
    int    points_in_window_ = 0;
    bool   exact_clock_started_ = false;

    // 采样率估算器状态
    bool estimator_initialized_ = false; // 替代 last_window_time_==0.0 的隐式判断
    bool resume_first_estimate_ = false; // resume 后跳过首次估算窗口
    int  ema_warmup_count_      = 0;     // 可变权重 EMA 预热计数

    // 连接状态追踪（检测断→连跳变）
    bool was_ch1_connected_ = false;

    // MCU-reported timing / snapshot state
    uint32_t last_snapshot_id_ = 0;
    double snapshot_rate_hz_ = 0.0;
    uint64_t dropped_samples_ = 0;
    double actual_sample_rate_hz_ = 0.0;
    uint32_t actual_window_samples_ = 0;
    double actual_window_start_ = 0.0;

    // Real-time measurement
    bool measure_active_ = false;
    double measure_x_ = 0.0;
    double measure_y_ = 0.0;

    // Offline sessions
    std::vector<OfflineSession> offline_sessions_;
    int next_session_id_ = 0;

    // Macros
    MacroManager macro_mgr_;

    void FetchNetworkData();
    void LoadCSV(OfflineSession& session);
    std::string OpenFileDialog();
    std::string OpenIniFileDialog();
};

#endif // SHARED_STATE_H
