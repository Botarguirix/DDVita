// Reading Unity's own files from the user's copy of the game: SerializedFile (format 22, Unity
// 2020.3), UnityFS bundles (LZ4 / LZ4HC blocks) and type trees. Objects are read into JSON with
// the same shape UnityPy's read_typetree gives (pairs as [first, second], byte arrays as binary).
#pragma once
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dd::install {

using Bytes = std::vector<uint8_t>;
using json = nlohmann::json;

// Little / big endian reader over a byte range. Reading past the end sets failed() and returns 0.
class Reader {
public:
    Reader(const uint8_t* data, size_t size, bool bigEndian = false) : p_(data), n_(size), big_(bigEndian) {}
    size_t pos() const { return pos_; }
    size_t size() const { return n_; }
    void Seek(size_t pos) { pos_ = pos; }
    void Skip(size_t n) { pos_ += n; }
    void Align(size_t n) { pos_ = (pos_ + n - 1) / n * n; }
    void SetBigEndian(bool big) { big_ = big; }
    bool failed() const { return failed_; }
    void Fail() {
        failed_ = true;
        pos_ = n_;
    }
    const uint8_t* Ptr(size_t n); // n bytes at the cursor (advances), nullptr past the end

    uint8_t U8();
    uint16_t U16();
    uint32_t U32();
    uint64_t U64();
    int16_t I16() { return static_cast<int16_t>(U16()); }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    int64_t I64() { return static_cast<int64_t>(U64()); }
    float F32();
    double F64();
    std::string CStr(); // null-terminated

private:
    const uint8_t* p_;
    size_t n_, pos_ = 0;
    bool big_, failed_ = false;
};

// Type tree: nodes in depth-first order, as stored in the file. children[i] lists node i's children.
struct TypeTree {
    struct Node {
        int level = 0;
        std::string type, name;
        int32_t byteSize = 0;
        uint32_t metaFlag = 0;
    };
    std::vector<Node> nodes;
    std::vector<std::vector<int>> children;
    void Link(); // fills children from the levels
};

// The engine classes the installer reads (engine_types.inc), by class id; nullptr if not there.
const TypeTree* EngineType(int classId);
const std::string* CommonString(uint32_t offset);
// Unity's class name for a class id ("Texture2D"); nullptr if unknown
const char* ClassName(int classId);
// Where the class's name field is: 0 none, 1 first field, 2 further in (read the full tree)
int NameField(int classId);

struct SerializedType {
    int classId = 0;
    int16_t scriptIndex = -1;
    std::shared_ptr<TypeTree> tree; // null when the file has no type trees
};

struct ObjectInfo {
    int64_t pathId = 0;
    size_t offset = 0; // from the start of the file
    uint32_t size = 0;
    int type = 0; // index into SerializedFile::types
    int classId = 0;
};

class SerializedFile {
public:
    std::string name; // file name as other files' externals name it ("level9", "CAB-..." in bundles)
    std::string unityVersion;
    bool typeTrees = false;
    bool bigEndian = false;
    std::vector<SerializedType> types;
    std::vector<ObjectInfo> objects;
    std::vector<std::string> externals; // file names (path without folders), FileID 1 is externals[0]

    bool Parse(std::string fileName, std::shared_ptr<const Bytes> data, std::string* err);
    const ObjectInfo* Find(int64_t pathId) const;
    const uint8_t* ObjectData(const ObjectInfo& o) const { return data_->data() + o.offset; }

private:
    std::shared_ptr<const Bytes> data_;
    std::unordered_map<int64_t, size_t> index_;
};

// The files inside a UnityFS bundle: (name, contents).
bool ReadBundle(const Bytes& raw, std::vector<std::pair<std::string, Bytes>>* files, std::string* err);

// LZ4 block format (no frame). False if the input is malformed or does not fill `out` exactly.
bool Lz4Decompress(const uint8_t* src, size_t srcSize, uint8_t* out, size_t outSize);

// Reads one object with a type tree into JSON. Fields of type ManagedReferencesRegistry (always the
// last one, [SerializeReference] data) are left out, as the Python extractor does. `consumed`
// gets the bytes read: all of `size` when the tree matches the object.
bool ReadTypeTree(const TypeTree& tree, const uint8_t* data, size_t size, bool bigEndian, json* out,
                  size_t* consumed = nullptr);

} // namespace dd::install
