#ifndef MAP_PARSER_H
#define MAP_PARSER_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

struct MapSection {
    std::string name;
    uint32_t    vma;
    uint32_t    lma;
    uint32_t    size;
    // Files contributing to this section: (filename, size)
    std::vector<std::pair<std::string, uint32_t>> files;
};

struct MapCrossRef {
    std::string symbol;
    std::vector<std::string> files;
};

struct MapSymbol {
    std::string name;
    uint32_t    address;
    uint32_t    size;
};

class MapParser {
public:
    bool Load(const std::string& path);

    const std::vector<MapSection>& GetSections() const { return sections_; }
    const std::vector<MapCrossRef>& GetCrossRefs() const { return xrefs_; }
    const std::vector<MapSymbol>& GetSymbols() const { return symbols_; }
    bool IsLoaded() const { return loaded_; }

    uint32_t GetTotalFlash() const;
    uint32_t GetTotalRam() const;

    // Find which section contains an address
    const MapSection* FindSectionByAddr(uint32_t addr) const;

private:
    std::vector<MapSection> sections_;
    std::vector<MapCrossRef> xrefs_;
    std::vector<MapSymbol> symbols_;
    bool loaded_ = false;
};

#endif // MAP_PARSER_H
