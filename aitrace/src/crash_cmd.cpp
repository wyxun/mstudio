#include "crash_cmd.h"
#include "elf_parser.h"
#include "map_parser.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>

static uint32_t ParseHex(const char* s) {
    return (uint32_t)std::strtoul(s, nullptr, 16);
}

static void DecodeCFSR(uint32_t cfsr) {
    std::cout << "\nCFSR: 0x" << std::hex << std::setw(8) << std::setfill('0')
              << cfsr << std::dec << std::setfill(' ') << "\n";
    std::cout << "Fault type decode:\n";

    uint8_t ufsr = (cfsr >> 16) & 0xFF;
    if (ufsr) {
        if (ufsr & (1 << 0)) std::cout << "  - Undefined instruction\n";
        if (ufsr & (1 << 1)) std::cout << "  - Invalid state\n";
        if (ufsr & (1 << 2)) std::cout << "  - Invalid PC load\n";
        if (ufsr & (1 << 3)) std::cout << "  - No coprocessor\n";
        if (ufsr & (1 << 5)) std::cout << "  - Divide by zero\n";
        if (ufsr & (1 << 6)) std::cout << "  - Unaligned access\n";
    }

    uint8_t bfsr = (cfsr >> 8) & 0xFF;
    if (bfsr) {
        if (bfsr & (1 << 0)) std::cout << "  - Instruction bus error\n";
        if (bfsr & (1 << 1)) std::cout << "  - Precise data bus error\n";
        if (bfsr & (1 << 2)) std::cout << "  - Imprecise data bus error\n";
        if (bfsr & (1 << 3)) std::cout << "  - Unstack bus error\n";
        if (bfsr & (1 << 4)) std::cout << "  - Stack bus error\n";
        if (bfsr & (1 << 7)) std::cout << "  - BFAR valid\n";
    }

    uint8_t mmsr = cfsr & 0xFF;
    if (mmsr) {
        if (mmsr & (1 << 0)) std::cout << "  - Instruction access violation (MPU)\n";
        if (mmsr & (1 << 1)) std::cout << "  - Data access violation (MPU)\n";
        if (mmsr & (1 << 3)) std::cout << "  - Unstack MPU violation\n";
        if (mmsr & (1 << 4)) std::cout << "  - Stack MPU violation\n";
        if (mmsr & (1 << 7)) std::cout << "  - MMAR valid\n";
    }
}

static std::string ResolveAddrElf(ElfParser& elf, uint32_t addr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << addr;
    const auto& syms = elf.GetSymbols();
    const ElfSymbol* best = nullptr;
    for (const auto& s : syms) {
        if (s.address <= addr && (!best || s.address > best->address)) best = &s;
    }
    if (best && (addr - best->address) < 0x8000) {
        uint32_t off = addr - best->address;
        oss << std::dec << std::setfill(' ') << "  ->  " << best->name;
        if (off > 0) oss << " + 0x" << std::hex << off;
        oss << ((best->type == 2) ? " (function)" : "");
    } else {
        oss << "  ->  <unknown>";
    }
    return oss.str();
}

static void PrintUsage() {
    std::cerr << "Usage: aitrace crash report --pc=<hex> --lr=<hex> [--sp=<hex>]\n"
              << "                        --elf=<path> [--cfsr=<hex>]\n"
              << "                        [--stack=<h1,h2,...>]\n";
}

int crash_main(int argc, char* argv[]) {
    if (argc < 2) { PrintUsage(); return 1; }

    std::string sub = argv[1];
    if (sub != "report") { PrintUsage(); return 1; }

    uint32_t pc = 0, lr = 0, sp = 0, cfsr = 0;
    std::string elf_path;
    std::vector<uint32_t> stack_vals;
    bool has_pc = false, has_lr = false, has_cfsr = false;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--pc=") == 0)      { pc = ParseHex(arg.c_str() + 5); has_pc = true; }
        else if (arg.find("--lr=") == 0) { lr = ParseHex(arg.c_str() + 5); has_lr = true; }
        else if (arg.find("--sp=") == 0) { sp = ParseHex(arg.c_str() + 5); }
        else if (arg.find("--cfsr=") == 0) { cfsr = ParseHex(arg.c_str() + 7); has_cfsr = true; }
        else if (arg.find("--elf=") == 0) { elf_path = arg.substr(6); }
        else if (arg.find("--stack=") == 0) {
            std::string vals = arg.substr(8);
            std::istringstream ss(vals);
            std::string token;
            while (std::getline(ss, token, ','))
                stack_vals.push_back(ParseHex(token.c_str()));
        }
    }

    if (!has_pc || !has_lr || elf_path.empty()) {
        std::cerr << "Error: --pc, --lr, and --elf are required.\n";
        PrintUsage();
        return 1;
    }

    ElfParser elf;
    if (!elf.Load(elf_path)) {
        MapParser map;
        if (!map.Load(elf_path)) {
            std::cerr << "Failed to load " << elf_path << " as ELF or MAP file.\n";
            return 1;
        }
        // Resolve via map symbols (limited)
        std::cout << "===== CRASH ANALYSIS REPORT =====\n\n";
        std::cout << "PC: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << pc << std::dec << std::setfill(' ') << "\n";
        std::cout << "LR: 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << lr << std::dec << std::setfill(' ') << "\n\n";
        std::cout << "(Using .map — limited precision)\n\n";

        const auto& syms = map.GetSymbols();
        for (auto addr : {pc, lr}) {
            const MapSymbol* best = nullptr;
            for (const auto& s : syms)
                if (s.address <= addr && (!best || s.address > best->address)) best = &s;
            std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                      << addr << " : ";
            if (best && (addr - best->address) < 0x8000) {
                uint32_t off = addr - best->address;
                std::cout << best->name << " + 0x" << std::hex << off;
            } else { std::cout << "<unknown>"; }
            std::cout << std::dec << std::setfill(' ') << "\n";
        }
        if (has_cfsr && cfsr) DecodeCFSR(cfsr);
        return 0;
    }

    // ELF-based analysis
    std::cout << "===== CRASH ANALYSIS REPORT =====\n\n";
    std::cout << "PC: " << ResolveAddrElf(elf, pc) << "\n";
    std::cout << "LR: " << ResolveAddrElf(elf, lr) << "\n";
    if (sp != 0) std::cout << "SP: 0x" << std::hex << std::setw(8)
                           << std::setfill('0') << sp << std::dec
                           << std::setfill(' ') << "\n";

    if (!stack_vals.empty()) {
        std::cout << "\nStack values at SP:\n";
        uint32_t addr = sp;
        for (uint32_t val : stack_vals) {
            std::cout << "  [0x" << std::hex << std::setw(8) << std::setfill('0')
                      << addr << "] = " << ResolveAddrElf(elf, val) << "\n";
            addr += 4;
        }
        std::cout << std::dec << std::setfill(' ');
    }

    if (has_cfsr && cfsr) DecodeCFSR(cfsr);

    return 0;
}
