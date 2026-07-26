#include "shell_cmd.h"
#include <iostream>
#include <sstream>
#include <string>
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
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

static bool EnsureWSA() {
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

static SOCKET ConnectTCP(const char* host, int port) {
    EnsureWSA();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    fcntl(s, F_SETFL, O_NONBLOCK);
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

    if (!FD_ISSET(s, &wset)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* Firmware log flooding (e.g. periodic "[T] [Heartbeat] ..." lines) mixes
 * with shell responses on RTT Ch0. Two defences:
 *  - stop as soon as the mshell prompt "> " shows up after the response,
 *    with a hard cap so log spam can never keep the command alive forever;
 *  - by default drop trace-level "[T] " lines (log noise). Command
 *    responses are emitted at [I]/[W]/[E] level and are kept.
 * --raw disables filtering and prints the stream verbatim. */
static bool IsTraceLogLine(const std::string& line) {
    return line.rfind("[T] ", 0) == 0;
}

static void PrintFiltered(const std::string& acc, bool raw) {
    if (raw) {
        std::cout << acc;
        return;
    }
    std::istringstream ss(acc);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (IsTraceLogLine(line)) continue;
        /* bare prompt line: marks completion, not part of the response */
        if (line == "> ") continue;
        std::cout << line << "\n";
    }
}

int shell_main(int argc, char* argv[]) {
    bool raw = false;
    std::string cmd;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--raw") { raw = true; continue; }
        if (!cmd.empty()) cmd += " ";
        cmd += a;
    }

    if (cmd.empty()) {
        std::cerr << "Usage: aitrace shell [--raw] <cmd...>\n"
                  << "  --raw   do not filter firmware log lines ([T] ...)\n";
        return 1;
    }
    cmd += "\r\n";

    SOCKET s = ConnectTCP("127.0.0.1", 9090);
    if (s == INVALID_SOCKET) {
        std::cerr << "Failed to connect to RTT Ch0 (TCP 9090). Is OpenOCD running?\n";
        return 1;
    }

    /* Drain the RTT backlog (boot banner, buffered log spam) before sending,
     * otherwise a stale prompt in the backlog terminates the wait early. */
    {
        char drain_buf[4096];
        auto drain_end = std::chrono::steady_clock::now()
                       + std::chrono::milliseconds(400);
        while (std::chrono::steady_clock::now() < drain_end) {
            fd_set rset;
            FD_ZERO(&rset); FD_SET(s, &rset);
            timeval tv = {0, 50000};
            if (select((int)(s + 1), &rset, nullptr, nullptr, &tv) > 0 &&
                FD_ISSET(s, &rset)) {
                if (recv(s, drain_buf, sizeof(drain_buf), 0) <= 0) break;
            }
        }
    }

    if (send(s, cmd.c_str(), (int)cmd.size(), 0) < 0) {
        std::cerr << "Failed to send command.\n";
        closesocket(s);
        return 1;
    }

    std::string acc;
    bool got_data = false;
    char buf[4096];
    auto hard_deadline = std::chrono::steady_clock::now()
                       + std::chrono::milliseconds(5000);
    auto idle_deadline = hard_deadline;

    while (std::chrono::steady_clock::now() < hard_deadline) {
        fd_set rset;
        FD_ZERO(&rset); FD_SET(s, &rset);
        timeval tv = {0, 50000}; // 50ms poll
        int ret = select((int)(s + 1), &rset, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(s, &rset)) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n > 0) {
                acc.append(buf, (size_t)n);
                got_data = true;
                /* 600ms idle after the last chunk ends the command */
                idle_deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds(600);
                /* mshell reprints the prompt "> " after each response */
                if (acc.find("\n> ") != std::string::npos) break;
            } else if (n == 0 || (n < 0
#ifdef _WIN32
                && WSAGetLastError() != WSAEWOULDBLOCK
#else
                && errno != EAGAIN && errno != EWOULDBLOCK
#endif
            )) {
                break; // Connection closed or error
            }
        }
        if (got_data && std::chrono::steady_clock::now() > idle_deadline) break;
    }

    if (!got_data) {
        std::cout << "(no response from firmware)\n";
    } else {
        PrintFiltered(acc, raw);
    }
    closesocket(s);
    return 0;
}
