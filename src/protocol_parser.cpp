#include "protocol_parser.h"
#include <chrono>
#include <cstring>

#define SYNC_H 0xAA
#define SYNC_L 0x55
#define TYPE_DESC 0xFD

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

static double GetTimeSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

bool ProtocolParser::Feed(const std::vector<uint8_t>& data, std::vector<DataSample>& ready_samples) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    bool desc_updated = false;

    while (buffer_.size() >= 2) {
        if (buffer_[0] != SYNC_H || buffer_[1] != SYNC_L) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        if (buffer_.size() < 4) {
            break; // Need more data header
        }

        uint8_t frame_type = buffer_[2];

        if (frame_type == TYPE_DESC) {
            uint8_t ch_count = buffer_[3];
            size_t frame_len = 4 + 12 * ch_count + 1;
            if (buffer_.size() < frame_len) {
                break;
            }

            uint8_t crc = buffer_[frame_len - 1];
            if (CalcCRC(&buffer_[2], frame_len - 3) == crc) {
                channels_.clear();
                for (int i = 0; i < ch_count; ++i) {
                    size_t off = 4 + i * 12;
                    ChannelDesc desc;
                    char name_buf[9] = {0};
                    std::memcpy(name_buf, &buffer_[off], 8);
                    desc.name = name_buf;

                    float scale;
                    std::memcpy(&scale, &buffer_[off + 8], 4);
                    desc.scale = scale;

                    channels_.push_back(desc);
                }
                /* The firmware derives the data-frame mask length from the
                 * registered channel count. Learn it from the descriptor
                 * instead of the compile-time constructor value, otherwise
                 * every data frame is misframed when they differ. */
                mask_bytes_ = (ch_count + 7) / 8;
                if (mask_bytes_ < 1) {
                    mask_bytes_ = 1;
                }
                desc_updated = true;
                buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
            } else {
                /* CRC mismatch: this is not a valid descriptor. It may be a
                 * data frame whose seq byte happens to be 0xFD, or stream
                 * corruption. Resync one byte at a time instead of erasing
                 * a whole descriptor length (which eats following frames). */
                buffer_.erase(buffer_.begin());
            }
        } else {
            // Data frame
            uint8_t seq = frame_type; // the third byte is seq logically for data frames
            (void)seq;

            int active_count = 0;
            for (int i = 0; i < mask_bytes_; ++i) {
                uint8_t b = buffer_[3 + i];
                // Kernighan's bit count
                for (; b; active_count++) {
                    b &= b - 1;
                }
            }

            size_t frame_len = 3 + mask_bytes_ + active_count * 2 + 1;
            if (buffer_.size() < frame_len) {
                break;
            }

            uint8_t crc = buffer_[frame_len - 1];
            uint8_t calc_crc = CalcCRC(&buffer_[2], frame_len - 3);
            if (calc_crc == crc) {
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
                            sample.ch_values[ch_idx] = (float)raw_val / channels_[ch_idx].scale;
                        } else {
                            sample.ch_values[ch_idx] = (float)raw_val;
                        }
                    }
                }
                ready_samples.push_back(std::move(sample));
            }
            buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
        }
    }
    return desc_updated;
}
