#include "ocd_cmd.h"
#include "ocd_client.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdlib>

static void PrintUsage() {
    std::cerr << "Usage: aitrace ocd <subcommand> [args]\n"
              << "  halt                   Halt CPU (intrusive!)\n"
              << "  resume                 Resume CPU\n"
              << "  regs                   Dump all core registers\n"
              << "  peek  <hex_addr>       Read uint32 at address\n"
              << "  mdw   <hex_addr> [n]   Dump n words of memory\n"
              << "  stack [depth]          Dump stack around SP\n";
}

static uint32_t ParseHex(const char* s) {
    return (uint32_t)std::strtoul(s, nullptr, 16);
}

int ocd_main(int argc, char* argv[]) {
    if (argc < 2) { PrintUsage(); return 1; }

    std::string sub = argv[1];

    OcdClient ocd;
    if (!ocd.Connect("127.0.0.1", 4444)) {
        std::cerr << "Failed to connect to OpenOCD (TCP 4444). Is OpenOCD running?\n";
        return 1;
    }

    if (sub == "halt") {
        bool ok = ocd.Halt();
        std::cout << (ok ? "CPU halted.\n" : "Halt failed.\n");
    } else if (sub == "resume") {
        bool ok = ocd.Resume();
        std::cout << (ok ? "CPU resumed.\n" : "Resume failed.\n");
    } else if (sub == "regs") {
        auto regs = ocd.GetRegs();
        for (const auto& r : regs) {
            std::cout << std::setw(8) << r.name << " : 0x"
                      << std::hex << std::setw(8) << std::setfill('0')
                      << r.value << std::dec << std::setfill(' ') << "\n";
        }
    } else if (sub == "peek") {
        if (argc < 3) { std::cerr << "Usage: aitrace ocd peek <hex_addr>\n"; return 1; }
        uint32_t addr = ParseHex(argv[2]);
        uint32_t val = ocd.ReadMem32(addr);
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                  << val << std::dec << std::setfill(' ') << "\n";
    } else if (sub == "mdw") {
        if (argc < 3) { std::cerr << "Usage: aitrace ocd mdw <hex_addr> [count]\n"; return 1; }
        uint32_t addr = ParseHex(argv[2]);
        int count = (argc > 3) ? std::stoi(argv[3]) : 16;
        auto vals = ocd.ReadMemBlock32(addr, count);
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
    } else if (sub == "stack") {
        int depth = (argc > 2) ? std::stoi(argv[2]) : 32;
        auto regs = ocd.GetRegs();
        uint32_t sp = 0;
        for (const auto& r : regs) {
            if (r.name == "sp" || r.name == "msp") { sp = r.value; break; }
        }
        if (sp == 0) {
            std::cerr << "Could not read SP.\n";
            return 1;
        }
        sp &= ~0x3u;
        uint32_t start = sp - 32;
        auto vals = ocd.ReadMemBlock32(start, depth);
        for (size_t i = 0; i < vals.size(); i++) {
            uint32_t addr = start + (uint32_t)(i * 4);
            std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                      << addr << ": 0x" << std::setw(8) << vals[i];
            if (addr == sp) std::cout << " <-- SP";
            std::cout << "\n";
        }
        std::cout << std::dec << std::setfill(' ') << "\n";
    } else {
        PrintUsage();
        return 1;
    }

    return 0;
}
