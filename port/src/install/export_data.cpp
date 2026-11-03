#include "install/export_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <unordered_map>

namespace dd::install {

namespace {

// Asset config classes that are editor/runtime plumbing, not game data
const std::set<std::string> kSkipConfigClasses = {
    "ActivationPlayableAsset", "ActivationTrack", "AnimationPlayableAsset", "AnimationTrack", "AudioPlayableAsset",
    "AudioTrack", "CinemachineBlenderSettings", "CinemachineShot", "CinemachineTrack", "ControllerDataFiles",
    "CustomCalculation_Accelerometer", "CustomCalculation_CompareElementValues", "CustomCalculation_FirstNonZero",
    "CustomCalculation_LogitechGRacingWheelPedals", "ES3GlobalReferences", "GUISkin", "GroupTrack",
    "HardwareJoystickMap", "HardwareJoystickTemplateMap", "MarkerTrack", "NoiseSettings", "SignalAsset",
    "SignalEmitter", "SignalTrack", "TimelineAsset", "EditorConfig", "StringTable", "SharedTableData", "Locale",
    "LocalizationSettings",
};
// Scene singletons whose serialized fields feed game logic, then prefab components (one prefab each)
const char* const kSceneSingletons[] = {
    "Teams", "PlayerClasses", "AppDeathball", "Configs", "PerkManager", "SessionManager", "LeagueManager",
    "GameController", "FieldManager", "FieldGenerator", "EnemyManager", "PlayerManager", "HazardManager",
    "PlayerStatUpgrader", "GlobalStatTracker", "Jukebox", "Player", "Ball", "Enemy", "BreakableWall", "FloorSpikes",
    "LightningHazard", "Treasure"};
// Configs loaded by name in the original (Resources), singletons too
const char* const kNamedSingletons[] = {"SessionConfig", "BuildConfig", "AppConfig_DD",
                                        "TimingConfig", "ScoringConfig", "LevelUpConfig"};
// The extractor writes scene_components/<Class>.json only for classes with this many instances at most
constexpr size_t kMaxSceneInstances = 20;

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool Truthy(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int64_t>() != 0;
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get_ref<const std::string&>().empty();
    if (v.is_array() || v.is_object()) return !v.empty();
    return false;
}

std::string Str(const json& o, const char* k) {
    auto it = o.find(k);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string FileOf(const json& o) { return Str(o, "_file"); }
std::string IdOf(const json& o) { return FileOf(o) + ":" + std::to_string(o.value("_path_id", int64_t(0))); }

const json* SceneFirst(const Extracted& ex, const std::string& cls) {
    auto it = ex.scene.find(cls);
    if (it == ex.scene.end() || it->second.empty() || it->second.size() > kMaxSceneInstances) return nullptr;
    return &it->second.front();
}

// configs(cls) of the exporters: m_Name -> object in file order, later repeats replacing earlier ones
std::vector<std::pair<std::string, const json*>> ConfigsByName(const Extracted& ex, const std::string& cls) {
    std::vector<std::pair<std::string, const json*>> out;
    auto it = ex.configs.find(cls);
    if (it == ex.configs.end()) return out;
    for (const json& d : it->second) {
        std::string n = Str(d, "m_Name");
        auto at = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == n; });
        if (at == out.end()) out.emplace_back(n, &d);
        else at->second = &d;
    }
    return out;
}

// Localization tables: "GUID:<collection guid>" -> (collection name, entry id -> key)
struct Tables {
    std::unordered_map<std::string, std::pair<std::string, std::unordered_map<int64_t, std::string>>> byGuid;

    explicit Tables(const Extracted& ex) {
        for (const json& j : ex.localization) {
            std::string n = Str(j, "m_Name");
            if (n.size() < 12 || n.compare(n.size() - 12, 12, " Shared Data") != 0) continue;
            auto& t = byGuid["GUID:" + Str(j, "m_TableCollectionNameGuidString")];
            t.first = Str(j, "m_TableCollectionName");
            t.second.clear();
            for (const json& e : j.value("m_Entries", json::array())) t.second[e["m_Id"].get<int64_t>()] = Str(e, "m_Key");
        }
    }

