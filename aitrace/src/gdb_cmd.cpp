#include "gdb_cmd.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#define STATE_FILE "C:/Windows/Temp/aitrace_gdb_state.txt"
#else
#include <unistd.h>
#define STATE_FILE "/tmp/aitrace_gdb_state.txt"
#endif

static void PrintUsage() {
    std::cerr << "Usage: aitrace gdb <subcommand> [args]\n"
              << "  connect [--port 3333] --elf <path>\n"
              << "  break    <location>\n"
              << "  continue\n"
              << "  step\n"
              << "  print    <expression>\n"
              << "  bt\n"
              << "  detach\n";
}

static int g_gdb_port = 3333;

// Save/load session state to a temp file so commands share context
static void SaveState(const std::string& elf) {
    std::ofstream f(STATE_FILE);
    f << elf << "\n" << g_gdb_port << "\n";
}

static std::string LoadState() {
    std::ifstream f(STATE_FILE);
    std::string elf;
    if (std::getline(f, elf)) {
        std::string port_str;
        if (std::getline(f, port_str)) g_gdb_port = std::stoi(port_str);
    }
    return elf;
}

static void ClearState() {
    std::remove(STATE_FILE);
}

static bool DetectOpenOCD() {
#ifdef _WIN32
    WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
#endif
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(s, FIONBIO, &mode);
#endif
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4444);
#ifdef _WIN32
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
#else
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
    connect(s, (sockaddr*)&addr, sizeof(addr));
    fd_set wset, eset;
    FD_ZERO(&wset); FD_SET(s, &wset);
    FD_ZERO(&eset); FD_SET(s, &eset);
    timeval tv = {0, 500000};
    select((int)(s + 1), nullptr, &wset, &eset, &tv);
    bool ok = FD_ISSET(s, &wset);
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return ok;
}

// Write commands to a temp file and execute with gdb-multiarch -batch
static std::string RunGdbBatch(const std::string& elf,
                                const std::string& commands) {
    std::ostringstream full_cmd;
    full_cmd << "target extended-remote localhost:" << g_gdb_port << "\n"
             << commands << "\n"
             << "disconnect\n"
             << "quit\n";

#ifdef _WIN32
    char tmpbuf[MAX_PATH];
    GetTempPathA(sizeof(tmpbuf), tmpbuf);
    std::string tmpfile = std::string(tmpbuf) + "aitrace_gdb_cmds.txt";
#else
    std::string tmpfile = "/tmp/aitrace_gdb_cmds.txt";
#endif

    {
        std::ofstream f(tmpfile);
        f << full_cmd.str();
    }

    std::ostringstream run;
    run << "gdb-multiarch -batch -x \"" << tmpfile << "\" \""
        << elf << "\" 2>&1";

    std::string result;
    FILE* pipe = popen(run.str().c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to start GDB.\n";
        return "";
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    std::remove(tmpfile.c_str());
    return result;
}

int gdb_main(int argc, char* argv[]) {
    if (argc < 2) { PrintUsage(); return 1; }

    std::string sub = argv[1];

    if (sub == "connect") {
        std::string elf_path;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg.find("--port=") == 0) g_gdb_port = std::stoi(arg.substr(7));
            else if (arg.find("--elf=") == 0) elf_path = arg.substr(6);
        }
        if (elf_path.empty()) {
            std::cerr << "Error: --elf=<path> is required.\n";
            return 1;
        }
        if (!DetectOpenOCD()) {
            std::cerr << "Error: OpenOCD not detected on TCP 4444.\n"
                      << "Please start OpenOCD first (e.g. 'make.bat rtt').\n";
            return 1;
        }
        // Test connection
        auto result = RunGdbBatch(elf_path, "");
        if (result.find("error") != std::string::npos
            || result.find("Error") != std::string::npos) {
            std::cerr << "GDB connection test failed:\n" << result << "\n";
            return 1;
        }
        SaveState(elf_path);
        std::cout << "GDB connected. ELF: " << elf_path << "\n";
        std::cout << "Ready: break, continue, step, print, bt, detach\n";
        return 0;
    }

    // All other commands need a prior connect
    std::string elf = LoadState();
    if (elf.empty()) {
        std::cerr << "Not connected. Run 'aitrace gdb connect --elf <path>' first.\n";
        return 1;
    }

    if (sub == "break") {
        if (argc < 3) { std::cerr << "Usage: aitrace gdb break <location>\n"; return 1; }
        std::cout << RunGdbBatch(elf, "break " + std::string(argv[2]));
    } else if (sub == "continue") {
        std::cout << RunGdbBatch(elf, "continue");
    } else if (sub == "step") {
        std::cout << RunGdbBatch(elf, "step");
    } else if (sub == "print") {
        if (argc < 3) { std::cerr << "Usage: aitrace gdb print <expr>\n"; return 1; }
        std::cout << RunGdbBatch(elf, "print " + std::string(argv[2]));
    } else if (sub == "bt") {
        std::cout << RunGdbBatch(elf, "bt");
    } else if (sub == "detach") {
        ClearState();
        std::cout << "Disconnected.\n";
    } else {
        PrintUsage();
        return 1;
    }

    return 0;
}
