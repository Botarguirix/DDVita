// The on-Vita installer: turns the user's own copy of the game (the "Dungeon Deathball_Data" folder
// and UnityPlayer.dll, copied as they are) into the port's data/, the same files
// tools/prepare_data.py makes on a PC. Nothing of the game is in the VPK.
#pragma once
#include <functional>
#include <string>

namespace dd::install {

// Data format and step versions written to version.json (keep in step with tools/prepare_data.py)
std::string VersionJson();

// gameDir holds "<game>_Data" and UnityPlayer.dll
bool HasGameCopy(const std::string& gameDir);

// True when outDir/version.json is missing or older than what this installer writes
bool NeedsInstall(const std::string& outDir);

// progress(stage, fraction 0..1). fontDir: where font.tex / font.json (the port's own HUD font,
// not game data) are; copied into render/ when given.
bool Install(const std::string& gameDir, const std::string& outDir, const std::string& fontDir,
             const std::function<void(const char*, float)>& progress, std::string* err);

} // namespace dd::install
