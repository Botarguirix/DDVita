// The data/ files built from the game's objects, as tools/export_gamedata.py, export_loc.py,
// export_help.py and export_achievements.py write them from extract_assets.py's output.
#pragma once
#include <string>

#include "install/extract.h"

namespace dd::install {

bool ExportGamedata(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err);
bool ExportLoc(const Extracted& ex, const std::string& outDir, std::string* err); // <outDir>/<locale>.json
bool ExportHelp(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err);
bool ExportAchievements(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err);
// A scene's Canvas trees (render/ui_*.json). Needs the MonoBehaviours' full script trees.
bool ExportUi(const GameFiles& g, const Extracted& ex, const std::string& sceneFile, const std::string& outPath, std::string* err);

// Only what the exporters (data, render, audio) read: their scene classes, no skipped configs
ExtractFilter ExportFilter();

// Writes text to a file (false on error)
bool WriteText(const std::string& path, const std::string& text);

} // namespace dd::install
