// PC test tool for the on-Vita installer (port/src/install).
//
//   ddinstall stats <game>_Data            read every object with a type tree; report the ones whose
//                                          tree does not consume exactly the object's bytes
//   ddinstall dump <game>_Data <file> <id> one object as JSON (byte arrays as their size)
//   ddinstall gencheck <game>_Data <trees.json>
//                                          compare the generated MonoBehaviour trees with reference
//                                          trees {"asm|class": [[level, type, name, flag], ...]}
//                                          (UnityPy's, dumped locally); the MonoBehaviour header and
//                                          non-primitive type names are not compared
//   ddinstall export <game>_Data <out>      every exporter, timed (gamedata, loc, help, achievements,
//                                          render, fonts, audio)
//   ddinstall install <game> <out> [fonts]  the Vita's installer: <game> holds <game>_Data and
//                                          UnityPlayer.dll; fonts: where font.tex / font.json are
//   ddinstall pcmcheck <game>_Data <wavs>   decoded AudioClips against UnityPy's WAVs
//   ddinstall list <game>_Data <file> [cls] path ids and classes of a file's objects
#include <chrono>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "install/export_audio.h"
#include "install/export_data.h"
#include "install/export_fonts.h"
#include "install/export_render.h"
#include "install/game_files.h"
#include "install/installer.h"
#include "install/mono_types.h"

using namespace dd::install;

// Live and peak bytes of C++ allocations (the Vita has about 160 MB of heap for the installer)
static size_t g_live = 0, g_peak = 0;

void* operator new(size_t n) {
    size_t* p = static_cast<size_t*>(std::malloc(n + 16));
    if (!p) throw std::bad_alloc();
    *p = n;
    g_live += n, g_peak = std::max(g_peak, g_live);
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    size_t* p = reinterpret_cast<size_t*>(static_cast<char*>(q) - 16);
    g_live -= *p;
    std::free(p);
}
void operator delete(void* q, size_t) noexcept { operator delete(q); }

