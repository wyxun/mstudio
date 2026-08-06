#include "protocol_parser.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static uint8_t XorCrc(const std::vector<uint8_t>& data, size_t start) {
    uint8_t crc = 0xFF;
    for (size_t i = start; i < data.size(); ++i) crc ^= data[i];
    return crc;
}

static uint16_t Crc16(const std::vector<uint8_t>& data, size_t start) {
    uint16_t crc = 0xFFFF;
    for (size_t i = start; i < data.size(); ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
}

static void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void PutFloat(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(out, bits);
}

static std::vector<uint8_t> MakeDescriptor() {
    std::vector<uint8_t> f;
    f.push_back(0xAA); f.push_back(0x55); f.push_back(0xFD);
    f.push_back(2);

    const char* name0 = "Ch0";
    const char* name1 = "Ch1";
    for (int i = 0; i < 8; ++i) f.push_back((uint8_t)name0[i]);
    PutFloat(f, 1.0f);
    for (int i = 0; i < 8; ++i) f.push_back((uint8_t)name1[i]);
    PutFloat(f, 1000.0f);

    f.push_back(XorCrc(f, 2));
    return f;
}

static std::vector<uint8_t> MakeMeta() {
    std::vector<uint8_t> f;
    f.push_back(0xAA); f.push_back(0x55); f.push_back(0xFE);
    f.push_back(1);
    f.push_back(2);
    PutU32(f, 1000000u);
    PutU16(f, 32);
    f.push_back(0);
    f.push_back(XorCrc(f, 2));
    return f;
}

static std::vector<uint8_t> MakeBatch() {
    std::vector<uint8_t> f;
    f.push_back(0xAA); f.push_back(0x55); f.push_back(0xFC);
    f.push_back(1);
    f.push_back(2);
    PutU32(f, 10u);
    PutU16(f, 2);
    PutU32(f, 50000u);

    f.push_back(0x01);
    PutU16(f, 1000);

    f.push_back(0x02);
    PutU16(f, 2000);

    PutU16(f, Crc16(f, 2));
    return f;
}

static std::vector<uint8_t> MakeSnapshot() {
    std::vector<uint8_t> f;
    f.push_back(0xAA); f.push_back(0x55); f.push_back(0xFA);
    f.push_back(1);
    f.push_back(2);
    PutU32(f, 20u);
    PutU16(f, 1);
    PutU32(f, 50000u);
    PutU32(f, 7u);

    f.push_back(0x03);
    PutU16(f, 1);
    PutU16(f, 2000);

    PutU16(f, Crc16(f, 2));
    return f;
}

int main() {
    ProtocolParser parser(8);
    std::vector<DataSample> samples;

    bool desc = parser.Feed(MakeDescriptor(), samples);
    assert(desc);
    assert(parser.GetChannels().size() == 2);
    assert(samples.empty());

    samples.clear();
    bool meta = parser.Feed(MakeMeta(), samples);
    assert(!meta);
    assert(parser.GetStreamRateHz() > 999.0 &&
           parser.GetStreamRateHz() < 1001.0);
    assert(samples.empty());

    samples.clear();
    parser.Feed(MakeBatch(), samples);
    assert(samples.size() == 2);
    assert(samples[0].sample_index == 10u);
    assert(samples[1].sample_index == 11u);
    assert(samples[0].has_exact_timestamp);
    assert(samples[0].ch_values.at(0) == 1000.0f);
    assert(samples[1].ch_values.at(1) == 2.0f);
    assert(samples[0].sample_period == 0.00005);

    samples.clear();
    parser.Feed(MakeSnapshot(), samples);
    assert(samples.size() == 1);
    assert(samples[0].is_snapshot);
    assert(samples[0].sample_index == 20u);
    assert(parser.GetLastSnapshotId() == 7u);
    assert(parser.GetSnapshotRateHz() > 19999.0 &&
           parser.GetSnapshotRateHz() < 20001.0);

    std::vector<uint8_t> bad = MakeBatch();
    bad[bad.size() - 1] ^= 0xFF;
    samples.clear();
    parser.Feed(bad, samples);
    assert(samples.empty());
    assert(parser.GetDroppedSamples() >= 2);

    samples.clear();
    parser.Feed(MakeSnapshot(), samples);
    assert(samples.size() == 1);

    std::printf("protocol_parser_test: OK\n");
    return 0;
}
