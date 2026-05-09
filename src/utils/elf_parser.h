#ifndef ELF_PARSER_H
#define ELF_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

struct ElfSymbol {
    std::string name;
    uint32_t    address;
    uint32_t    size;
    uint8_t     type;     // 0=NOTYPE, 1=OBJECT, 2=FUNC
    uint8_t     binding;  // 0=LOCAL, 1=GLOBAL, 2=WEAK
};

class ElfParser {
public:
    bool Load(const std::string& path);

    const std::vector<ElfSymbol>& GetSymbols() const { return symbols_; }
    const ElfSymbol* FindByName(const std::string& name) const;
    std::vector<const ElfSymbol*> GetVariables() const;
    std::vector<const ElfSymbol*> GetFunctions() const;
    bool IsLoaded() const { return loaded_; }

private:
    std::vector<ElfSymbol> symbols_;
    bool loaded_ = false;

    static uint32_t ReadU32(const uint8_t* p);
    static uint16_t ReadU16(const uint8_t* p);
};

#endif // ELF_PARSER_H
