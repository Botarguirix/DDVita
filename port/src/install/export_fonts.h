// font_s / font_m / font_l (.tex + .json) from the game's own fonts, as tools/make_font.py makes them
#pragma once
#include <string>

#include "install/game_files.h"

namespace dd::install {

bool ExportFonts(const GameFiles& g, const std::string& outDir, std::string* err);

} // namespace dd::install
