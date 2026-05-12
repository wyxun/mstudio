#ifndef OCD_CLIENT_H
#define OCD_CLIENT_H

#include <string>
#include <vector>
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
    OcdClient() = default;
    ~OcdClient() { Disconnect(); }

    bool Connect(const char* host = "127.0.0.1", int port = 4444);
    void Disconnect();
    bool IsConnected() const { return sock_ != INVALID_SOCKET; }

    std::string SendCommand(const std::string& cmd);

    // Convenience
    bool Halt();
    bool Resume();
    bool ResetHalt();
    std::vector<RegEntry> GetRegs();
    uint32_t ReadMem32(uint32_t addr);
    std::vector<uint32_t> ReadMemBlock32(uint32_t addr, int count);

private:
    SOCKET sock_ = INVALID_SOCKET;
    std::string StripTelnet(const std::string& data);
    std::string RecvUntilTimeout(int timeout_ms = 300);
    void DrainPending();
};

#endif // OCD_CLIENT_H
