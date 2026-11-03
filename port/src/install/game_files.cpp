#include "install/game_files.h"

#include <algorithm>
#include <cstdio>

#ifdef __vita__
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#else
#include <filesystem>
#endif

namespace dd::install {

bool ReadFile(const std::string& path, Bytes* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out->resize(size > 0 ? static_cast<size_t>(size) : 0);
    bool ok = size >= 0 && std::fread(out->data(), 1, out->size(), f) == out->size();
    std::fclose(f);
    return ok;
}

std::vector<std::string> ListDir(const std::string& dir, bool wantDirs) {
    std::vector<std::string> names;
#ifdef __vita__
    SceUID d = sceIoDopen(dir.c_str());
    if (d < 0) return names;
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0)
        if (SCE_S_ISDIR(e.d_stat.st_mode) == wantDirs) names.push_back(e.d_name);
    sceIoDclose(d);
#else
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec))
        if (e.is_directory() == wantDirs) names.push_back(e.path().filename().u8string());
#endif
    std::sort(names.begin(), names.end());
    return names;
}

bool MakeDirs(const std::string& dir) {
#ifdef __vita__
    for (size_t at = dir.find('/'); at != std::string::npos; at = dir.find('/', at + 1))
        sceIoMkdir(dir.substr(0, at).c_str(), 0777);
    sceIoMkdir(dir.c_str(), 0777);
    SceIoStat st;
    return sceIoGetstat(dir.c_str(), &st) >= 0 && SCE_S_ISDIR(st.st_mode);
#else
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    return std::filesystem::is_directory(std::filesystem::u8path(dir), ec);
#endif
}

namespace {

// serialized files of a player build: globalgamemanagers(.assets), levelN, sharedassetsN.assets, resources.assets
bool IsSerializedName(const std::string& n) {
    auto digitsFrom = [&](size_t at, size_t end) {
        if (at >= end) return false;
        for (size_t i = at; i < end; i++)
            if (n[i] < '0' || n[i] > '9') return false;
        return true;
    };
    auto ends = [&](const char* s) {
        std::string e = s;
        return n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0;
    };
    if (n == "globalgamemanagers" || n == "globalgamemanagers.assets" || n == "resources.assets") return true;
    if (n.rfind("level", 0) == 0) return digitsFrom(5, n.size());
    if (n.rfind("sharedassets", 0) == 0 && ends(".assets")) return digitsFrom(12, n.size() - 7);
    return false;
}

void FindBundles(const std::string& dir, std::vector<std::string>* out) {
    for (const std::string& f : ListDir(dir))
        if (f.size() > 7 && f.compare(f.size() - 7, 7, ".bundle") == 0) out->push_back(dir + "/" + f);
    for (const std::string& d : ListDir(dir, true)) FindBundles(dir + "/" + d, out);
}

} // namespace

bool GameFiles::Load(const std::string& dataDir, std::string* err) {
    dataDir_ = dataDir;
    for (const std::string& n : ListDir(dataDir)) {
        if (!IsSerializedName(n)) continue;
        auto data = std::make_shared<Bytes>();
        if (!ReadFile(dataDir + "/" + n, data.get())) return *err = "cannot read " + n, false;
        auto f = std::make_unique<SerializedFile>();
        if (!f->Parse(n, data, err)) return false;
        byName_[n] = files.size();
        files.push_back(std::move(f));
    }
    if (files.empty()) return *err = "no game files in " + dataDir, false;
    // Unity's built-in resources (default materials, fonts...) that the game's files point to
    for (const char* n : {"unity default resources", "unity_builtin_extra"}) {
        auto data = std::make_shared<Bytes>();
        if (!ReadFile(dataDir + "/Resources/" + n, data.get())) continue;
        auto f = std::make_unique<SerializedFile>();
        if (!f->Parse(n, data, err)) return false;
        byName_[n] = files.size();
        files.push_back(std::move(f));
    }
    std::vector<std::string> bundles;
    FindBundles(dataDir + "/StreamingAssets/aa", &bundles);
    for (const std::string& path : bundles) {
        Bytes raw;
        std::vector<std::pair<std::string, Bytes>> inner;
        if (!ReadFile(path, &raw) || !ReadBundle(raw, &inner, err)) return *err = path + ": " + *err, false;
        for (auto& [name, bytes] : inner) {
            if (name.size() > 5 && name.compare(name.size() - 5, 5, ".resS") == 0) {
                resources_[name] = std::make_unique<Bytes>(std::move(bytes));
                continue;
            }
            auto f = std::make_unique<SerializedFile>();
            if (!f->Parse(name, std::make_shared<Bytes>(std::move(bytes)), err)) return false;
            byName_[name] = files.size();
            files.push_back(std::move(f));
        }
    }
    return true;
}

