// What tools/extract_assets.py writes to extracted/, kept in memory: the asset MonoBehaviours
// (configs, by class), the game's scene components (by class), the localization tables of the
// bundles and the name index of every other object (pptr_index).
#pragma once
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "install/game_files.h"

namespace dd::install {

struct Extracted {
    // class -> objects as JSON with "_file", "_path_id" and "_class", sorted by m_Name like the
    // extractor's per-class folders
    std::map<std::string, std::vector<json>> configs;
    // class -> components of the game's own assemblies, in file order
    std::map<std::string, std::vector<json>> scene;
    // MonoBehaviours of the Addressables bundles (string tables, shared table data)
    std::vector<json> localization;
    // "file:pathId" -> [Unity class name, name or null] for everything but MonoBehaviours and transforms
    std::unordered_map<std::string, std::pair<std::string, json>> names;

    // "file:pathId" of a PPtr read in `file` (FileID 0: that file); empty for null
    std::string Key(const GameFiles& g, const std::string& file, const json& pptr) const;
};

// What to keep (memory on the Vita): the scene classes the exporters read (empty: all) and config
// classes they never read. Bundle objects always go to `localization`.
struct ExtractFilter {
    std::set<std::string> sceneClasses, skipConfigs;
};

bool Extract(const GameFiles& g, Extracted* out, std::string* err, const ExtractFilter* filter = nullptr);

// Python's jsonable view of a value read from a type tree: bytes as hex (up to 64) or "<n bytes>",
// unaligned byte arrays as lists of ints
json Jsonable(const json& v);

} // namespace dd::install
