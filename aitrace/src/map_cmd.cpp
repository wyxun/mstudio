#include "map_cmd.h"
#include "elf_parser.h"
#include "map_parser.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>

static void PrintUsage() {
    std::cerr << "Usage: aitrace map <subcommand> [args]\n"
              << "  resolve <elf_or_map> <addr1> [addr2...]    Address -> symbol+offset\n"
              << "  info    <elf_or_map>                        Section sizes\n";
}

static uint32_t ParseHex(const char* s) {
    return (uint32_t)std::strtoul(s, nullptr, 16);
}

static bool IsElf(const std::string& path) {
    return path.size() > 4 &&
        (path.substr(path.size() - 4) == ".elf" || path.substr(path.size() - 4) == ".ELF");
}

// Resolve addresses against ELF symbol table
static void ResolveWithElf(const std::string& path,
                           const std::vector<uint32_t>& addrs) {
    ElfParser elf;
    if (!elf.Load(path)) {
        std::cerr << "Failed to load ELF: " << path << "\n";
        return;
    }
    const auto& syms = elf.GetSymbols();
    for (uint32_t addr : addrs) {
        const ElfSymbol* best = nullptr;
        for (const auto& s : syms) {
            if (s.address <= addr && (!best || s.address > best->address))
                best = &s;
        }
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                  << addr << std::dec << std::setfill(' ') << " : ";
        if (best && (addr - best->address) < 0x10000) {
            uint32_t off = addr - best->address;
            std::cout << best->name << " + 0x" << std::hex << off << std::dec;
            const char* type = (best->type == 2) ? " [FUNC]"
                             : (best->type == 1) ? " [OBJECT]" : "";
            std::cout << type;
        } else {
            std::cout << "<unknown>";
        }
        std::cout << "\n";
    }
}

// Resolve addresses against MAP symbol table
static void ResolveWithMap(const std::string& path,
                           const std::vector<uint32_t>& addrs) {
    MapParser map;
    if (!map.Load(path)) {
        std::cerr << "Failed to load MAP: " << path << "\n";
        return;
    }
    const auto& syms = map.GetSymbols();
    for (uint32_t addr : addrs) {
        const MapSymbol* best = nullptr;
        for (const auto& s : syms) {
            if (s.address <= addr && (!best || s.address > best->address))
                best = &s;
        }
        const MapSection* sec = map.FindSectionByAddr(addr);
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0')
                  << addr << std::dec << std::setfill(' ') << " : ";
        if (best && (addr - best->address) < 0x10000) {
            uint32_t off = addr - best->address;
            std::cout << best->name << " + 0x" << std::hex << off << std::dec;
        } else {
            std::cout << "<unknown>";
        }
        if (sec) std::cout << "  [" << sec->name << "]";
        std::cout << "\n";
    }
}

// Print section info for .map files
static void InfoMap(const std::string& path) {
    MapParser map;
    if (!map.Load(path)) {
        std::cerr << "Failed to load MAP: " << path << "\n";
        return;
    }
    std::cout << "MAP: " << path << "\n\nSections:\n";
    for (const auto& sec : map.GetSections()) {
        std::cout << "  " << std::setw(20) << std::left << sec.name
                  << "  VMA:0x" << std::hex << std::setw(8) << std::setfill('0') << sec.vma
                  << "  Size:" << std::dec << std::setw(8) << std::setfill(' ') << sec.size
                  << "  (" << sec.files.size() << " files)\n";
    }
    std::cout << std::dec << "\nTotal Flash: " << map.GetTotalFlash() << " bytes\n";
    std::cout << "Total RAM:   " << map.GetTotalRam() << " bytes\n";
}

// Print symbol counts for .elf files
static void InfoElf(const std::string& path) {
    ElfParser elf;
    if (!elf.Load(path)) {
        std::cerr << "Failed to load ELF: " << path << "\n";
        return;
    }
    auto funcs = elf.GetFunctions();
    auto vars  = elf.GetVariables();
    std::cout << "ELF: " << path << "\n";
    std::cout << "Symbols:   " << elf.GetSymbols().size() << "\n";
    std::cout << "Functions: " << funcs.size() << "\n";
    std::cout << "Variables: " << vars.size() << "\n";
}

int map_main(int argc, char* argv[]) {
    if (argc < 3) { PrintUsage(); return 1; }

    std::string sub = argv[1];
    std::string path = argv[2];

    if (sub == "resolve") {
        std::vector<uint32_t> addrs;
        for (int i = 3; i < argc; i++) addrs.push_back(ParseHex(argv[i]));
        if (addrs.empty()) { std::cerr << "At least one address required.\n"; return 1; }
        if (IsElf(path)) ResolveWithElf(path, addrs);
        else            ResolveWithMap(path, addrs);
    } else if (sub == "info") {
        if (IsElf(path)) InfoElf(path);
        else             InfoMap(path);
    } else {
        PrintUsage();
        return 1;
    }
    return 0;
}
