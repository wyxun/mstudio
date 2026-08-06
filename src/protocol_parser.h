#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <vector>
#include <string>
#include <cstdint>
#include <map>

// Represents an identified channel mapping from the device
struct ChannelDesc {
    std::string name;
    float scale;
};

// Extracted frame data representing physics values
struct DataSample {
    double timestamp = 0.0;  // Host or device-derived time for rendering
    uint32_t sample_index = 0;
    double sample_period = 0.0;
    bool has_exact_timestamp = false;
    bool is_snapshot = false;
    bool gap_marker = false;
    bool batch_start = false;
    double batch_anchor_time = 0.0;
    std::map<int, float> ch_values; // ch_idx to mapped physical value
};

class ProtocolParser {
public:
    ProtocolParser(int max_channels = 16);
    ~ProtocolParser() = default;

    // Feeds bytes into protocol parser. 
    // Populates ready_samples for 'data' frames. Returns true if 'desc' frame updated descriptions.
    bool Feed(const std::vector<uint8_t>& data, std::vector<DataSample>& ready_samples);

    const std::vector<ChannelDesc>& GetChannels() const { return channels_; }
    double GetStreamPeriodSeconds() const { return stream_period_s_; }
    double GetStreamRateHz() const {
        return stream_period_s_ > 0.0 ? 1.0 / stream_period_s_ : 0.0;
    }
    double GetSnapshotPeriodSeconds() const { return snapshot_period_s_; }
    double GetSnapshotRateHz() const {
        return snapshot_period_s_ > 0.0 ? 1.0 / snapshot_period_s_ : 0.0;
    }
    uint32_t GetLastSnapshotId() const { return last_snapshot_id_; }
    uint64_t GetDroppedSamples() const { return dropped_samples_; }

private:
    int max_channels_;
    int mask_bytes_;
    std::vector<ChannelDesc> channels_;
    std::vector<uint8_t> buffer_;
    double stream_period_s_ = 0.0;
    double snapshot_period_s_ = 0.0;
    uint32_t last_snapshot_id_ = 0;
    uint64_t dropped_samples_ = 0;
    int last_seq_ = -1;
    bool has_last_sample_index_ = false;
    uint32_t last_sample_index_ = 0;

    uint8_t CalcCRC(const uint8_t* payload, size_t len) const;
    uint16_t CalcCRC16(const uint8_t* payload, size_t len) const;
};

#endif // PROTOCOL_PARSER_H