namespace {

// "live / peak" MB of C++ allocations since the last call (the peak is reset)
std::string MemoryMb() {
    std::string s = std::to_string(g_live >> 20) + " MB, peak " + std::to_string(g_peak >> 20) + " MB";
    g_peak = g_live;
    return s;
}

// binary values as "<n bytes>", like the Python extractor's jsonable
json Printable(const json& v) {
    if (v.is_binary()) return "<" + std::to_string(v.get_binary().size()) + " bytes>";
    if (v.is_object()) {
        json o = json::object();
        for (auto it = v.begin(); it != v.end(); ++it) o[it.key()] = Printable(it.value());
        return o;
    }
    if (v.is_array()) {
        json a = json::array();
        for (const json& x : v) a.push_back(Printable(x));
        return a;
    }
    return v;
}

int Stats(const GameFiles& g) {
    struct Count {
        int ok = 0, bad = 0, none = 0;
    };
    std::map<int, Count> byClass;
    for (const auto& f : g.files)
        for (const ObjectInfo& o : f->objects) {
            ObjRef r{f.get(), &o};
            Count& c = byClass[o.classId];
            const TypeTree* t = g.TreeFor(r);
            if (!t) {
                c.none++;
                continue;
            }
            json v;
            size_t used = 0;
            bool ok = ReadTypeTree(*t, f->ObjectData(o), o.size, f->bigEndian, &v, &used);
            // [SerializeReference] data ends with the references registry, which is not read
            bool registry = false;
            for (const TypeTree::Node& n : t->nodes) registry = registry || n.type == "managedReference";
            if (ok && (used == o.size || (registry && used < o.size))) {
                c.ok++;
            } else {
                if (c.bad++ < 3)
                    std::printf("  class %d %s:%lld read %u of %u bytes%s\n", o.classId, f->name.c_str(), static_cast<long long>(o.pathId),
                                static_cast<unsigned>(used), o.size, ok ? "" : " (failed)");
            }
        }
    int bad = 0;
    for (auto& [cls, c] : byClass) {
        if (c.ok || c.bad) std::printf("class %4d: %6d ok %6d bad %6d without tree\n", cls, c.ok, c.bad, c.none);
        bad += c.bad;
    }
    return bad ? 1 : 0;
}

bool Compared(const std::string& type) {
    static const std::set<std::string> kinds = {"int", "unsigned int", "SInt64", "UInt64", "float", "double", "UInt8", "SInt8",
                                                "SInt16", "UInt16", "string", "Array", "char", "bool", "managedReference"};
    return kinds.count(type) != 0;
}

int GenCheck(ScriptLayouts& layouts, const char* refPath) {
    std::ifstream f(refPath, std::ios::binary);
    json ref = json::parse(f, nullptr, false);
    if (!ref.is_object()) return std::fprintf(stderr, "bad reference file\n"), 1;
    int same = 0, differ = 0, missing = 0;
    for (auto it = ref.begin(); it != ref.end(); ++it) {
        if (!it.value().is_array()) continue;
        size_t bar = it.key().find('|');
        const TypeTree* t = layouts.Tree(it.key().substr(0, bar), it.key().substr(bar + 1));
        if (!t) {
            if (missing++ < 10) std::printf("missing %s\n", it.key().c_str());
            continue;
        }
        // the reference without its ManagedReferencesRegistry subtrees (the reader leaves them out)
        json rows = json::array();
        int skipBelow = -1;
        for (const json& r : it.value()) {
            int level = r[0].get<int>();
            if (skipBelow >= 0 && level > skipBelow) continue;
            skipBelow = -1;
            if (r[1] == "ManagedReferencesRegistry") {
                skipBelow = level;
                continue;
            }
            rows.push_back(r);
        }
        // skip the header in each: up to the first level-1 node after m_Name
        auto after = [](auto count, auto level, auto name) {
            size_t i = 0;
            while (i < count && !(level(i) == 1 && name(i) == "m_Name")) i++;
            for (i++; i < count && level(i) > 1; i++) {
            }
            return i;
        };
        size_t gi = after(t->nodes.size(), [&](size_t i) { return t->nodes[i].level; }, [&](size_t i) { return t->nodes[i].name; });
        size_t ri = after(rows.size(), [&](size_t i) { return rows[i][0].get<int>(); },
                          [&](size_t i) { return rows[i][2].get<std::string>(); });
        std::string diff;
        size_t glen = t->nodes.size() - gi, rlen = rows.size() - ri;
        for (; gi < t->nodes.size() && ri < rows.size() && diff.empty(); gi++, ri++) {
            size_t i = gi;
            const json& r = rows[ri];
            const TypeTree::Node& g = t->nodes[gi];
            std::string rt = r[1];
            bool typeOk = (!Compared(rt) && !Compared(g.type)) || rt == g.type;
            if (r[0].get<int>() != g.level || r[2].get<std::string>() != g.name || r[3].get<uint32_t>() != g.metaFlag || !typeOk)
                diff = "node " + std::to_string(i) + ": got " + std::to_string(g.level) + " " + g.type + " " + g.name + " " +
                       std::to_string(g.metaFlag) + ", want " + std::to_string(r[0].get<int>()) + " " + rt + " " +
                       r[2].get<std::string>() + " " + std::to_string(r[3].get<uint32_t>());
        }
        if (diff.empty() && glen != rlen) diff = "length " + std::to_string(glen) + " vs " + std::to_string(rlen);
        if (diff.empty()) {
            same++;
        } else if (differ++ < 25) {
            std::printf("%s: %s\n", it.key().c_str(), diff.c_str());
        }
    }
    std::printf("gencheck: %d same, %d differ, %d missing\n", same, differ, missing);
    return differ || missing ? 1 : 0;
}

int Export(const GameFiles& g, const std::string& out) {
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    };
    Extracted ex;
    std::string err;
    ExtractFilter filter = ExportFilter();
    if (!Extract(g, &ex, &err, &filter)) return std::fprintf(stderr, "extract: %s\n", err.c_str()), 1;
    size_t configs = 0;
    for (const auto& [c, v] : ex.configs) configs += v.size();
    std::printf("extract: %d configs, %d scene classes, %d tables, %d names (%d ms)\n", static_cast<int>(configs),
                static_cast<int>(ex.scene.size()), static_cast<int>(ex.localization.size()), static_cast<int>(ex.names.size()), ms());
    if (!MakeDirs(out + "/loc")) return std::fprintf(stderr, "cannot create %s\n", out.c_str()), 1;
    bool ok = ExportGamedata(g, ex, out + "/gamedata.json", &err) && ExportLoc(ex, out + "/loc", &err) &&
              ExportHelp(g, ex, out + "/help.json", &err) && ExportAchievements(g, ex, out + "/achievements.json", &err);
    if (!ok) return std::fprintf(stderr, "export: %s\n", err.c_str()), 1;
    std::printf("export: data (%d ms)\n", ms());
    if (!ExportRender(g, ex, out + "/render", &err)) return std::fprintf(stderr, "render: %s\n", err.c_str()), 1;
    std::printf("export: render (%d ms)\n", ms());
    if (!ExportFonts(g, out + "/render", &err)) return std::fprintf(stderr, "fonts: %s\n", err.c_str()), 1;
    if (!ExportAudio(g, ex, g.dataDir() + "/../UnityPlayer.dll", out + "/audio", &err))
        return std::fprintf(stderr, "audio: %s\n", err.c_str()), 1;
    std::printf("export: audio (%d ms)\n", ms());
    std::printf("export: done (%d ms)\n", ms());
    return 0;
}

