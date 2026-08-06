#include "wave_cmd.h"
#include "protocol_parser.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

namespace {
bool EnsureWSA() {
#ifdef _WIN32
    static bool init = false;
    if (!init) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        init = true;
    }
#endif
    return true;
}

SOCKET ConnectTCP(const char* host, int port) {
    EnsureWSA();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#endif
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    inet_pton(AF_INET, host, &addr.sin_addr);
#else
    addr.sin_addr.s_addr = inet_addr(host);
#endif
    connect(s, (sockaddr*)&addr, sizeof(addr));
    fd_set wset, eset;
    FD_ZERO(&wset); FD_SET(s, &wset);
    FD_ZERO(&eset); FD_SET(s, &eset);
    timeval tv = {1, 0};
    select((int)(s + 1), nullptr, &wset, &eset, &tv);
    if (!FD_ISSET(s, &wset)) { closesocket(s); return INVALID_SOCKET; }
    return s;
}

static void SendShellCmd(const std::string& cmd) {
    SOCKET s = ConnectTCP("127.0.0.1", 9090);
    if (s == INVALID_SOCKET) {
        std::cerr << "Failed to connect to RTT Ch0 (TCP 9090).\n";
        return;
    }
    std::string line = cmd + "\r\n";
    send(s, line.c_str(), (int)line.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char buf[4096]; int n;
    bool got_data = false;
    while ((n = recv(s, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = 0; std::cout << buf; got_data = true;
    }
    closesocket(s);
    if (!got_data) std::cout << "OK\n";
}

} // anonymous namespace

static void PrintUsage() {
    std::cerr << "Usage: aitrace wave <subcommand> [args]\n"
              << "  list                    List channels\n"
              << "  start                   Start acquisition\n"
              << "  stop                    Stop acquisition\n"
              << "  rate <n>                Set decimation rate\n"
              << "  capture <seconds>       Capture CSV to stdout\n"
              << "  capture <s> --output <f> Capture CSV to file\n"
              << "  stat [seconds]          Link quality: frame rate / crc_err / seq_lost\n";
}

int wave_main(int argc, char* argv[]) {
    if (argc < 2) { PrintUsage(); return 1; }

    std::string sub = argv[1];

    // Control commands go via RTT Ch0
    if (sub == "start" || sub == "stop" || sub == "list" || sub == "rate") {
        std::string cmd = "wave " + sub;
        if (sub == "rate") {
            if (argc < 3) { std::cerr << "Usage: aitrace wave rate <n>\n"; return 1; }
            cmd += " " + std::string(argv[2]);
        }
        SendShellCmd(cmd);
        return 0;
    }

    // stat [seconds] — raw frame accounting: rate / CRC errors / seq gaps.
    // Cross-check with firmware `wave drop`: produced = ok + seq_lost + drops.
    if (sub == "stat") {
        double duration = (argc >= 3) ? std::stod(argv[2]) : 5.0;

        SOCKET s = ConnectTCP("127.0.0.1", 9091);
        if (s == INVALID_SOCKET) {
            std::cerr << "Failed to connect to RTT Ch1 (TCP 9091).\n";
            return 1;
        }

        std::vector<uint8_t> buf;
        int mask_bytes = 0;      // learned from descriptor frames
        int last_seq = -1;
        size_t n_ok = 0, n_crc = 0, n_gap = 0, n_bytes = 0;
        size_t tot_ok = 0, tot_crc = 0, tot_gap = 0;

        auto rd16 = [](const std::vector<uint8_t>& b, size_t off) -> uint16_t {
            return (uint16_t)(b[off] | ((uint16_t)b[off + 1] << 8));
        };
        auto crc16 = [](const uint8_t* p, size_t len) -> uint16_t {
            uint16_t crc = 0xFFFF;
            for (size_t k = 0; k < len; k++) {
                crc ^= (uint16_t)p[k] << 8;
                for (int bit = 0; bit < 8; bit++) {
                    crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                         : (uint16_t)(crc << 1);
                }
            }
            return crc;
        };

        /* Parse buffered frames; statistics are only accumulated when
         * 'counting' is true (warmup parses silently to sync up and learn
         * the descriptor). */
        auto parse = [&](bool counting) {
            size_t i = 0;
            while (true) {
                while (i + 1 < buf.size() &&
                       !(buf[i] == 0xAA && buf[i + 1] == 0x55)) i++;
                if (i + 3 > buf.size()) break;
                uint8_t third = buf[i + 2];
                if (third == 0xFD) { // descriptor frame
                    if (i + 4 > buf.size()) break;
                    size_t flen = 4 + buf[i + 3] * 12 + 1;
                    if (i + flen > buf.size()) break;
                    uint8_t crc = 0xFF;
                    for (size_t k = i + 2; k < i + flen - 1; k++) crc ^= buf[k];
                    if (crc == buf[i + flen - 1]) {
                        mask_bytes = (buf[i + 3] + 7) / 8;
                        if (mask_bytes < 1) mask_bytes = 1;
                        i += flen;
                    } else {
                        if (counting) n_crc++;
                        i += 1;
                    }
                    continue;
                }
                if (third == 0xFE) { // metadata frame
                    const size_t flen = 13;
                    if (i + flen > buf.size()) break;
                    uint8_t crc = 0xFF;
                    for (size_t k = i + 2; k < i + flen - 1; k++) crc ^= buf[k];
                    if (crc == buf[i + flen - 1]) {
                        i += flen;
                    } else {
                        if (counting) n_crc++;
                        i += 1;
                    }
                    continue;
                }
                if (third == 0xFC || third == 0xFA) { // batch / snapshot
                    const size_t min_header = (third == 0xFA) ? 19 : 15;
                    if (i + min_header > buf.size()) break;
                    uint8_t ch_count = buf[i + 4];
                    uint16_t sample_count = rd16(buf, i + 9);
                    size_t local_mask = (ch_count + 7) / 8;
                    if (local_mask < 1) local_mask = 1;
                    size_t off = i + min_header;
                    bool incomplete = false;
                    for (uint16_t s = 0; s < sample_count; s++) {
                        if (off + local_mask > buf.size()) {
                            incomplete = true;
                            break;
                        }
                        int active = 0;
                        for (size_t m = 0; m < local_mask; m++) {
                            uint8_t b = buf[off + m];
                            for (; b; active++) b &= (uint8_t)(b - 1);
                        }
                        off += local_mask + 2 * (size_t)active;
                    }
                    if (incomplete) break;
                    size_t flen = off - i + 2;
                    if (i + flen > buf.size()) break;
                    uint16_t crc = rd16(buf, off);
                    if (crc == crc16(&buf[i + 2], off - (i + 2))) {
                        if (counting) n_ok++;
                        i += flen;
                    } else {
                        if (counting) n_crc++;
                        i += flen;
                    }
                    continue;
                }
                if (mask_bytes == 0) { i++; continue; } // no desc yet
                // data frame: len = 3 + mask + 2*popcount(mask) + 1
                if (i + 3 + (size_t)mask_bytes > buf.size()) break;
                int n_ch = 0;
                for (int m = 0; m < mask_bytes; m++) {
                    uint8_t b = buf[i + 3 + m];
                    for (; b; n_ch++) b &= b - 1;
                }
                size_t flen = 3 + mask_bytes + 2 * n_ch + 1;
                if (i + flen > buf.size()) break;
                uint8_t crc = 0xFF;
                for (size_t k = i + 2; k < i + flen - 1; k++) crc ^= buf[k];
                if (crc == buf[i + flen - 1]) {
                    if (counting) {
                        n_ok++;
                        if (last_seq >= 0) {
                            int gap = (third - last_seq - 1) & 0xFF;
                            /* firmware never emits seq 0xFD (reserved) */
                            if (gap == 1 && ((last_seq + 1) & 0xFF) == 0xFD)
                                gap = 0;
                            n_gap += (size_t)gap;
                        }
                    }
                    last_seq = third;
                    i += flen;
                } else {
                    if (counting) n_crc++;
                    i += 1;
                }
            }
            buf.erase(buf.begin(), buf.begin() + (ptrdiff_t)i);
        };

        auto recv_chunk = [&](int timeout_ms) {
            fd_set rset;
            FD_ZERO(&rset); FD_SET(s, &rset);
            timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            if (select((int)(s + 1), &rset, nullptr, nullptr, &tv) > 0 &&
                FD_ISSET(s, &rset)) {
                uint8_t chunk[4096];
                int n = recv(s, (char*)chunk, sizeof(chunk), 0);
                if (n <= 0) return false;
                n_bytes += (size_t)n;
                buf.insert(buf.end(), chunk, chunk + n);
            }
            return true;
        };

        std::cout << "# stat " << duration << "s  (ok f/s | crc_err | seq_lost)\n";
        /* Warmup: the RTT up-buffer holds a stale backlog (device keeps
         * writing while no client is attached). Parse silently until the
         * first valid descriptor teaches us the mask length (2s cap). */
        auto warmup_end = std::chrono::steady_clock::now()
                        + std::chrono::seconds(2);
        while (mask_bytes == 0 && std::chrono::steady_clock::now() < warmup_end) {
            if (!recv_chunk(100)) goto done;
            parse(false);
        }
        last_seq = -1;
        buf.clear();

        {
            auto t_start = std::chrono::steady_clock::now();
            auto t_report = t_start;
            auto deadline = t_start + std::chrono::duration<double>(duration);

            while (std::chrono::steady_clock::now() < deadline) {
                if (!recv_chunk(100)) goto done;
                parse(true);

                auto now = std::chrono::steady_clock::now();
                if (now - t_report >= std::chrono::seconds(1)) {
                    double dt =
                        std::chrono::duration<double>(now - t_report).count();
                    t_report = now;
                    std::cout << "rate " << (double)n_ok / dt
                              << " f/s   crc_err " << n_crc
                              << "   seq_lost " << n_gap << "\n";
                    tot_ok += n_ok; tot_crc += n_crc; tot_gap += n_gap;
                    n_ok = n_crc = n_gap = 0;
                }
            }
        }

done:
        closesocket(s);
        std::cout << "summary: " << tot_ok << " frames (" 
                  << (double)n_bytes / 1024.0 << " KB total), crc_err "
                  << tot_crc << ", seq_lost " << tot_gap << "\n";
        return 0;
    }

    // capture <seconds> [--output <file>]
    if (sub == "capture") {
        if (argc < 3) { std::cerr << "Usage: aitrace wave capture <seconds>\n"; return 1; }
        double duration = std::stod(argv[2]);
        std::string outfile;
        for (int i = 3; i < argc; i++) {
            if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
                outfile = argv[++i];
            }
        }

        SOCKET s = ConnectTCP("127.0.0.1", 9091);
        if (s == INVALID_SOCKET) {
            std::cerr << "Failed to connect to RTT Ch1 (TCP 9091).\n";
            return 1;
        }

        ProtocolParser parser(16);
        bool header_written = false;

        std::ofstream file_out;
        if (!outfile.empty()) {
            file_out.open(outfile);
            if (!file_out.is_open()) {
                std::cerr << "Failed to open output file: " << outfile << "\n";
                closesocket(s);
                return 1;
            }
        }
        std::ostream& out = outfile.empty() ? std::cout : file_out;

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::duration<double>(duration);
        auto hard_deadline = deadline + std::chrono::seconds(2);

        while (std::chrono::steady_clock::now() < hard_deadline) {
            fd_set rset;
            FD_ZERO(&rset); FD_SET(s, &rset);
            timeval tv = {0, 100000}; // 100ms
            int ret = select((int)(s + 1), &rset, nullptr, nullptr, &tv);
            if (ret <= 0) {
                if (std::chrono::steady_clock::now() > deadline) break;
                continue;
            }

            uint8_t buf[4096];
            int bytes = recv(s, (char*)buf, sizeof(buf), 0);
            if (bytes <= 0) break;

            std::vector<uint8_t> raw(buf, buf + bytes);
            std::vector<DataSample> samples;
            parser.Feed(raw, samples);

            // Write CSV header from descriptor frame
            if (!header_written && !parser.GetChannels().empty()) {
                const auto& chs = parser.GetChannels();
                out << "time";
                for (size_t i = 0; i < chs.size(); i++) {
                    out << ",";
                    const std::string& name = chs[i].name;
                    size_t end = name.find_last_not_of(' ');
                    if (end != std::string::npos)
                        out.write(name.c_str(), end + 1);
                    else out << name;
                }
                out << "\n";
                header_written = true;
            }

            for (const auto& sample : samples) {
                out << sample.timestamp;
                const auto& chs = parser.GetChannels();
                for (size_t i = 0; i < chs.size(); i++) {
                    out << ",";
                    auto it = sample.ch_values.find((int)i);
                    if (it != sample.ch_values.end()) out << it->second;
                }
                out << "\n";
            }

            if (std::chrono::steady_clock::now() > deadline) break;
        }

        closesocket(s);
        return 0;
    }

    PrintUsage();
    return 1;
}
