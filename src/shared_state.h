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
    ProtocolParser parser_{8};

    // Waveform buffers
    std::map<int, ScrollingBuffer> ch_buffers_;
    std::map<int, bool> ch_visibility_;
    bool paused_ = false;
    double time_last_ = 0.0;
    float history_window_ = 10.0f;

    // Recording
    bool is_recording_ = false;
    std::ofstream record_file_;
    std::string recording_filename_;

    // Terminal
    std::string term_log_;
    char term_input_buf_[256] = {0};
    bool auto_scroll_ = true;
    ImGuiTextFilter term_filter_;

    // Adaptive clock
    double virtual_clock_ = 0.0;
    double smoothed_period_ = 0.001;
    double last_window_time_ = 0.0;
    int points_in_window_ = 0;

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