// every AudioClip decoded and compared with the WAVs UnityPy / FMOD wrote (<dir>/<name>.wav.wav)
int PcmCheck(const GameFiles& g, const std::string& wavDir) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> clips;
    std::vector<uint32_t> crcs;
    for (ObjRef o : g.Objects(83)) {
        json c;
        if (!g.Read(o, &c)) continue;
        const Bytes* res = g.Resource(c["m_Resource"]["m_Source"].get<std::string>());
        size_t off = c["m_Resource"]["m_Offset"].get<size_t>(), size = c["m_Resource"]["m_Size"].get<size_t>();
        if (!res || off + size > res->size()) continue;
        clips.emplace_back(c["m_Name"].get<std::string>(), std::vector<uint8_t>(res->begin() + static_cast<long>(off), res->begin() + static_cast<long>(off + size)));
        if (uint32_t crc = FsbVorbisCrc(clips.back().second.data(), size)) crcs.push_back(crc);
    }
    VorbisSetups setups;
    setups.Load(g.dataDir() + "/../UnityPlayer.dll", crcs);
    int checked = 0, exact = 0, bad = 0;
    for (const auto& [name, fsb] : clips) {
        Bytes wav;
        if (!ReadFile(wavDir + "/" + name + ".wav.wav", &wav) || wav.size() < 44) continue;
        Pcm pcm;
        std::string err;
        if (!DecodeFsb(fsb.data(), fsb.size(), setups, &pcm, &err)) {
            std::printf("%s: %s\n", name.c_str(), err.c_str());
            bad++;
            continue;
        }
        checked++;
        int ch = wav[22] | wav[23] << 8, rate = wav[24] | wav[25] << 8 | wav[26] << 16;
        size_t n = (wav.size() - 44) / 2;
        int maxd = 0;
        size_t diffs = 0;
        for (size_t i = 0; i < std::min(n, pcm.samples.size()); i++) {
            int a = static_cast<int16_t>(wav[44 + i * 2] | wav[45 + i * 2] << 8), d = std::abs(a - pcm.samples[i]);
            if (d) diffs++, maxd = std::max(maxd, d);
        }
        if (ch != pcm.channels || rate != pcm.rate || n != pcm.samples.size() || maxd > 2) {
            if (bad++ < 15)
                std::printf("%s: ch %d/%d rate %d/%d samples %d/%d, %d differ (max %d)\n", name.c_str(), ch, pcm.channels, rate, pcm.rate,
                            static_cast<int>(n), static_cast<int>(pcm.samples.size()), static_cast<int>(diffs), maxd);
        } else if (!diffs) {
            exact++;
        }
    }
    std::printf("pcmcheck: %d clips, %d exact, %d off by at most 2, %d bad\n", checked, exact, checked - exact - bad, bad);
    return bad ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ddinstall stats|dump <game>_Data [file pathId]\n");
        return 2;
    }
    std::string cmd = argv[1], err;
    if (cmd == "install" && argc >= 4) { // install <game folder> <out> [font dir]: what the Vita does
        auto t0 = std::chrono::steady_clock::now();
        std::string last;
        bool ok = Install(argv[2], argv[3], argc >= 5 ? argv[4] : "", [&](const char* what, float f) {
            if (last != what) std::printf("%5.1f%% %s (before: %s)\n", f * 100, what, MemoryMb().c_str()), last = what;
        }, &err);
        if (!ok) return std::fprintf(stderr, "install: %s\n", err.c_str()), 1;
        std::printf("install: done in %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        return 0;
    }
    GameFiles g;
    if (!g.Load(argv[2], &err)) {
        std::fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    std::printf("%d files\n", static_cast<int>(g.files.size()));
    ScriptLayouts layouts(std::string(argv[2]) + "/Managed");
    g.SetScriptTrees([&](const std::string& a, const std::string& c) { return layouts.Tree(a, c); });
    if (cmd == "stats") return Stats(g);
    if (cmd == "gencheck" && argc >= 4) return GenCheck(layouts, argv[3]);
    if (cmd == "export" && argc >= 4) return Export(g, argv[3]);
    if (cmd == "pcmcheck" && argc >= 4) return PcmCheck(g, argv[3]);
    if (cmd == "list" && argc >= 4) { // list <Data> <file> [classId]: path ids and classes
        const SerializedFile* f = g.File(argv[3]);
        if (!f) return std::fprintf(stderr, "no such file\n"), 1;
        for (const ObjectInfo& o : f->objects)
            if (argc < 5 || o.classId == std::atoi(argv[4]))
                std::printf("%lld %d %s\n", static_cast<long long>(o.pathId), o.classId, ClassName(o.classId) ? ClassName(o.classId) : "?");
        return 0;
    }
    if (cmd == "dump" && argc >= 5) {
        const SerializedFile* f = g.File(argv[3]);
        const ObjectInfo* o = f ? f->Find(std::strtoll(argv[4], nullptr, 10)) : nullptr;
        if (!o) return std::fprintf(stderr, "no such object\n"), 1;
        json v;
        bool ok = g.Read({f, o}, &v);
        std::printf("%s%s\n", Printable(v).dump(1).c_str(), ok ? "" : "\n(read failed)");
        return ok ? 0 : 1;
    }
    return 2;
}
