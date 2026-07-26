#include "ocd_cmd.h"
#include "ocd_client.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <thread>
#include <chrono>

static void PrintUsage() {
    std::cerr << "Usage: aitrace ocd <subcommand> [args]\n"
              << "  halt                   Halt CPU (intrusive!)\n"
              << "  resume                 Resume CPU\n"
              << "  regs                   Dump all core registers (auto-halts)\n"
              << "  peek  <hex_addr>       Read uint32 at address (auto-halts)\n"
              << "  mdw   <hex_addr> [n]   Dump n words of memory (auto-halts)\n"
              << "  stack [depth]          Dump stack around SP (auto-halts)\n";
}

static uint32_t ParseHex(const char* s) {
    return (uint32_t)std::strtoul(s, nullptr, 16);
}

static void WithHalt(OcdClient& ocd, bool auto_resume, std::function<void()> fn) {
    ocd.SendCommandSync("halt");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fn();
    if (auto_resume) ocd.SendCommandSync("resume");
}

int ocd_main(int argc, char* argv[]) {
    if (argc < 2) { PrintUsage(); return 1; }

    std::string sub = argv[1];

    OcdClient ocd;
    if (!ocd.ConnectSync("127.0.0.1", 4444)) {
        std::cerr << "Failed to connect to OpenOCD (TCP 4444). Is OpenOCD running?\n";
        return 1;
    }

    if (sub == "halt") {
        auto r = ocd.SendCommandSync("halt");
        if (r.empty() || r.find("timed out") != std::string::npos)
            std::cout << "Halt failed.\n";
        else
            std::cout << "CPU halted.\n";
    } else if (sub == "resume") {
        auto r = ocd.SendCommandSync("resume");
        if (r.empty()) std::cout << "Resume failed.\n";
        else std::cout << "CPU resumed.\n";
    } else if (sub == "regs") {
        WithHalt(ocd, true, [&]() {
            auto regs = ocd.GetRegsSync();
            if (regs.empty()) {
                std::cout << "(could not read registers)\n";
                return;
            }
            for (const auto& r : regs) {
                std::cout << std::setw(8) << r.name << " : 0x"
                          << std::hex << std::setw(8) << std::setfill('0')
                          << r.value << std::dec << std::setfill(' ') << "\n";
            }
        });
    } else if (sub == "peek") {
        if (argc < 3) { std::cerr << "Usage: aitrace ocd peek <hex_addr>\n"; return 1; }
        uint32_t addr = ParseHex(argv[2]);
        WithHalt(ocd, true, [&]() {
            uint32_t val = ocd.ReadMem32Sync(addr);
            std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                      << val << std::dec << std::setfill(' ') << "\n";
        });
    } else if (sub == "mdw") {
        if (argc < 3) { std::cerr << "Usage: aitrace ocd mdw <hex_addr> [count]\n"; return 1; }
        uint32_t addr = ParseHex(argv[2]);
        int count = (argc > 3) ? std::stoi(argv[3]) : 16;
        WithHalt(ocd, true, [&]() {
            auto vals = ocd.ReadMemBlock32Sync(addr, count);
            for (size_t i = 0; i < vals.size(); i++) {
                if (i % 4 == 0) {
                    if (i > 0) std::cout << "\n";
                    std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                              << (addr + (uint32_t)(i * 4)) << ": ";
                }
                std::cout << std::hex << std::setw(8) << std::setfill('0')
                          << vals[i] << " ";
            }
            std::cout << std::dec << std::setfill(' ') << "\n";
        });
    } else if (sub == "stack") {
        int depth = (argc > 2) ? std::stoi(argv[2]) : 32;
        WithHalt(ocd, true, [&]() {
            auto regs = ocd.GetRegsSync();
            uint32_t sp = 0;
            for (const auto& r : regs) {
                if (r.name == "sp" || r.name == "msp") { sp = r.value; break; }
            }
            if (sp == 0) {
                std::cout << "(could not read SP)\n";
                return;
            }
            sp &= ~0x3u;
            uint32_t start = sp - 32;
            auto vals = ocd.ReadMemBlock32Sync(start, depth);
            for (size_t i = 0; i < vals.size(); i++) {
                uint32_t addr = start + (uint32_t)(i * 4);
                std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                          << addr << ": 0x" << std::setw(8) << vals[i];
                if (addr == sp) std::cout << " <-- SP";
                std::cout << "\n";
            }
            std::cout << std::dec << std::setfill(' ') << "\n";
        });
    } else if (sub == "raw") {
        if (argc < 3) { std::cerr << "Usage: aitrace ocd raw <cmd> [args...]\n"; return 1; }
        std::string full;
        for (int i = 2; i < argc; i++) {
            if (i > 2) full += " ";
            full += argv[i];
        }
        std::cout << ocd.SendCommandSync(full) << "\n";
    } else {
        PrintUsage();
        return 1;
    }

    return 0;
}
