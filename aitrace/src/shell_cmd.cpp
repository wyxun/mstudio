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

    if (send(s, cmd.c_str(), (int)cmd.size(), 0) < 0) {
        std::cerr << "Failed to send command.\n";
        closesocket(s);
        return 1;
    }

    // Poll for response like network_mgr does: continuous select + recv
    // with 50ms idle timeout between data chunks
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(5000);
    bool got_data = false;
    char buf[4096];

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rset;
        FD_ZERO(&rset); FD_SET(s, &rset);
        timeval tv = {0, 50000}; // 50ms poll
        int ret = select((int)(s + 1), &rset, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(s, &rset)) {
            int n = recv(s, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = 0;
                std::cout << buf;
                got_data = true;
                // Reset deadline after receiving data — wait up to 500ms more
                deadline = std::chrono::steady_clock::now()
                         + std::chrono::milliseconds(500);
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
        if (got_data && std::chrono::steady_clock::now() > deadline) break;
    }

    if (!got_data) std::cout << "(no response from firmware)\n";
    closesocket(s);
    return 0;
}
