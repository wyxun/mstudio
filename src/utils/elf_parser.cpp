#include "elf_parser.h"
#include <fstream>
#include <cstring>

// ELF constants
enum {
    ELFCLASS32  = 1,
    ELFDATA2LSB = 1,
    EM_ARM      = 40,

    SHT_SYMTAB  = 2,
    SHT_STRTAB  = 3,
    SHT_DYNSYM  = 11,
};

enum {
    STB_LOCAL  = 0,
    STB_GLOBAL = 1,
    STB_WEAK   = 2,
};

enum {
    STT_NOTYPE  = 0,
    STT_OBJECT  = 1,
    STT_FUNC    = 2,
};

// ELF32 header layout
static constexpr int EHDR_SIZE    = 52;
static constexpr int SHDR_SIZE    = 40;


uint32_t ElfParser::ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t ElfParser::ReadU16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool ElfParser::Load(const std::string& path) {
    symbols_.clear();
    loaded_ = false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // 1. Read ELF header
    uint8_t ehdr[EHDR_SIZE];
    if (!file.read(reinterpret_cast<char*>(ehdr), EHDR_SIZE)) return false;

    // Validate magic: 0x7F 'E' 'L' 'F'
    if (ehdr[0] != 0x7F || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F')
        return false;
    if (ehdr[4] != ELFCLASS32) return false;   // 32-bit
    if (ehdr[5] != ELFDATA2LSB) return false;  // little-endian
    if (ReadU16(&ehdr[18]) != EM_ARM) return false;         // ARM machine

    uint32_t e_shoff    = ReadU32(&ehdr[32]);
    uint16_t e_shentsize = ReadU16(&ehdr[46]);
    uint16_t e_shnum    = ReadU16(&ehdr[48]);
    uint16_t e_shstrndx = ReadU16(&ehdr[50]);

    if (e_shentsize != SHDR_SIZE) return false;

    // 2. Read the section name string table header (e_shstrndx)
    uint32_t shstrtab_off = 0;
    uint32_t shstrtab_sz  = 0;
    {
        uint8_t shdr[SHDR_SIZE];
        uint32_t pos = e_shoff + (uint32_t)e_shstrndx * SHDR_SIZE;
        file.seekg(pos);
        if (!file.read(reinterpret_cast<char*>(shdr), SHDR_SIZE)) return false;
        shstrtab_off = ReadU32(&shdr[16]);
        shstrtab_sz  = ReadU32(&shdr[20]);
    }

    // 3. Read the section name string table
    std::vector<char> shstrtab(shstrtab_sz);
    file.seekg(shstrtab_off);
    if (!file.read(shstrtab.data(), shstrtab_sz)) return false;

    // 4. Iterate section headers to find .symtab and .strtab
    uint32_t symtab_off = 0, symtab_sz = 0, symtab_entsize = 0;
    uint32_t strtab_off = 0, strtab_sz = 0;

    for (int i = 0; i < e_shnum; i++) {
        uint8_t shdr[SHDR_SIZE];
        uint32_t pos = e_shoff + (uint32_t)i * SHDR_SIZE;
        file.seekg(pos);
        if (!file.read(reinterpret_cast<char*>(shdr), SHDR_SIZE)) return false;

        uint32_t sh_name  = ReadU32(&shdr[0]);
        uint32_t sh_type  = ReadU32(&shdr[4]);
        uint32_t sh_size  = ReadU32(&shdr[20]);

        if (sh_name >= shstrtab_sz) continue;
        const char* name = &shstrtab[sh_name];

        if (sh_type == SHT_SYMTAB && std::strcmp(name, ".symtab") == 0) {
            symtab_off     = ReadU32(&shdr[16]);
            symtab_sz      = sh_size;
            symtab_entsize = ReadU32(&shdr[36]);
        } else if (sh_type == SHT_STRTAB && std::strcmp(name, ".strtab") == 0) {
            strtab_off = ReadU32(&shdr[16]);
            strtab_sz  = sh_size;
        }
    }

    if (symtab_off == 0 || strtab_off == 0) return false;

    // 5. Read .strtab
    std::vector<char> strtab(strtab_sz);
    file.seekg(strtab_off);
    if (!file.read(strtab.data(), strtab_sz)) return false;

    // 6. Parse symbol entries
    size_t sym_count = symtab_sz / symtab_entsize;
    std::vector<uint8_t> symtab_data(symtab_sz);
    file.seekg(symtab_off);
    if (!file.read(reinterpret_cast<char*>(symtab_data.data()), symtab_sz)) return false;

    for (size_t i = 0; i < sym_count; i++) {
        const uint8_t* entry = &symtab_data[i * symtab_entsize];
        uint32_t st_name  = ReadU32(&entry[0]);
        uint32_t st_value = ReadU32(&entry[4]);
        uint32_t st_size  = ReadU32(&entry[8]);
        uint8_t  st_info  = entry[12];
        uint16_t st_shndx = ReadU16(&entry[14]);

        // Skip undefined (SHN_UNDEF = 0) and absolute (SHN_ABS = 0xFFF1) symbols with zero address
        if (st_shndx == 0) continue;

        // Resolve name from strtab
        if (st_name >= strtab_sz) continue;
        const char* sym_name = &strtab[st_name];
        if (sym_name[0] == '\0') continue;

        ElfSymbol sym;
        sym.name    = sym_name;
        sym.address = st_value;
        sym.size    = st_size;
        sym.type    = (st_info & 0x0F);
        sym.binding = (st_info >> 4);
        symbols_.push_back(std::move(sym));
    }

    loaded_ = true;
    return true;
}

const ElfSymbol* ElfParser::FindByName(const std::string& name) const {
    for (const auto& sym : symbols_) {
        if (sym.name == name) return &sym;
    }
    return nullptr;
}

std::vector<const ElfSymbol*> ElfParser::GetVariables() const {
    std::vector<const ElfSymbol*> result;
    for (const auto& sym : symbols_) {
        if (sym.type == STT_OBJECT) result.push_back(&sym);
    }
    return result;
}

std::vector<const ElfSymbol*> ElfParser::GetFunctions() const {
    std::vector<const ElfSymbol*> result;
    for (const auto& sym : symbols_) {
        if (sym.type == STT_FUNC) result.push_back(&sym);
    }
    return result;
}