    // LocalizedString -> "Table/Key" ("" when unset)
    std::string Loc(const json& ls) const {
        if (!Truthy(ls) || !ls.is_object()) return std::string();
        auto t = byGuid.find(Str(ls.value("m_TableReference", json::object()), "m_TableCollectionName"));
        if (t == byGuid.end()) return std::string();
        const json& e = ls.value("m_TableEntryReference", json::object());
        std::string key = Str(e, "m_Key");
        if (key.empty() && e.contains("m_KeyId")) {
            auto k = t->second.second.find(e["m_KeyId"].get<int64_t>());
            if (k != t->second.second.end()) key = k->second;
        }
        return key.empty() ? std::string() : t->second.first + "/" + key;
    }
};

// A Sprite PPtr of `owner` -> the sprite's name (null if not a sprite)
json SpriteName(const GameFiles& g, const Extracted& ex, const json& owner, const json& ref) {
    if (!Truthy(ref) || !ref.is_object() || !Truthy(ref.value("m_PathID", json(0)))) return nullptr;
    auto hit = ex.names.find(ex.Key(g, FileOf(owner), ref));
    return hit != ex.names.end() && hit->second.first == "Sprite" ? hit->second.second : json(nullptr);
}

// "#rrggbb", channels rounded half to even like Python's round
std::string Colour(const json& c) {
    char buf[8];
    int v[3], i = 0;
    for (const char* k : {"r", "g", "b"})
        v[i++] = static_cast<int>(std::clamp(std::nearbyint(c.value(k, 0.0) * 255), 0.0, 255.0));
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", v[0], v[1], v[2]);
    return buf;
}

} // namespace

ExtractFilter ExportFilter() {
    ExtractFilter f;
    f.skipConfigs = kSkipConfigClasses;
    for (const char* c : kSceneSingletons) f.sceneClasses.insert(c);
    // help.json's popups, achievements.json's list, audio.json's crowd
    for (const char* c : {"EnemyInfoPopup", "PlayerStatsPopup", "AttackTooltip", "AchievementManagerDeathball", "CrowdAudio"})
        f.sceneClasses.insert(c);
    return f;
}

