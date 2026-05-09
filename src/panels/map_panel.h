#ifndef MAP_PANEL_H
#define MAP_PANEL_H

#include "panel_base.h"
#include "utils/map_parser.h"
#include <string>
#include <vector>

class MapPanel : public Panel {
public:
    explicit MapPanel(SharedState& s);
    const char* Name() const override { return "Map Analyzer"; }
    void Render() override;

private:
    void LoadMap();
    const char* SectionType(const MapSection& sec) const;

    MapParser parser_;
    char map_path_[256] = "build/template.map";
    int selected_section_ = -1;
    char search_buf_[64] = {0};
};

#endif // MAP_PANEL_H
