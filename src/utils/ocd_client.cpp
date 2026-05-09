#include "ocd_client.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static bool s_wsa_init = false;
static void EnsureWSA() {
    if (!s_wsa_init) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        s_wsa_init = true;
    }
}
#else
#include <sys/select.h>
static void EnsureWSA() {}
#endif

bool OcdClient::Connect(const char* host, int port) {
    EnsureWSA();
    Disconnect();

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;

    // Non-blocking connect with timeout (same pattern as NetworkMgr)
    u_long mode = 1;
#ifdef _WIN32
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    fcntl(sock_, F_SETFL, O_NONBLOCK);
#endif

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    inet_pton(AF_INET, host, &addr.sin_addr);
#else
    addr.sin_addr.s_addr = inet_addr(host);
#endif

    connect(sock_, (sockaddr*)&addr, sizeof(addr));

    fd_set write_set, err_set;
    FD_ZERO(&write_set);
    FD_SET(sock_, &write_set);
    err_set = write_set;

    timeval tv = {0, 500000}; // 500ms timeout
    int ret = select((int)(sock_ + 1), NULL, &write_set, &err_set, &tv);

    if (ret <= 0 || !FD_ISSET(sock_, &write_set)) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    // Consume initial telnet negotiation (non-blocking, best-effort)
    char buf[128];
    timeval tv0 = {0, 0};
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(sock_, &rs);
    if (select((int)(sock_ + 1), &rs, NULL, NULL, &tv0) > 0) {
        recv(sock_, buf, sizeof(buf), 0);
    }

    return true;
}

void OcdClient::Disconnect() {
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
}

std::string OcdClient::StripTelnet(const std::string& data) {
    std::string out;
    out.reserve(data.size());
    for (size_t i = 0; i < data.size(); i++) {
        if ((unsigned char)data[i] == 0xFF && i + 2 < data.size()) {
            i += 2;
            continue;
        }
        out += data[i];
    }
    return out;
}

std::string OcdClient::RecvUntilTimeout(int timeout_ms) {
    std::string result;
    char buf[4096];
    int max_iter = 50; // safety limit

    while (max_iter-- > 0) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock_, &read_set);
        timeval tv = {0, timeout_ms * 1000};

        int ret = select((int)(sock_ + 1), &read_set, NULL, NULL, &tv);
        if (ret <= 0) break;

        int bytes = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) break;

        result.append(buf, bytes);
    }

    return StripTelnet(result);
}

std::string OcdClient::SendCommand(const std::string& cmd) {
    if (sock_ == INVALID_SOCKET) return "";

    std::string line = cmd + "\n";
    int sent = send(sock_, line.c_str(), (int)line.size(), 0);
    if (sent < 0) {
        Disconnect();
        return "";
    }

    return RecvUntilTimeout(200);
}

bool OcdClient::Halt() {
    auto r = SendCommand("halt");
    return !r.empty() && r.find("timed out") == std::string::npos;
}

bool OcdClient::Resume() {
    auto r = SendCommand("resume");
    return !r.empty();
}

bool OcdClient::ResetHalt() {
    auto r = SendCommand("reset halt");
    return !r.empty();
}

std::vector<RegEntry> OcdClient::GetRegs() {
    std::vector<RegEntry> regs;
    auto response = SendCommand("reg");
    if (response.empty()) return regs;

    std::istringstream ss(response);
    std::string line;
    while (std::getline(ss, line)) {
        auto paren = line.find('(');
        if (paren == std::string::npos) continue;

        auto paren_end = line.find(')', paren);
        if (paren_end == std::string::npos) continue;

        auto name_start = line.find_first_not_of(" \t", paren_end + 1);
        if (name_start == std::string::npos) continue;

        auto slash = line.find("(/", name_start);
        if (slash == std::string::npos) continue;

        auto slash_end = line.find(')', slash);
        if (slash_end == std::string::npos) continue;

        auto colon = line.find(':', slash_end);
        if (colon == std::string::npos) continue;

        auto val_start = line.find("0x", colon);
        if (val_start == std::string::npos) continue;

        RegEntry e;
        e.name = line.substr(name_start, slash - name_start);
        while (!e.name.empty() && e.name.back() == ' ') e.name.pop_back();
        try {
            e.bits = std::stoi(line.substr(slash + 2, slash_end - slash - 2));
            e.value = std::stoul(line.substr(val_start), nullptr, 16);
        } catch (...) {
            continue;
        }
        regs.push_back(e);
    }
    return regs;
}

uint32_t OcdClient::ReadMem32(uint32_t addr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "mrw 0x%08X", addr);
    auto response = SendCommand(buf);

    auto pos = response.find("0x");
    if (pos != std::string::npos) {
        return std::stoul(response.substr(pos), nullptr, 16);
    }
    return 0;
}

std::vector<uint32_t> OcdClient::ReadMemBlock32(uint32_t addr, int count) {
    std::vector<uint32_t> result;
    char buf[64];
    snprintf(buf, sizeof(buf), "mdw 0x%08X %d", addr, count);
    auto response = SendCommand(buf);

    std::istringstream ss(response);
    std::string line;
    while (std::getline(ss, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::istringstream vs(line.substr(colon + 1));
        std::string word;
        while (vs >> word) {
            result.push_back(std::stoul(word, nullptr, 16));
        }
    }
    return result;
}