bool WriteText(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool ExportGamedata(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err) {
    // raw objects in the exporter's order: config classes by folder name, then the scene singletons
    std::vector<std::pair<std::string, json>> raw;
    std::unordered_map<std::string, size_t> rawAt;
    auto add = [&](const json& d) {
        std::string id = IdOf(d);
        auto it = rawAt.find(id);
        if (it != rawAt.end()) raw[it->second].second = Jsonable(d);
        else rawAt[id] = raw.size(), raw.emplace_back(id, Jsonable(d));
    };
    std::vector<std::string> classes;
    for (const auto& [cls, items] : ex.configs)
        if (!kSkipConfigClasses.count(cls)) classes.push_back(cls);
    std::stable_sort(classes.begin(), classes.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    for (const std::string& cls : classes)
        for (const json& d : ex.configs.at(cls)) add(d);
    json singletons = json::object();
    for (const char* name : kSceneSingletons) {
        auto it = ex.scene.find(name);
        if (it == ex.scene.end() || it->second.size() > kMaxSceneInstances) continue;
        for (const json& d : it->second) add(d);
        singletons[name] = IdOf(it->second.front());
    }

    std::function<json(const json&, const std::string&)> convert = [&](const json& v, const std::string& file) -> json {
        if (v.is_object()) {
            if (v.size() == 2 && v.contains("m_FileID") && v.contains("m_PathID")) {
                std::string key = ex.Key(g, file, v);
                if (key.empty()) return nullptr;
                if (rawAt.count(key)) return {{"$ref", key}};
                auto hit = ex.names.find(key);
                if (hit != ex.names.end()) {
                    const json& n = hit->second.second;
                    return {{"$asset", hit->second.first + ":" + (n.is_string() ? n.get<std::string>() : "None")}};
                }
                return {{"$missing", key}};
            }
            json o = json::object();
            for (auto it = v.begin(); it != v.end(); ++it) o[it.key()] = convert(it.value(), file);
            return o;
        }
        if (v.is_array()) {
            json a = json::array();
            for (const json& x : v) a.push_back(convert(x, file));
            return a;
        }
        return v;
    };
    static const std::set<std::string> drop = {"m_GameObject", "m_Enabled", "m_Script", "_file",
                                               "_path_id",     "_partial",  "m_Name",   "_class"};
    json objects = json::object();
    for (const auto& [key, d] : raw) {
        json o = {{"_class", d["_class"]}, {"_name", d.value("m_Name", json(""))}};
        std::string file = FileOf(d);
        for (auto it = d.begin(); it != d.end(); ++it)
            if (!drop.count(it.key())) o[it.key()] = convert(it.value(), file);
        objects[key] = std::move(o);
    }
    for (const auto& [key, d] : raw) {
        std::string cls = Str(d, "_class");
        for (const char* name : kNamedSingletons)
            if (cls == name && !singletons.contains(name)) singletons[name] = key;
    }
    json root = {{"singletons", std::move(singletons)}, {"objects", std::move(objects)}};
    if (!WriteText(outPath, root.dump())) return *err = "cannot write " + outPath, false;
    return true;
}

bool ExportLoc(const Extracted& ex, const std::string& outDir, std::string* err) {
    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> keys; // table -> id -> key
    for (const json& j : ex.localization) {
        std::string n = Str(j, "m_Name");
        if (n.size() < 12 || n.compare(n.size() - 12, 12, " Shared Data") != 0) continue;
        auto& ids = keys[Str(j, "m_TableCollectionName")];
        ids.clear();
        for (const json& e : j.value("m_Entries", json::array())) ids[e["m_Id"].get<int64_t>()] = Str(e, "m_Key");
    }
    std::map<std::string, json> locales;
    for (const json& j : ex.localization) {
        std::string n = Str(j, "m_Name");
        size_t us = n.rfind('_');
        if (us == std::string::npos || !j.contains("m_TableData")) continue;
        std::string table = n.substr(0, us), code = n.substr(us + 1);
        const auto& ids = keys[table];
        json& d = locales[code];
        if (d.is_null()) d = json::object();
        for (const json& e : j["m_TableData"]) {
            auto k = ids.find(e["m_Id"].get<int64_t>());
            std::string text = Str(e, "m_Localized");
            if (k != ids.end() && !text.empty()) d[table + "/" + k->second] = text;
        }
    }
    if (locales.empty()) return *err = "no localization tables", false;
    for (const auto& [code, d] : locales)
        if (!WriteText(outDir + "/" + code + ".json", d.dump(0))) return *err = "cannot write " + code + ".json", false;
    return true;
}

bool ExportHelp(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err) {
    Tables tables(ex);
    std::unordered_map<std::string, const json*> itemsById;
    for (const auto& [name, d] : ConfigsByName(ex, "HowToPlayItemConfig")) itemsById[IdOf(*d)] = d;
    auto pagesByName = ConfigsByName(ex, "HowToPlayPageConfig");
    // the title screen's page list in menu order (not in the data); the controller page is the port's own
    static const char* const order[] = {"Basics",          "Scoring",        "Actions",       "Stamina",
                                        "TheBall",         "Enemies",        "EnemyIntentions", "IntentionMarkers",
                                        "PlayerClasses",   "Stats",          "Teams",         "BallMods",
                                        "ScoresAndRanks",  "FinalScore",     "Tips"};
    json pages = json::array();
    for (const char* shortName : order) {
        std::string a = std::string("HowToPlay_") + shortName + "_Page", b = a + "_REMAKE";
        auto pit = std::find_if(pagesByName.begin(), pagesByName.end(),
                                [&](const auto& p) { return p.first == a || p.first == b; });
        if (pit == pagesByName.end()) return *err = "no help page " + a, false;
        json p = *pit->second; // copies: missing fields read as null
        json items = json::array();
        for (const json& ref : p.value("itemConfigs", json::array())) {
            auto iit = itemsById.find(FileOf(p) + ":" + std::to_string(ref.value("m_PathID", int64_t(0))));
            if (iit == itemsById.end()) return *err = "missing help item in " + a, false;
            json it = *iit->second;
            bool showSub = Truthy(it["showSubHeader"]);
            json images = json::array();
            for (json im : it.value("images", json::array()))
                images.push_back({{"sprite", SpriteName(g, ex, it, im["sprite"])},
                                  {"x", im["offset"]["x"]},
                                  {"y", im["offset"]["y"]},
                                  {"control", im["controllerFunction"]},
                                  {"player", Truthy(im["generatePlayerSprite"])}});
            items.push_back({{"text", tables.Loc(it["localizedBody"])},
                             {"fallback", it["text"]},
                             {"color", Colour(it["textColour"])},
                             {"hasSub", showSub || Truthy(it["subHeader"]["enabled"])},
                             {"sub", showSub ? tables.Loc(it["localizedSubHeader"]) : std::string()},
                             {"subColor", Truthy(it["subHeaderColour"]["enabled"])
                                              ? Colour(it["subHeaderColour"]["value"]) : std::string("#cccccc")},
                             {"inline", Truthy(it["collapseSubHeaderToInline"])},
                             {"images", std::move(images)}});
        }
        pages.push_back({{"name", shortName},
                         {"header", tables.Loc(p["localizedHeader"])},
                         {"headerText", p["headerText"]},
                         {"items", std::move(items)}});
    }
    json tiles = json::object();
    for (const auto& [name, tp] : ConfigsByName(ex, "TileInfoConfig")) {
        json t = *tp;
        std::string key = name;
        const std::string prefix = "TileInfoConfig_";
        for (size_t at; (at = key.find(prefix)) != std::string::npos;) key.erase(at, prefix.size());
        tiles[key] = {{"header", tables.Loc(t["localizedHeader"])},
                      {"headerText", t["bespokeName"]},
                      {"text", tables.Loc(t["localizedDescription"])},
                      {"fallback", t["description"]},
                      {"color", Colour(t["headerBackgroundColour"])},
                      {"isBall", Truthy(t["isBall"])},
                      {"isHazard", Truthy(t["isHazard"])}};
    }
    json popups = json::object();
    for (const char* name : {"EnemyInfoPopup", "PlayerStatsPopup", "AttackTooltip"}) {
        const json* comp = SceneFirst(ex, name);
        if (!comp) return *err = std::string("no ") + name, false;
        json p = json::object();
        for (auto it = comp->begin(); it != comp->end(); ++it)
            if (it->is_object() && it->contains("m_TableEntryReference")) p[it.key()] = tables.Loc(*it);
        popups[name] = std::move(p);
    }
    json root = {{"pages", std::move(pages)}, {"tiles", std::move(tiles)}, {"popups", std::move(popups)}};
    if (!WriteText(outPath, root.dump(1))) return *err = "cannot write " + outPath, false;
    return true;
}

bool ExportAchievements(const GameFiles& g, const Extracted& ex, const std::string& outPath, std::string* err) {
    Tables tables(ex);
    std::unordered_map<std::string, const json*> configs;
    auto cit = ex.configs.find("AchievementConfigDeathball");
    if (cit != ex.configs.end())
        for (const json& d : cit->second) configs[IdOf(d)] = &d;
    const json* mp = SceneFirst(ex, "AchievementManagerDeathball");
    if (!mp) return *err = "no AchievementManagerDeathball", false;
    json manager = *mp;
    json list = json::array();
    for (const json& ref : manager.value("achievements", json::array())) {
        auto it = configs.find(ex.Key(g, FileOf(manager), ref));
        if (it == configs.end()) return *err = "missing achievement config", false;
        json a = *it->second;
        list.push_back({{"id", a["achievement"]},
                        {"config", a["m_Name"]},
                        {"name", tables.Loc(a["localizedName"])},
                        {"nameText", a["achievementName"]},
                        {"desc", tables.Loc(a["localizedDescription"])},
                        {"descText", a["description"]},
                        {"hidden", Truthy(a["hidden"])},
                        {"targetValue", a["targetValue"]},
                        {"statType", a["statType"]},
                        {"icon", SpriteName(g, ex, a, a["iconUnlocked"])},
                        {"iconLocked", SpriteName(g, ex, a, a["iconLocked"])}});
    }
    json root = {{"achievements", std::move(list)},
                 {"lockedIcon", SpriteName(g, ex, manager, manager["defaultLockedSprite"])}};
    if (!WriteText(outPath, root.dump(1))) return *err = "cannot write " + outPath, false;
    return true;
}

} // namespace dd::install
