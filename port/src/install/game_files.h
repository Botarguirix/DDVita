// The user's copy of the game as the installer sees it: every serialized file of
// "Dungeon Deathball_Data" (plus the Addressables bundles), cross-file references and reading of
// objects with their type tree (embedded, engine or generated for MonoBehaviours).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "install/unity_file.h"

namespace dd::install {

struct ObjRef {
    const SerializedFile* file = nullptr;
    const ObjectInfo* obj = nullptr;
    explicit operator bool() const { return obj != nullptr; }
};

// Type trees for MonoBehaviour scripts (assembly, full class name) -> tree including the base
// MonoBehaviour fields; nullptr when the class cannot be laid out.
using ScriptTrees = std::function<const TypeTree*(const std::string& assembly, const std::string& fullName)>;

class GameFiles {
public:
    // dataDir: the "<game>_Data" folder. Loads every serialized file and the bundles in
    // StreamingAssets/aa (their files are named as inside the bundle, "CAB-...").
    bool Load(const std::string& dataDir, std::string* err);

    std::vector<std::unique_ptr<SerializedFile>> files;
    const SerializedFile* File(const std::string& name) const;
    std::vector<ObjRef> Objects(int classId) const; // every object of a class, in file order

    // The object a PPtr ({m_FileID, m_PathID}) read in `from` points to; empty if null or missing.
    ObjRef Resolve(const SerializedFile& from, const json& pptr) const;
    ObjRef Resolve(const SerializedFile& from, int fileId, int64_t pathId) const;

    void SetScriptTrees(ScriptTrees trees) { scriptTrees_ = std::move(trees); }
    // The tree an object reads with: embedded, engine, or for a MonoBehaviour its script's
    // (only the base fields when `baseOnly` or the script has no tree).
    const TypeTree* TreeFor(ObjRef o, bool baseOnly = false) const;
    bool Read(ObjRef o, json* out, bool baseOnly = false) const;
    // (assembly, namespace.class) of a MonoBehaviour's script; empty strings if unknown
    std::pair<std::string, std::string> ScriptOf(ObjRef mb) const;

    // Bytes of a resource file next to the serialized files (.resS / .resource), cached
    const Bytes* Resource(const std::string& path) const;
    void DropResources() const { resources_.clear(); } // frees the cached resource files (memory)
    // `size` bytes at `offset` of a resource file, read without caching the file (the bundles'
    // own .resS are in memory already)
    bool ReadResource(const std::string& path, uint64_t offset, size_t size, Bytes* out) const;
    const std::string& dataDir() const { return dataDir_; }

private:
    std::string dataDir_;
    std::unordered_map<std::string, size_t> byName_;
    ScriptTrees scriptTrees_;
    mutable std::unordered_map<std::string, std::unique_ptr<Bytes>> resources_;
    mutable std::unordered_map<const ObjectInfo*, std::pair<std::string, std::string>> scriptOf_;
};

// Platform file helpers (stdio and sceIo / std::filesystem)
bool ReadFile(const std::string& path, Bytes* out);
std::vector<std::string> ListDir(const std::string& dir, bool wantDirs = false); // names only
bool MakeDirs(const std::string& dir); // with its parents; true if it exists afterwards

} // namespace dd::install
