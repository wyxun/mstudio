#ifndef OCD_CLIENT_H
#define OCD_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
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

struct RegEntry {
    std::string name;
    uint32_t    value;
    int         bits;  // register width (32 or 64)
};

class OcdClient {
public:
    OcdClient();
    ~OcdClient();

    void ConnectAsync(const std::string& host = "127.0.0.1", int port = 4444);
    void DisconnectAsync();
    
    bool IsConnected();
    bool IsConnecting();

    void HaltAsync();
    void ResumeAsync();
    void TriggerRefreshRegs();
    void TriggerReadMem32(uint32_t addr);

    bool FetchNewRegs(std::vector<RegEntry>& out_regs);
    bool GetCachedMemValue(uint32_t addr, uint32_t& out_val);

    // Synchronous convenience API for CLI tools (aitrace). Drives the
    // socket directly from the calling thread; do not mix with the async
    // task-queue API above on the same instance.
    bool ConnectSync(const char* host, int port);
    std::string SendCommandSync(const std::string& cmd);
    std::vector<RegEntry> GetRegsSync();
    uint32_t ReadMem32Sync(uint32_t addr);
    std::vector<uint32_t> ReadMemBlock32Sync(uint32_t addr, int count);

private:
    bool ConnectInternal(const char* host, int port);
    void DisconnectInternal();
    bool HaltInternal();
    bool ResumeInternal();
    std::vector<RegEntry> GetRegsInternal();
    uint32_t ReadMem32Internal(uint32_t addr);

    std::string SendCommand(const std::string& cmd);

    std::thread worker_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::function<void()>> task_queue_;
    std::atomic<bool> running_{true};

    enum class State {
        Disconnected,
        Connecting,
        Connected
    };
    std::atomic<State> state_{State::Disconnected};

    std::mutex regs_mutex_;
    std::vector<RegEntry> cached_regs_;
    bool regs_dirty_ = false;

    std::mutex mem_mutex_;
    std::map<uint32_t, uint32_t> cached_mem_;

    SOCKET sock_ = INVALID_SOCKET;
    std::string host_ = "127.0.0.1";
    int port_ = 4444;

    void WorkerLoop();
    void PushTask(std::function<void()> task);

    std::string StripTelnet(const std::string& data);
    std::string RecvUntilTimeout(int timeout_ms = 300);
    void DrainPending();
};

#endif // OCD_CLIENT_H

