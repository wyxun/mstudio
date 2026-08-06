#include "protocol_parser.h"
#include <chrono>
#include <cstring>

#define SYNC_H 0xAA
#define SYNC_L 0x55
#define TYPE_DESC 0xFD
#define TYPE_META 0xFE
#define TYPE_BATCH 0xFC
#define TYPE_SNAPSHOT 0xFA

ProtocolParser::ProtocolParser(int max_channels)
    : max_channels_(max_channels) {
    mask_bytes_ = (max_channels_ + 7) / 8;
}

uint8_t ProtocolParser::CalcCRC(const uint8_t* payload, size_t len) const {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= payload[i];
    }
    return crc;
}

uint16_t ProtocolParser::CalcCRC16(const uint8_t* payload, size_t len) const {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)payload[i] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static double GetTimeSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

static uint16_t ReadU16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int Popcount(uint8_t b) {
    int n = 0;
    for (; b; n++) b &= (uint8_t)(b - 1);
    return n;
}

bool ProtocolParser::Feed(const std::vector<uint8_t>& data, std::vector<DataSample>& ready_samples) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    bool desc_updated = false;

    while (buffer_.size() >= 2) {
        if (buffer_[0] != SYNC_H || buffer_[1] != SYNC_L) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        if (buffer_.size() < 3) break;
        uint8_t frame_type = buffer_[2];

        if (frame_type == TYPE_DESC) {
            if (buffer_.size() < 4) break;
            uint8_t ch_count = buffer_[3];
            size_t frame_len = 4 + 12 * ch_count + 1;
            if (buffer_.size() < frame_len) break;

            uint8_t crc = buffer_[frame_len - 1];
            if (CalcCRC(&buffer_[2], frame_len - 3) == crc) {
                channels_.clear();
                for (int i = 0; i < ch_count; ++i) {
                    size_t off = 4 + i * 12;
                    ChannelDesc desc;
                    char name_buf[9] = {0};
                    std::memcpy(name_buf, &buffer_[off], 8);
                    desc.name = name_buf;
                    std::memcpy(&desc.scale, &buffer_[off + 8], 4);
                    channels_.push_back(desc);
                }
                mask_bytes_ = (ch_count + 7) / 8;
                if (mask_bytes_ < 1) mask_bytes_ = 1;
                last_seq_ = -1;
                has_last_sample_index_ = false;
                desc_updated = true;
                buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
            } else {
                buffer_.erase(buffer_.begin());
            }
            continue;
        }

        if (frame_type == TYPE_META) {
            const size_t frame_len = 13;
            if (buffer_.size() < frame_len) break;

            uint8_t crc = buffer_[frame_len - 1];
            if (CalcCRC(&buffer_[2], frame_len - 3) == crc) {
                uint32_t period_ns = ReadU32(&buffer_[5]);
                if (period_ns > 0) {
                    stream_period_s_ = (double)period_ns / 1e9;
                }
                buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
            } else {
                buffer_.erase(buffer_.begin());
            }
            continue;
        }

        if (frame_type == TYPE_BATCH || frame_type == TYPE_SNAPSHOT) {
            const size_t min_header = 15;
            if (buffer_.size() < min_header) break;

            uint8_t ch_count = buffer_[4];
            uint16_t sample_count = ReadU16(&buffer_[9]);
            uint32_t start_index = ReadU32(&buffer_[5]);
            uint32_t period_ns = ReadU32(&buffer_[11]);
            size_t payload_start = (frame_type == TYPE_SNAPSHOT) ? 19 : 15;
            size_t mask_bytes = (ch_count + 7) / 8;
            if (mask_bytes < 1) mask_bytes = 1;

            if (sample_count == 0 || ch_count == 0 ||
                ch_count > (uint8_t)max_channels_ ||
                ch_count != (uint8_t)channels_.size()) {
                buffer_.erase(buffer_.begin());
                continue;
            }

            size_t off = payload_start;
            bool incomplete = false;
            std::vector<size_t> active_counts;
            active_counts.reserve(sample_count);

            for (uint16_t s = 0; s < sample_count; ++s) {
                if (off + mask_bytes > buffer_.size()) {
                    incomplete = true;
                    break;
                }
                int active = 0;
                for (size_t m = 0; m < mask_bytes; ++m) {
                    active += Popcount(buffer_[off + m]);
                }
                active_counts.push_back((size_t)active);
                off += mask_bytes + 2 * (size_t)active;
            }

            if (incomplete) break;
            size_t frame_len = off + 2;
            if (buffer_.size() < frame_len) break;

            uint16_t crc = ReadU16(&buffer_[off]);
            if (CalcCRC16(&buffer_[2], off - 2) != crc) {
                dropped_samples_ += sample_count;
                buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
                continue;
            }

            double period_s = period_ns > 0 ? (double)period_ns / 1e9 : 0.0;
            double now = GetTimeSeconds();
            double end_time = now;
            double start_time = sample_count > 0
                ? end_time - (double)(sample_count - 1) * period_s
                : end_time;

            if (frame_type == TYPE_BATCH && has_last_sample_index_ &&
                start_index > last_sample_index_ + 1) {
                DataSample gap;
                gap.gap_marker = true;
                gap.sample_index = last_sample_index_ + 1;
                gap.timestamp = start_time;
                gap.has_exact_timestamp = true;
                gap.batch_anchor_time = now;
                ready_samples.push_back(std::move(gap));
            }

            size_t data_off = payload_start;
            for (uint16_t s = 0; s < sample_count; ++s) {
                DataSample sample;
                sample.timestamp = start_time + (double)s * period_s;
                sample.sample_index = start_index + s;
                sample.sample_period = period_s;
                sample.has_exact_timestamp = true;
                sample.is_snapshot = (frame_type == TYPE_SNAPSHOT);
                sample.batch_start = (s == 0);
                sample.batch_anchor_time = now;

                for (size_t ch_idx = 0; ch_idx < channels_.size(); ++ch_idx) {
                    size_t byte_idx = ch_idx / 8;
                    size_t bit_idx = ch_idx % 8;
                    if (buffer_[data_off + byte_idx] & (1 << bit_idx)) {
                        int16_t raw_val;
                        size_t val_off = data_off + mask_bytes;
                        for (size_t k = 0; k < ch_idx; ++k) {
                            size_t kb = k / 8;
                            size_t kbit = k % 8;
                            if (buffer_[data_off + kb] & (1 << kbit)) {
                                val_off += 2;
                            }
                        }
                        std::memcpy(&raw_val, &buffer_[val_off], 2);
                        if (channels_[ch_idx].scale != 0.0f) {
                            sample.ch_values[(int)ch_idx] =
                                (float)raw_val / channels_[ch_idx].scale;
                        } else {
                            sample.ch_values[(int)ch_idx] = (float)raw_val;
                        }
                    }
                }
                data_off += mask_bytes + 2 * active_counts[s];
                ready_samples.push_back(std::move(sample));
            }

            if (sample_count > 0 && frame_type == TYPE_BATCH) {
                last_sample_index_ = start_index + sample_count - 1;
                has_last_sample_index_ = true;
                if (period_ns > 0) {
                    stream_period_s_ = (double)period_ns / 1e9;
                }
            } else if (frame_type == TYPE_SNAPSHOT) {
                last_snapshot_id_ = ReadU32(&buffer_[15]);
                if (period_ns > 0) {
                    snapshot_period_s_ = (double)period_ns / 1e9;
                }
            }

            buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
            continue;
        }

        // Legacy data frame: byte 2 is the sequence number.
        uint8_t seq = frame_type;
        if (last_seq_ >= 0) {
            int gap = (seq - last_seq_ - 1) & 0xFF;
            if (gap > 0) dropped_samples_ += (uint64_t)gap;
        }
        last_seq_ = seq;

        int active_count = 0;
        for (int i = 0; i < mask_bytes_; ++i) {
            active_count += Popcount(buffer_[3 + i]);
        }

        size_t frame_len = 3 + mask_bytes_ + 2 * active_count + 1;
        if (buffer_.size() < frame_len) break;

        uint8_t crc = buffer_[frame_len - 1];
        if (CalcCRC(&buffer_[2], frame_len - 3) != crc) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
            continue;
        }

        DataSample sample;
        sample.timestamp = GetTimeSeconds();
        size_t val_idx = 3 + mask_bytes_;
        for (size_t ch_idx = 0; ch_idx < channels_.size(); ++ch_idx) {
            size_t byte_idx = ch_idx / 8;
            size_t bit_idx = ch_idx % 8;
            if (buffer_[3 + byte_idx] & (1 << bit_idx)) {
                int16_t raw_val;
                std::memcpy(&raw_val, &buffer_[val_idx], 2);
                val_idx += 2;
                if (channels_[ch_idx].scale != 0.0f) {
                    sample.ch_values[(int)ch_idx] =
                        (float)raw_val / channels_[ch_idx].scale;
                } else {
                    sample.ch_values[(int)ch_idx] = (float)raw_val;
                }
            }
        }
        ready_samples.push_back(std::move(sample));
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
    }
    return desc_updated;
}
