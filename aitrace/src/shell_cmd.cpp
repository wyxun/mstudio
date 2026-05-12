#include "shell_cmd.h"
#include <iostream>
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
    timeval tv = {1, 0}; // 1s timeout
    select((int)(s + 1), nullptr, &wset, &eset, &tv);

    if (!FD_ISSET(s, &wset)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static std::string RecvAll(SOCKET s, int timeout_ms) {
    std::string result;
    char buf[4096];
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rset;
        FD_ZERO(&rset); FD_SET(s, &rset);
        timeval tv = {0, 100000}; // 100ms
        int ret = select((int)(s + 1), &rset, nullptr, nullptr, &tv);
        if (ret <= 0) {
            if (!result.empty()) break; // Got some data, no more coming
            continue;
        }
        int bytes = recv(s, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) break;
        result.append(buf, bytes);
    }
    return result;
}

int shell_main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: aitrace shell <cmd...>\n";
        return 1;
    }

    // Reconstruct command string from args
    std::string cmd;
    for (int i = 1; i < argc; i++) {
        if (i > 1) cmd += " ";
        cmd += argv[i];
    }
    cmd += "\r\n";

    SOCKET s = ConnectTCP("127.0.0.1", 9090);
    if (s == INVALID_SOCKET) {
        std::cerr << "Failed to connect to RTT Ch0 (TCP 9090). Is OpenOCD running?\n";
        return 1;
    }

    int sent = send(s, cmd.c_str(), (int)cmd.size(), 0);
    if (sent < 0) {
        std::cerr << "Failed to send command.\n";
        closesocket(s);
        return 1;
    }

    // Read response with 2s total timeout
    std::string response = RecvAll(s, 2000);
    std::cout << response;
    if (!response.empty() && response.back() != '\n') std::cout << "\n";

    closesocket(s);
    return 0;
}