const SerializedFile* GameFiles::File(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : files[it->second].get();
}

std::vector<ObjRef> GameFiles::Objects(int classId) const {
    std::vector<ObjRef> out;
    for (const auto& f : files)
        for (const ObjectInfo& o : f->objects)
            if (o.classId == classId) out.push_back({f.get(), &o});
    return out;
}

ObjRef GameFiles::Resolve(const SerializedFile& from, int fileId, int64_t pathId) const {
    if (pathId == 0) return {};
    const SerializedFile* f = &from;
    if (fileId != 0) {
        if (fileId < 0 || static_cast<size_t>(fileId) > from.externals.size()) return {};
        f = File(from.externals[static_cast<size_t>(fileId - 1)]);
        if (!f) return {};
    }
    const ObjectInfo* o = f->Find(pathId);
    return o ? ObjRef{f, o} : ObjRef{};
}

ObjRef GameFiles::Resolve(const SerializedFile& from, const json& pptr) const {
    if (!pptr.is_object() || !pptr.contains("m_FileID") || !pptr.contains("m_PathID")) return {};
    return Resolve(from, pptr["m_FileID"].get<int>(), pptr["m_PathID"].get<int64_t>());
}

std::pair<std::string, std::string> GameFiles::ScriptOf(ObjRef mb) const {
    auto it = scriptOf_.find(mb.obj);
    if (it != scriptOf_.end()) return it->second;
    std::pair<std::string, std::string> r;
    json base, script;
    if (Read(mb, &base, true) && base.contains("m_Script")) {
        ObjRef s = Resolve(*mb.file, base["m_Script"]);
        if (s && s.obj->classId == 115 && Read(s, &script)) {
            std::string ns = script.value("m_Namespace", "");
            r = {script.value("m_AssemblyName", ""), (ns.empty() ? "" : ns + ".") + script.value("m_ClassName", "")};
        }
    }
    scriptOf_[mb.obj] = r;
    return r;
}

const TypeTree* GameFiles::TreeFor(ObjRef o, bool baseOnly) const {
    const SerializedType& t = o.file->types[static_cast<size_t>(o.obj->type)];
    if (t.tree) return t.tree.get();
    if (o.obj->classId == 114 && !baseOnly && scriptTrees_) {
        auto [asm_, cls] = ScriptOf(o);
        if (!cls.empty())
            if (const TypeTree* tree = scriptTrees_(asm_, cls)) return tree;
    }
    return EngineType(o.obj->classId);
}

bool GameFiles::Read(ObjRef o, json* out, bool baseOnly) const {
    const TypeTree* tree = TreeFor(o, baseOnly);
    if (!tree) return false;
    return ReadTypeTree(*tree, o.file->ObjectData(*o.obj), o.obj->size, o.file->bigEndian, out);
}

const Bytes* GameFiles::Resource(const std::string& path) const {
    std::string name = path;
    size_t s = name.find_last_of("/\\");
    if (s != std::string::npos) name = name.substr(s + 1);
    auto it = resources_.find(name);
    if (it != resources_.end()) return it->second.get();
    auto bytes = std::make_unique<Bytes>();
    if (!ReadFile(dataDir_ + "/" + name, bytes.get())) return nullptr;
    return (resources_[name] = std::move(bytes)).get();
}

bool GameFiles::ReadResource(const std::string& path, uint64_t offset, size_t size, Bytes* out) const {
    std::string name = path;
    size_t s = name.find_last_of("/\\");
    if (s != std::string::npos) name = name.substr(s + 1);
    auto it = resources_.find(name);
    if (it != resources_.end()) {
        if (offset + size > it->second->size()) return false;
        out->assign(it->second->begin() + static_cast<long>(offset), it->second->begin() + static_cast<long>(offset + size));
        return true;
    }
    FILE* f = std::fopen((dataDir_ + "/" + name).c_str(), "rb");
    if (!f) return false;
    out->resize(size);
    bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(out->data(), 1, size, f) == size;
    std::fclose(f);
    return ok;
}

} // namespace dd::install
