#include "install/installer.h"

#include <cstdio>

#include "install/export_audio.h"
#include "install/export_data.h"
#include "install/export_fonts.h"
#include "install/export_render.h"
#include "install/mono_types.h"

namespace dd::install {

namespace {

// render v2: the arenas' Billboard props; ui 1: the menus' Canvas trees, 2: + the gameplay UI prefabs
const json kVersion = {{"format", 1},
                       {"steps", {{"extract", 1}, {"gamedata", 1}, {"render", 2}, {"fonts", 1}, {"loc", 1},
                                  {"help", 1}, {"achievements", 1}, {"audio", 2}, {"ui", 2}}}};

std::string FindDataDir(const std::string& gameDir) {
    for (const std::string& d : ListDir(gameDir, true)) {
        if (d.size() <= 5 || d.compare(d.size() - 5, 5, "_Data") != 0) continue;
        FILE* f = std::fopen((gameDir + "/" + d + "/globalgamemanagers").c_str(), "rb");
        if (f) {
            std::fclose(f);
            return gameDir + "/" + d;
        }
    }
    return std::string();
}

bool Exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

} // namespace

std::string VersionJson() { return kVersion.dump(1) + "\n"; }

bool HasGameCopy(const std::string& gameDir) {
    return !FindDataDir(gameDir).empty() && Exists(gameDir + "/UnityPlayer.dll");
}

bool NeedsInstall(const std::string& outDir) {
    Bytes b;
    if (!Exists(outDir + "/gamedata.json") || !ReadFile(outDir + "/version.json", &b)) return true;
    json v = json::parse(b.begin(), b.end(), nullptr, false);
    return !v.is_object() || v.value("format", 0) != kVersion["format"] || v.value("steps", json()) != kVersion["steps"];
}

static bool InstallSteps(const std::string& gameDir, const std::string& outDir, const std::string& fontDir,
                         const std::function<void(const char*, float)>& progress, std::string* err) {
    auto report = [&](const char* what, float f) {
        if (progress) progress(what, f);
    };
    std::string dataDir = FindDataDir(gameDir);
    if (dataDir.empty()) return *err = "no game data folder (*_Data) in " + gameDir, false;
    std::string dll = gameDir + "/UnityPlayer.dll";
    if (!Exists(dll)) return *err = "UnityPlayer.dll is missing in " + gameDir, false;
    // an interrupted install is redone: version.json is written last
    std::remove((outDir + "/version.json").c_str());
    if (!MakeDirs(outDir + "/loc") || !MakeDirs(outDir + "/render") || !MakeDirs(outDir + "/audio"))
        return *err = "cannot create " + outDir, false;

    report("Reading the game files", 0.0f);
    GameFiles g;
    if (!g.Load(dataDir, err)) return false;
    Extracted ex;
    {
        report("Reading the game's objects", 0.04f);
        ScriptLayouts layouts(dataDir + "/Managed");
        g.SetScriptTrees([&](const std::string& a, const std::string& c) { return layouts.Tree(a, c); });
        ExtractFilter filter = ExportFilter();
        bool ok = Extract(g, &ex, err, &filter);
        // the menus' Canvas trees (title scene, gameplay scene) need the script trees too
        report("Menus", 0.08f);
        ok = ok && ExportUi(g, ex, "level3", outDir + "/render/ui_title.json", err) &&
             ExportUi(g, ex, "level4", outDir + "/render/ui_game.json", err) &&
             // the gameplay scene's UI prefabs (PlayerInfoPanel, the turn nodes, popups...)
             ExportUi(g, ex, "sharedassets4.assets", outDir + "/render/ui_prefabs.json", err);
        g.SetScriptTrees({}); // the exporters only need the MonoBehaviours' base fields from here on
        if (!ok) return false;
    }
    report("Game data and texts", 0.10f);
    if (!ExportGamedata(g, ex, outDir + "/gamedata.json", err) || !ExportLoc(ex, outDir + "/loc", err) ||
        !ExportHelp(g, ex, outDir + "/help.json", err) || !ExportAchievements(g, ex, outDir + "/achievements.json", err))
        return false;
    report("Textures", 0.12f);
    if (!ExportRender(g, ex, outDir + "/render", err, [&](int done, int total) { report("Textures", 0.12f + 0.36f * done / total); }))
        return false;
    report("Fonts", 0.48f);
    if (!ExportFonts(g, outDir + "/render", err)) return false;
    for (const char* f : {"font.tex", "font.json"}) {
        Bytes b;
        if (!fontDir.empty() && ReadFile(fontDir + "/" + f, &b) &&
            !WriteText(outDir + "/render/" + f, std::string(b.begin(), b.end())))
            return *err = std::string("cannot write ") + f, false;
    }
    g.DropResources();
    report("Sounds and music", 0.50f);
    if (!ExportAudio(g, ex, dll, outDir + "/audio", err, [&](int done, int total) { report("Sounds and music", 0.50f + 0.50f * done / total); }))
        return false;
    g.DropResources();
    if (!WriteText(outDir + "/version.json", VersionJson())) return *err = "cannot write version.json", false;
    report("Done", 1.0f);
    return true;
}

bool Install(const std::string& gameDir, const std::string& outDir, const std::string& fontDir,
             const std::function<void(const char*, float)>& progress, std::string* err) {
    try {
        return InstallSteps(gameDir, outDir, fontDir, progress, err);
    } catch (const std::exception& e) { // unexpected game files (another version of the game)
        *err = std::string("unexpected data: ") + e.what();
    } catch (...) {
        *err = "unexpected error";
    }
    return false;
}

} // namespace dd::install
