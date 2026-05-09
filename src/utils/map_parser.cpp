#include "map_parser.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <vector>

static bool TryParseHex(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isxdigit((unsigned char)c)) return false;
    }
    out = (uint32_t)std::strtoul(s.c_str(), nullptr, 16);
    return true;
}

// Tail after hex columns has the form: [Align] [Out/In] [Symbol]
// Extract the actual symbol name (last token group, after skipping 2 numeric tokens).
static std::string ExtractSymbolName(const std::string& tail) {
    std::vector<std::string> tokens;
    std::istringstream iss(tail);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);

    if (tokens.empty()) return "";
    // Pattern: align out_or_in symbol [= value]
    if (tokens.size() >= 3) {
        std::string name = tokens[2];
        for (size_t i = 3; i < tokens.size(); i++)
            name += " " + tokens[i];
        return name;
    }
    return tokens.back();
}

static bool IsFlashSection(const std::string& name) {
    return name == ".text" || name == ".rodata" || name == ".isr_vector" ||
           name.find(".ARM") == 0 || name.find(".init") == 0 ||
           name.find(".fini") == 0 || name == ".data"; // .data LMA is in flash
}

static bool IsRamSection(const std::string& name) {
    return name == ".data" || name == ".bss" || name == ".noinit" ||
           name == "._user_heap_stack" || name.find(".ram") == 0;
}

bool MapParser::Load(const std::string& path) {
    sections_.clear();
    xrefs_.clear();
    loaded_ = false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    MapSection* cur_section = nullptr;

    while (std::getline(file, line)) {
        if (line.find("Cross Reference") != std::string::npos ||
            line.find("Cross reference") != std::string::npos) break;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.find("VMA") != std::string::npos &&
            line.find("LMA") != std::string::npos) continue;

        // Token-based parsing: first 3 tokens are VMA, LMA, Size (all hex)
        std::istringstream iss(line);
        std::string vma_str, lma_str, size_str;
        if (!(iss >> vma_str >> lma_str >> size_str)) continue;

        uint32_t vma, lma, size;
        if (!TryParseHex(vma_str, vma) ||
            !TryParseHex(lma_str, lma) ||
            !TryParseHex(size_str, size)) continue;

        // Remaining line: Align Out/In Symbol
        std::string tail;
        std::getline(iss, tail);
        std::string symbol = ExtractSymbolName(tail);
        if (symbol.empty()) continue;

        // Skip alignment directives and absolute symbol defs
        if (symbol.find(". = ALIGN") != std::string::npos) continue;
        if (symbol.find("LOADADDR") != std::string::npos) continue;
        if (symbol.find('=') != std::string::npos) continue; // _estack = ..., etc.

        // Section header: symbol starts with '.' and size > 0
        if (symbol[0] == '.' && size > 0) {
            // Filter debug sections and other non-load sections
            if (symbol.find(".debug_") == 0 || symbol == ".comment" ||
                symbol == ".symtab" || symbol == ".strtab" || symbol == ".shstrtab") {
                cur_section = nullptr;
                continue;
            }
            MapSection sec;
            sec.name = symbol;
            sec.vma  = vma;
            sec.lma  = lma;
            sec.size = size;
            sections_.push_back(std::move(sec));
            cur_section = &sections_.back();
            continue;
        }

        // File contribution: contains ".o:("
        if (cur_section && symbol.find(".o:(") != std::string::npos) {
            size_t last_slash = symbol.find_last_of("/\\");
            std::string fname = (last_slash != std::string::npos)
                                ? symbol.substr(last_slash + 1) : symbol;
            cur_section->files.push_back({fname, size});
            continue;
        }

        // Individual symbol entry (non-section, non-file, inside a section)
        if (cur_section && !symbol.empty() && symbol[0] != '.') {
            // Filter linker-generated absolute symbols (= value)
            if (symbol.find('=') == std::string::npos) {
                MapSymbol msym;
                msym.name = symbol;
                msym.address = vma;
                msym.size = size;
                symbols_.push_back(std::move(msym));
            }
        }
    }

    // Phase 2: Parse cross-reference table
    std::getline(file, line); // blank
    std::getline(file, line); // "Symbol                                            File"

    MapCrossRef* cur_xref = nullptr;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line[0] != ' ') {
            std::string sym;
            if (line.length() > 50) sym = line.substr(0, 50);
            else sym = line;

            size_t end = sym.find_last_not_of(" \t");
            if (end != std::string::npos) sym = sym.substr(0, end + 1);

            if (!sym.empty()) {
                MapCrossRef xref;
                xref.symbol = sym;
                xrefs_.push_back(std::move(xref));
                cur_xref = &xrefs_.back();
            }
            continue;
        }

        if (cur_xref && line.find(".o") != std::string::npos) {
            std::string fname = line;
            size_t first = fname.find_first_not_of(" \t");
            if (first != std::string::npos) fname = fname.substr(first);

            size_t last_slash = fname.find_last_of("/\\");
            if (last_slash != std::string::npos) fname = fname.substr(last_slash + 1);

            cur_xref->files.push_back(fname);
        }
    }

    loaded_ = true;
    return true;
}

uint32_t MapParser::GetTotalFlash() const {
    uint32_t total = 0;
    for (const auto& sec : sections_) {
        if (IsFlashSection(sec.name)) total += sec.size;
    }
    return total;
}

uint32_t MapParser::GetTotalRam() const {
    uint32_t total = 0;
    for (const auto& sec : sections_) {
        if (IsRamSection(sec.name)) total += sec.size;
    }
    return total;
}

const MapSection* MapParser::FindSectionByAddr(uint32_t addr) const {
    for (const auto& sec : sections_) {
        if (addr >= sec.vma && addr < sec.vma + sec.size) return &sec;
    }
    return nullptr;
}
