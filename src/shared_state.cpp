#include "shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

void SharedState::FetchNetworkData() {
    auto& net = NetworkMgr::GetInstance();

    // Fetch Ch0 Log lines
    std::string ch0_str;
    if (net.FetchCh0Data(ch0_str)) {
        term_log_ += ch0_str;
        if (term_log_.size() > 500000) {
            term_log_.erase(0, term_log_.size() - 250000);
        }
    }

    // Fetch Ch1 Binary Stream
    std::vector<uint8_t> ch1_bytes;
    if (net.FetchCh1Data(ch1_bytes)) {
        std::vector<DataSample> samples;
        bool desc_changed = parser_.Feed(ch1_bytes, samples);

        if (desc_changed) {
            // New channels map received, could reset buffer or handle dynamically
        }

        if (!paused_) {
            double now = ImGui::GetTime();
            time_last_ = now;

            if (last_window_time_ == 0.0) {
                last_window_time_ = now;
                virtual_clock_ = now;
            }

            // Adaptive period estimation (every 500ms)
            points_in_window_ += (int)samples.size();
            if (now - last_window_time_ > 0.5) {
                if (points_in_window_ > 0) {
                    double estimated = (now - last_window_time_) / points_in_window_;
                    if (estimated > 10.0) estimated = 10.0;
                    if (estimated < 0.000001) estimated = 0.000001;

                    if (smoothed_period_ == 0.001) smoothed_period_ = estimated;
                    else smoothed_period_ = smoothed_period_ * 0.9 + estimated * 0.1;
                }
                last_window_time_ = now;
                points_in_window_ = 0;
            }

            for (const auto& s : samples) {
                virtual_clock_ += smoothed_period_;

                if (virtual_clock_ > now + 0.5 || virtual_clock_ < now - 2.0) {
                    virtual_clock_ = now;
                }

                time_last_ = virtual_clock_;

                for (const auto& kv : s.ch_values) {
                    int ch_idx = kv.first;
                    float val = kv.second;
                    if (ch_buffers_.find(ch_idx) == ch_buffers_.end()) {
                        ch_buffers_[ch_idx] = ScrollingBuffer(1000000);
                        ch_visibility_[ch_idx] = true;
                    }
                    ch_buffers_[ch_idx].AddPoint(virtual_clock_, val);
                }

                // Record to CSV
                if (is_recording_ && record_file_.is_open()) {
                    record_file_ << virtual_clock_;
                    const auto& channels = parser_.GetChannels();
                    for (size_t i = 0; i < channels.size(); i++) {
                        auto it = s.ch_values.find(i);
                        if (it != s.ch_values.end()) {
                            record_file_ << "," << it->second;
                        } else {
                            record_file_ << ",";
                        }
                    }
                    record_file_ << "\n";
                }
            }
        }
    }
}

void SharedState::LoadCSV(OfflineSession& session) {
    std::ifstream file(session.filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV: " << session.filepath << std::endl;
        return;
    }

    session.data.time.clear();
    session.data.channels.clear();
    session.data.channel_names.clear();

    std::string line;
    // Parse Header
    if (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::stringstream ss(line);
        std::string token;
        std::getline(ss, token, ','); // Time
        int ch_idx = 0;
        while (std::getline(ss, token, ',')) {
            session.data.channel_names[ch_idx] = token;
            session.data.channels[ch_idx] = std::vector<double>();
            ch_idx++;
        }
    }

    // Parse Data
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        std::stringstream ss(line);
        std::string token;
        if (!std::getline(ss, token, ',')) continue;

        try {
            double t = std::stod(token);
            session.data.time.push_back(t);

            for (auto& pair : session.data.channels) {
                if (std::getline(ss, token, ',') && !token.empty()) {
                    try {
                        pair.second.push_back(std::stod(token));
                    } catch (...) {
                        pair.second.push_back(0.0);
                    }
                } else {
                    pair.second.push_back(0.0);
                }
            }
        } catch (...) {}
    }
}

std::string SharedState::OpenFileDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "CSV Files\0*.csv\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
#endif
    return "";
}

std::string SharedState::OpenIniFileDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "INI Files\0*.ini\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
#endif
    return "";
}
