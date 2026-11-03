#include "install/extract.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace dd::install {

namespace {

// Scene components of these assemblies are kept (singletons such as Teams, PlayerClasses)
bool GameAssembly(std::string a) {
    if (a.size() > 4 && a.compare(a.size() - 4, 4, ".dll") == 0) a.resize(a.size() - 4);
    return a == "_Project.Runtime" || a == "com.mattglanville.eggtools-base" || a == "com.mattglanville.eggtools-menus";
}

std::string ShortClass(const std::string& full) {
    size_t dot = full.find_last_of('.');
    return dot == std::string::npos ? full : full.substr(dot + 1);
}

bool IsBundleFile(const std::string& name) { return name.rfind("CAB-", 0) == 0; }

// extract_assets.safe: file-name-safe text, at most 120 characters (code points)
std::string SafeName(const std::string& name, const std::string& fallback) {
    std::string s;
    for (char c : name) s += (std::string("<>:\"/\\|?*").find(c) != std::string::npos ||
                              static_cast<unsigned char>(c) < 0x20) ? '_' : c;
    size_t b = s.find_first_not_of(" \t\n\r\v\f"), e = s.find_last_not_of(" \t\n\r\v\f");
    s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    if (s.empty()) s = fallback;
    size_t chars = 0, i = 0;
    for (; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && chars++ == 120) break;
    return s.substr(0, i);
}

// ObjectReader.peek_name: the object read up to its first top-level "m_Name" / "name" field
json PeekName(const GameFiles& g, ObjRef o) {
    const TypeTree* t = g.TreeFor(o);
    if (NameField(o.obj->classId) == 1 &&
        (!t || (!t->children[0].empty() && t->nodes[static_cast<size_t>(t->children[0][0])].name == "m_Name"))) {
        Reader r(o.file->ObjectData(*o.obj), o.obj->size, o.file->bigEndian);
        int32_t len = r.I32();
        const uint8_t* p = len > 0 ? r.Ptr(static_cast<size_t>(len)) : nullptr;
        return p ? json(std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(len))) : json("");
    }
    if (!t) return nullptr;
    for (int c : t->children[0]) {
        const std::string& n = t->nodes[static_cast<size_t>(c)].name;
        if (n == "m_Name" || n == "name") {
            json v;
            if (!g.Read(o, &v) || !v.is_object()) return nullptr;
            auto it = v.find(n);
            return it == v.end() ? json(nullptr) : *it;
        }
    }
    return nullptr;
}

} // namespace

json Jsonable(const json& v) {
    if (v.is_binary()) {
        const auto& b = v.get_binary();
        if (b.has_subtype() && b.subtype() == 0) {
            json a = json::array();
            for (uint8_t x : b) a.push_back(x);
            return a;
        }
        if (b.size() > 64) return "<" + std::to_string(b.size()) + " bytes>";
        static const char* hex = "0123456789abcdef";
        std::string s;
        for (uint8_t x : b) s += hex[x >> 4], s += hex[x & 15];
        return s;
    }
    if (v.is_object()) {
        json o = json::object();
        for (auto it = v.begin(); it != v.end(); ++it) o[it.key()] = Jsonable(it.value());
        return o;
    }
    if (v.is_array()) {
        json a = json::array();
        for (const json& x : v) a.push_back(Jsonable(x));
        return a;
    }
    return v;
}

std::string Extracted::Key(const GameFiles& g, const std::string& file, const json& pptr) const {
    if (!pptr.is_object() || !pptr.contains("m_PathID") || pptr["m_PathID"].get<int64_t>() == 0) return std::string();
    int fid = pptr["m_FileID"].get<int>();
    std::string target = file;
    if (fid != 0) {
        const SerializedFile* f = g.File(file);
        if (!f || fid < 0 || static_cast<size_t>(fid) > f->externals.size()) return std::string();
        target = f->externals[static_cast<size_t>(fid - 1)];
    }
    return target + ":" + std::to_string(pptr["m_PathID"].get<int64_t>());
}

static std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool Extract(const GameFiles& g, Extracted* out, std::string* err, const ExtractFilter* filter) {
    // configs/<Class>/<name>.json as the extractor names them on Windows (case-insensitive,
    // "__1" for repeats); the exporters read them in that file order
    std::map<std::string, std::vector<std::pair<std::string, json>>> named;
    std::map<std::string, std::set<std::string>> used;
    for (const auto& f : g.files) {
        bool bundle = IsBundleFile(f->name);
        for (const ObjectInfo& o : f->objects) {
            ObjRef r{f.get(), &o};
            int cls = o.classId;
            std::string key = f->name + ":" + std::to_string(o.pathId);
            if (cls != 114) {
                if (cls != 4 && cls != 224 && cls != 222) { // Transform, RectTransform, CanvasRenderer
                    const char* name = ClassName(cls);
                    out->names[key] = {name ? name : std::to_string(cls), PeekName(g, r)};
                }
                continue;
            }
            auto [assembly, full] = g.ScriptOf(r);
            if (full.empty()) continue;
            if (filter && !bundle) {
                // decide from the base fields whether the object is kept before reading all of it
                std::string cls = ShortClass(full);
                bool skipConfig = filter->skipConfigs.count(cls) != 0;
                bool wantScene = GameAssembly(assembly) && (filter->sceneClasses.empty() || filter->sceneClasses.count(cls));
                if (skipConfig || !wantScene) {
                    json base;
                    if (!g.Read(r, &base, true)) continue;
                    const json& go = base["m_GameObject"];
                    bool asset = !go.is_object() || go.value("m_PathID", int64_t(0)) == 0;
                    if (asset ? skipConfig : !wantScene) continue;
                }
            }
            json v;
            if (!g.Read(r, &v) || !v.is_object()) {
                std::fprintf(stderr, "extract: cannot read %s (%s)\n", key.c_str(), full.c_str());
                continue;
            }
            std::string shortName = ShortClass(full);
            v["_file"] = f->name;
            v["_path_id"] = o.pathId;
            v["_class"] = shortName;
            if (bundle) out->localization.push_back(v);
            const json& go = v["m_GameObject"];
            bool asset = !go.is_object() || go.value("m_PathID", int64_t(0)) == 0;
            if (asset) {
                const json& nm = v["m_Name"];
                std::string stem = SafeName(nm.is_string() ? nm.get<std::string>() : std::string(),
                                            std::to_string(o.pathId));
                std::set<std::string>& taken = used[shortName];
                std::string file = stem + ".json";
                for (int i = 1; taken.count(Lower(file)); i++) file = stem + "__" + std::to_string(i) + ".json";
                taken.insert(Lower(file));
                named[shortName].emplace_back(Lower(file), std::move(v));
            } else if (GameAssembly(assembly)) {
                out->scene[shortName].push_back(std::move(v));
            }
        }
    }
    for (auto& [cls, items] : named) {
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<json>& list = out->configs[cls];
        for (auto& it : items) list.push_back(std::move(it.second));
    }
    (void)err;
    return true;
}

} // namespace dd::install
