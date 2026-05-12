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
              << "  capture <s> --output <f> Capture CSV to file\n";
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
