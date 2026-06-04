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

OcdClient::OcdClient() {
    worker_thread_ = std::thread(&OcdClient::WorkerLoop, this);
}

OcdClient::~OcdClient() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
    }
    queue_cv_.notify_all();
    
    // 主线程关闭套接字强制唤醒可能处于阻塞状态的后台 select/recv
    DisconnectInternal();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}


bool OcdClient::ConnectInternal(const char* host, int port) {
    EnsureWSA();
    DisconnectInternal();

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

    // Consume telnet negotiation + initial banner until prompt or timeout
    char buf[128];
    auto consume_deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < consume_deadline) {
        timeval tv0 = {0, 100000}; // 100ms
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(sock_, &rs);
        int ret = select((int)(sock_ + 1), &rs, NULL, NULL, &tv0);
        if (ret <= 0) break; // No more data
        int n = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        // Stop consuming once we see the prompt
        std::string chunk(buf, n);
        if (chunk.find("> ") != std::string::npos) break;
    }

    // Put socket back in blocking mode
#ifdef _WIN32
    u_long block = 0;
    ioctlsocket(sock_, FIONBIO, &block);
#else
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags & ~O_NONBLOCK);
#endif

    return true;
}

void OcdClient::DisconnectInternal() {
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
            i += 2; // Skip IAC + command + option
            continue;
        }
        if ((unsigned char)data[i] == 0x00) continue; // Skip NUL bytes
        out += data[i];
    }
    return out;
}

std::string OcdClient::RecvUntilTimeout(int timeout_ms) {
    std::string result;
    char buf[4096];
    int idle_loops = 0;
    const int max_idle = timeout_ms / 100; // timeout_ms / poll_interval

    while (idle_loops < max_idle) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock_, &read_set);
        timeval tv = {0, 100000}; // 100ms poll

        int ret = select((int)(sock_ + 1), &read_set, NULL, NULL, &tv);
        if (ret <= 0) { idle_loops++; continue; }

        int bytes = recv(sock_, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) { idle_loops++; continue; }

        result.append(buf, bytes);
        idle_loops = 0; // Reset on data
        // Stop when we see the OpenOCD prompt
        if (result.find("\n> ") != std::string::npos ||
            result.find("\r> ") != std::string::npos) break;
    }

    return StripTelnet(result);
}

std::string OcdClient::SendCommand(const std::string& cmd) {
    if (sock_ == INVALID_SOCKET) return "";

    // Drain any leftover data from previous response
    DrainPending();

    std::string line = cmd + "\r\n";
    int sent = send(sock_, line.c_str(), (int)line.size(), 0);
    if (sent < 0) {
        DisconnectInternal();
        return "";
    }

    return RecvUntilTimeout(2000);
}

void OcdClient::DrainPending() {
    char buf[256];
    timeval tv = {0, 50000}; // 50ms
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(sock_, &rs);
    while (select((int)(sock_ + 1), &rs, NULL, NULL, &tv) > 0) {
        recv(sock_, buf, sizeof(buf), 0);
        tv = {0, 50000};
        FD_ZERO(&rs);
        FD_SET(sock_, &rs);
    }
}

bool OcdClient::HaltInternal() {
    auto r = SendCommand("halt");
    return !r.empty() && r.find("timed out") == std::string::npos;
}

bool OcdClient::ResumeInternal() {
    auto r = SendCommand("resume");
    return !r.empty();
}

std::vector<RegEntry> OcdClient::GetRegsInternal() {
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

uint32_t OcdClient::ReadMem32Internal(uint32_t addr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "mrw 0x%08X", addr);
    auto response = SendCommand(buf);

    // mrw response is just "0xVALUE" (no colon)
    auto pos = response.rfind("0x");
    if (pos != std::string::npos) {
        try { return std::stoul(response.substr(pos), nullptr, 16); }
        catch (...) {}
    }
    return 0;
}

void OcdClient::PushTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(task);
    }
    queue_cv_.notify_one();
}

void OcdClient::WorkerLoop() {
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { return !task_queue_.empty() || !running_; });
            if (!running_) break;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        if (task) {
            task();
        }
    }
}

void OcdClient::ConnectAsync(const std::string& host, int port) {
    if (state_ != State::Disconnected) return;
    state_ = State::Connecting;
    host_ = host;
    port_ = port;
    PushTask([this]() {
        bool ok = ConnectInternal(host_.c_str(), port_);
        state_ = ok ? State::Connected : State::Disconnected;
    });
}

void OcdClient::DisconnectAsync() {
    PushTask([this]() {
        DisconnectInternal();
        state_ = State::Disconnected;
    });
}

bool OcdClient::IsConnected() {
    return state_ == State::Connected;
}

bool OcdClient::IsConnecting() {
    return state_ == State::Connecting;
}

void OcdClient::HaltAsync() {
    PushTask([this]() {
        HaltInternal();
    });
}

void OcdClient::ResumeAsync() {
    PushTask([this]() {
        ResumeInternal();
    });
}

void OcdClient::TriggerRefreshRegs() {
    PushTask([this]() {
        auto regs = GetRegsInternal();
        std::lock_guard<std::mutex> lock(regs_mutex_);
        cached_regs_ = std::move(regs);
        regs_dirty_ = true;
    });
}

void OcdClient::TriggerReadMem32(uint32_t addr) {
    PushTask([this, addr]() {
        uint32_t val = ReadMem32Internal(addr);
        std::lock_guard<std::mutex> lock(mem_mutex_);
        cached_mem_[addr] = val;
    });
}

bool OcdClient::FetchNewRegs(std::vector<RegEntry>& out_regs) {
    std::lock_guard<std::mutex> lock(regs_mutex_);
    if (!regs_dirty_) return false;
    out_regs = cached_regs_;
    regs_dirty_ = false;
    return true;
}

bool OcdClient::GetCachedMemValue(uint32_t addr, uint32_t& out_val) {
    std::lock_guard<std::mutex> lock(mem_mutex_);
    auto it = cached_mem_.find(addr);
    if (it == cached_mem_.end()) return false;
    out_val = it->second;
    return true;
}



