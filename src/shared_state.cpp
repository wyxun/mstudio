#include "shared_state.h"
#include "network_mgr.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

void SharedState::FetchNetworkData() {
    auto& net = NetworkMgr::GetInstance();

    // === [2a] 检测 Ch1 断→连跳变，清空历史数据，避免积压数据涌入 ===
    bool currently_connected = net.IsCh1Connected();
    if (currently_connected && !was_ch1_connected_) {
        for (auto& kv : ch_buffers_) kv.second.Erase();
        ch_buffers_.clear();
        ch_visibility_.clear();
        virtual_clock_         = 0.0;
        last_window_time_      = 0.0;
        estimator_initialized_ = false;
        ema_warmup_count_      = 0;
        actual_sample_rate_hz_ = 0.0;
        actual_window_samples_ = 0;
        actual_window_start_   = 0.0;
        exact_clock_started_   = false;
    }
    was_ch1_connected_ = currently_connected;

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

        // === [2b] desc 变更时，删除不在新列表中的僵尸 channel ===
        if (desc_changed) {
            const auto& new_channels = parser_.GetChannels();
            std::set<int> valid_indices;
            for (int i = 0; i < (int)new_channels.size(); i++)
                valid_indices.insert(i);
            for (auto it = ch_buffers_.begin(); it != ch_buffers_.end(); ) {
                if (!valid_indices.count(it->first)) {
                    ch_visibility_.erase(it->first);
                    it = ch_buffers_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        double reported_period = parser_.GetStreamPeriodSeconds();
        if (reported_period > 0.0) {
            smoothed_period_ = reported_period;
        }
        uint32_t snapshot_id = parser_.GetLastSnapshotId();
        if (snapshot_id != 0) {
            last_snapshot_id_ = snapshot_id;
        }
        double snapshot_period = parser_.GetSnapshotPeriodSeconds();
        if (snapshot_period > 0.0) {
            snapshot_rate_hz_ = 1.0 / snapshot_period;
        }
        dropped_samples_ = parser_.GetDroppedSamples();

        if (!paused_) {
            double now = ImGui::GetTime();
            time_last_ = now;

            // === [2c] 初始化（替代 last_window_time_==0.0 的隐式判断）===
            if (!estimator_initialized_) {
                last_window_time_      = now;
                virtual_clock_         = now;
                estimator_initialized_ = true;
                actual_window_start_   = now;
                actual_window_samples_ = 0;
            }

            for (const auto& s : samples) {
                if (!s.gap_marker && !s.is_snapshot) {
                    actual_window_samples_++;
                }
            }
            if (now - actual_window_start_ >= 1.0) {
                double elapsed = now - actual_window_start_;
                actual_sample_rate_hz_ = actual_window_samples_ / elapsed;
                actual_window_start_   = now;
                actual_window_samples_ = 0;
            }

            // === [2c] 可变权重 EMA + resume 保护 ===
            int arrival_count = 0;
            for (const auto& s : samples) {
                if (!s.has_exact_timestamp) arrival_count++;
            }
            points_in_window_ += arrival_count;

            if (resume_first_estimate_) {
                // resume 后首个估算窗口：重置起始时间，跳过本轮，避免暂停时长污染 period
                last_window_time_      = now;
                points_in_window_      = 0;
                resume_first_estimate_ = false;
            } else if (now - last_window_time_ > 0.5 && points_in_window_ > 0) {
                double estimated = (now - last_window_time_) / points_in_window_;
                estimated = std::max(0.000001, std::min(10.0, estimated));

                // 前 3 次估算采用激进权重，快速收敛到真实采样率
                float alpha = (ema_warmup_count_ == 0) ? 1.0f
                            : (ema_warmup_count_ == 1) ? 0.5f
                            : (ema_warmup_count_ == 2) ? 0.2f
                            :                            0.1f;
                smoothed_period_ = smoothed_period_ * (1.0 - (double)alpha)
                                 + estimated        * (double)alpha;
                if (ema_warmup_count_ < 3) ema_warmup_count_++;

                last_window_time_ = now;
                points_in_window_ = 0;
            }

            double exact_batch_anchor = 0.0;
            double exact_batch_duration = 0.0;
            bool exact_batch_active = false;

            for (const auto& s : samples) {
                if (s.batch_start && s.has_exact_timestamp) {
                    exact_batch_active = true;
                    if (exact_clock_started_ && s.sample_period > 0.0) {
                        exact_batch_anchor = virtual_clock_ + s.sample_period;
                    } else {
                        exact_batch_anchor = virtual_clock_;
                        exact_clock_started_ = true;
                    }
                    exact_batch_duration =
                        (s.batch_anchor_time > 0.0 &&
                         s.batch_anchor_time > s.timestamp)
                        ? s.batch_anchor_time - s.timestamp : 0.0;
                }

                if (s.gap_marker) {
                    double gap_time = s.timestamp;
                    if (s.batch_anchor_time > 0.0) {
                        gap_time = now - (s.batch_anchor_time - s.timestamp);
                    }
                    for (auto& kv : ch_buffers_) {
                        kv.second.AddPoint(gap_time, NAN);
                    }
                    time_last_ = gap_time;
                    if (is_recording_ && record_file_.is_open()) {
                        record_file_ << gap_time;
                        const auto& channels = parser_.GetChannels();
                        for (size_t i = 0; i < channels.size(); i++) {
                            auto it = last_csv_values_.find((int)i);
                            if (csv_forward_fill_ &&
                                it != last_csv_values_.end()) {
                                record_file_ << "," << it->second;
                            } else {
                                record_file_ << ",";
                            }
                        }
                        record_file_ << "\n";
                    }
                    continue;
                }

                if (s.has_exact_timestamp && s.batch_anchor_time > 0.0 &&
                    s.sample_period > 0.0 && exact_batch_active) {
                    double offset = s.batch_anchor_time - s.timestamp;
                    virtual_clock_ =
                        exact_batch_anchor + exact_batch_duration - offset;
                } else if (s.has_exact_timestamp) {
                    virtual_clock_ = now;
                } else {
                    virtual_clock_ += smoothed_period_;
                    if (virtual_clock_ > now + 0.5 ||
                        virtual_clock_ < now - 2.0) {
                        virtual_clock_ = now;
                    }
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
                            last_csv_values_[(int)i] = it->second;
                        } else if (csv_forward_fill_) {
                            auto last = last_csv_values_.find((int)i);
                            if (last != last_csv_values_.end()) {
                                record_file_ << "," << last->second;
                            } else {
                                record_file_ << ",";
                            }
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
