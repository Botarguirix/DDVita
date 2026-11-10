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

// ---------------------------------------------------------------------------------------------
// UI: the scenes' Canvas trees (RectTransform, Image, Text, effects, layout) for the port's
// generic Unity UI renderer (ui/canvas.h). Needs the MonoBehaviours' script trees (run it before
// they are dropped).
// ---------------------------------------------------------------------------------------------
namespace dd::install {

namespace {

json V2(const json& v) { return json::array({v.value("x", 0.0), v.value("y", 0.0)}); }
json Rgba(const json& c) { return json::array({c.value("r", 1.0), c.value("g", 1.0), c.value("b", 1.0), c.value("a", 1.0)}); }

std::string ShortName(const std::string& cls) {
    size_t dot = cls.rfind('.');
    return dot == std::string::npos ? cls : cls.substr(dot + 1);
}

} // namespace

bool ExportUi(const GameFiles& g, const Extracted& ex, const std::string& sceneFile, const std::string& outPath, std::string* err) {
    const SerializedFile* f = g.File(sceneFile);
    if (!f) return *err = "no scene " + sceneFile, false;
    Tables tables(ex);
    auto read = [&](int64_t id, json* v) {
        const ObjectInfo* o = f->Find(id);
        return o && g.Read({f, o}, v);
    };
    auto assetName = [&](const json& pptr) -> json {
        ObjRef r = g.Resolve(*f, pptr);
        json a;
        if (!r || !g.Read(r, &a, true) || !a.is_object()) return nullptr;
        return a.value("m_Name", json(nullptr));
    };
    // every RectTransform, keyed by path id; the nodes are numbered in a depth-first walk
    std::unordered_map<int64_t, json> rects;
    for (const ObjectInfo& o : f->objects) {
        if (o.classId != 224) continue;
        json v;
        if (g.Read({f, &o}, &v)) rects[o.pathId] = std::move(v);
    }
    json nodes = json::array();
    std::function<void(int64_t, int)> walk = [&](int64_t rtId, int parent) {
        const json& rt = rects[rtId];
        json go;
        int64_t goId = rt["m_GameObject"].value("m_PathID", int64_t(0));
        read(goId, &go);
        json n = {{"n", go.value("m_Name", "")},
                  {"a", go.value("m_IsActive", 1) != 0},
                  {"p", parent},
                  {"amin", V2(rt["m_AnchorMin"])},
                  {"amax", V2(rt["m_AnchorMax"])},
                  {"piv", V2(rt["m_Pivot"])},
                  {"pos", V2(rt["m_AnchoredPosition"])},
                  {"size", V2(rt["m_SizeDelta"])},
                  {"scale", V2(rt["m_LocalScale"])}};
        json comps = json::array();
        for (const json& c : go.value("m_Component", json::array())) {
            const json& ref = c.contains("component") ? c["component"] : c;
            int64_t cid = ref.value("m_PathID", int64_t(0));
            const ObjectInfo* co = f->Find(cid);
            if (!co || co->classId != 114) continue;
            std::string cls = ShortName(g.ScriptOf({f, co}).second);
            json v;
            if (!g.Read({f, co}, &v) || !v.is_object()) {
                comps.push_back({{"t", cls}});
                continue;
            }
            json k = {{"t", cls}, {"on", v.value("m_Enabled", 1) != 0}};
            if (cls == "Image" || cls == "RawImage") {
                k["sprite"] = assetName(v.value("m_Sprite", json()));
                k["color"] = Rgba(v["m_Color"]);
                for (const char* key : {"m_Type", "m_FillMethod", "m_FillOrigin", "m_FillCenter", "m_FillClockwise", "m_PreserveAspect"})
                    k[key + 2] = v.value(key, 0);
                k["FillAmount"] = v.value("m_FillAmount", 1.0);
                k["PPUM"] = v.value("m_PixelsPerUnitMultiplier", 1.0);
            } else if (cls == "Text") {
                const json& fd = v["m_FontData"];
                k["font"] = assetName(fd["m_Font"]);
                k["color"] = Rgba(v["m_Color"]);
                k["text"] = v.value("m_Text", "");
                for (const char* key : {"m_FontSize", "m_FontStyle", "m_Alignment", "m_HorizontalOverflow", "m_VerticalOverflow",
                                        "m_BestFit", "m_MinSize", "m_MaxSize", "m_RichText", "m_AlignByGeometry"})
                    k[key + 2] = fd.value(key, 0);
                k["LineSpacing"] = fd.value("m_LineSpacing", 1.0);
            } else if (cls == "Shadow" || cls == "Outline") {
                k["color"] = Rgba(v["m_EffectColor"]);
                k["dist"] = V2(v["m_EffectDistance"]);
            } else if (cls == "HorizontalLayoutGroup" || cls == "VerticalLayoutGroup" || cls == "GridLayoutGroup") {
                const json& p = v["m_Padding"];
                k["pad"] = {p.value("m_Left", 0), p.value("m_Right", 0), p.value("m_Top", 0), p.value("m_Bottom", 0)};
                k["align"] = v.value("m_ChildAlignment", 0);
                if (cls == "GridLayoutGroup") {
                    k["cell"] = V2(v["m_CellSize"]);
                    k["spacing"] = V2(v["m_Spacing"]);
                    for (const char* key : {"m_StartCorner", "m_StartAxis", "m_Constraint", "m_ConstraintCount"}) k[key + 2] = v.value(key, 0);
                } else {
                    k["spacing"] = v.value("m_Spacing", 0.0);
                    for (const char* key : {"m_ChildControlWidth", "m_ChildControlHeight", "m_ChildForceExpandWidth",
                                            "m_ChildForceExpandHeight", "m_ChildScaleWidth", "m_ChildScaleHeight", "m_ReverseArrangement"})
                        k[key + 2] = v.value(key, 0);
                }
            } else if (cls == "LayoutElement") {
                for (const char* key : {"m_MinWidth", "m_MinHeight", "m_PreferredWidth", "m_PreferredHeight", "m_FlexibleWidth", "m_FlexibleHeight"})
                    k[key + 2] = v.value(key, -1.0);
                k["Ignore"] = v.value("m_IgnoreLayout", 0);
            } else if (cls == "ContentSizeFitter") {
                k["h"] = v.value("m_HorizontalFit", 0);
                k["v"] = v.value("m_VerticalFit", 0);
            } else if (cls == "Mask") {
                k["show"] = v.value("m_ShowMaskGraphic", 1);
            } else if (cls == "LocalizeStringEvent") {
                k["key"] = tables.Loc(v.value("m_StringReference", json()));
            } else if (cls == "CanvasScaler") {
                k["ref"] = V2(v["m_ReferenceResolution"]);
                k["factor"] = v.value("m_ScaleFactor", 1.0);
                k["mode"] = v.value("m_UiScaleMode", 0);
            }
            comps.push_back(std::move(k));
        }
        if (!comps.empty()) n["c"] = std::move(comps);
        int me = static_cast<int>(nodes.size());
        nodes.push_back(std::move(n));
        json kids = json::array();
        for (const json& c : rt.value("m_Children", json::array())) {
            int64_t cid = c.value("m_PathID", int64_t(0));
            if (!rects.count(cid)) continue;
            kids.push_back(static_cast<int>(nodes.size()));
            walk(cid, me);
        }
        nodes[static_cast<size_t>(me)]["k"] = std::move(kids);
    };
    std::vector<int64_t> roots;
    for (const ObjectInfo& o : f->objects)
        // a Canvas root: its parent (if any) is a plain Transform, not a RectTransform
        if (o.classId == 224 && rects.count(o.pathId) && !rects.count(rects[o.pathId]["m_Father"].value("m_PathID", int64_t(0))))
            roots.push_back(o.pathId);
    for (int64_t r : roots) walk(r, -1);
    if (!WriteText(outPath, json({{"nodes", std::move(nodes)}}).dump())) return *err = "cannot write " + outPath, false;
    return true;
}

} // namespace dd::install
