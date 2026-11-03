// Unity file formats: see unity_file.h. Layouts follow Unity 2020.3 (SerializedFile format 22,
// UnityFS 6/7); the type tree reading rules are UnityPy's (TypeTreeHelper.read_value).
#include "install/unity_file.h"

#include <cstring>

namespace dd::install {

// ---------------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------------
const uint8_t* Reader::Ptr(size_t n) {
    if (pos_ > n_ || n > n_ - pos_) {
        failed_ = true;
        pos_ = n_;
        return nullptr;
    }
    const uint8_t* p = p_ + pos_;
    pos_ += n;
    return p;
}

uint8_t Reader::U8() {
    const uint8_t* p = Ptr(1);
    return p ? p[0] : 0;
}

uint16_t Reader::U16() {
    const uint8_t* p = Ptr(2);
    if (!p) return 0;
    return big_ ? static_cast<uint16_t>(p[0] << 8 | p[1]) : static_cast<uint16_t>(p[1] << 8 | p[0]);
}

uint32_t Reader::U32() {
    const uint8_t* p = Ptr(4);
    if (!p) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(p[big_ ? 3 - i : i]) << (8 * i);
    return v;
}

uint64_t Reader::U64() {
    const uint8_t* p = Ptr(8);
    if (!p) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(p[big_ ? 7 - i : i]) << (8 * i);
    return v;
}

float Reader::F32() {
    uint32_t u = U32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

double Reader::F64() {
    uint64_t u = U64();
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

std::string Reader::CStr() {
    std::string s;
    while (pos_ < n_ && p_[pos_]) s += static_cast<char>(p_[pos_++]);
    if (pos_ >= n_) failed_ = true;
    else pos_++;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Type trees
// ---------------------------------------------------------------------------------------------
void TypeTree::Link() {
    children.assign(nodes.size(), {});
    std::vector<int> stack; // parent chain
    for (size_t i = 0; i < nodes.size(); i++) {
        int level = nodes[i].level;
        while (!stack.empty() && nodes[static_cast<size_t>(stack.back())].level >= level) stack.pop_back();
        if (!stack.empty()) children[static_cast<size_t>(stack.back())].push_back(static_cast<int>(i));
        stack.push_back(static_cast<int>(i));
    }
}

namespace {

struct EngineNode {
    int level;
    const char *type, *name;
    int32_t byteSize;
    uint32_t metaFlag;
};
struct EngineClass {
    int classId;
    const char* name;
    int count;
};

const EngineNode kEngineNodes[] = {
#define ENGINE_TYPE(id, name, count)
#define NODE(level, type, name, size, flag) {level, type, name, size, flag},
#define COMMON_STRING(offset, s)
#define CLASS_INFO(id, name, mode)
#include "install/engine_types.inc"
#undef ENGINE_TYPE
#undef NODE
#undef COMMON_STRING
#undef CLASS_INFO
};

const EngineClass kEngineClasses[] = {
#define ENGINE_TYPE(id, name, count) {id, name, count},
#define NODE(level, type, name, size, flag)
#define COMMON_STRING(offset, s)
#define CLASS_INFO(id, name, mode)
#include "install/engine_types.inc"
#undef ENGINE_TYPE
#undef NODE
#undef COMMON_STRING
#undef CLASS_INFO
};

struct CommonEntry {
    uint32_t offset;
    const char* s;
};
const CommonEntry kCommonStrings[] = {
#define ENGINE_TYPE(id, name, count)
#define NODE(level, type, name, size, flag)
#define COMMON_STRING(offset, s) {offset, s},
#define CLASS_INFO(id, name, mode)
#include "install/engine_types.inc"
#undef ENGINE_TYPE
#undef NODE
#undef COMMON_STRING
#undef CLASS_INFO
};

} // namespace

const TypeTree* EngineType(int classId) {
    static std::unordered_map<int, TypeTree> trees = [] {
        std::unordered_map<int, TypeTree> m;
        size_t at = 0;
        for (const EngineClass& c : kEngineClasses) {
            TypeTree& t = m[c.classId];
            for (int i = 0; i < c.count; i++, at++) {
                const EngineNode& n = kEngineNodes[at];
                t.nodes.push_back({n.level, n.type, n.name, n.byteSize, n.metaFlag});
            }
            t.Link();
        }
        return m;
    }();
    auto it = trees.find(classId);
    return it == trees.end() ? nullptr : &it->second;
}

const std::string* CommonString(uint32_t offset) {
    static std::unordered_map<uint32_t, std::string> strings = [] {
        std::unordered_map<uint32_t, std::string> m;
        for (const CommonEntry& e : kCommonStrings) m[e.offset] = e.s;
        return m;
    }();
    auto it = strings.find(offset);
    return it == strings.end() ? nullptr : &it->second;
}

namespace {

struct ClassEntry {
    int classId;
    const char* name;
    int nameField;
};
const ClassEntry kClassInfo[] = {
#define ENGINE_TYPE(id, name, count)
#define NODE(level, type, name, size, flag)
#define COMMON_STRING(offset, s)
#define CLASS_INFO(id, name, mode) {id, name, mode},
#include "install/engine_types.inc"
#undef ENGINE_TYPE
#undef NODE
#undef COMMON_STRING
#undef CLASS_INFO
};

const ClassEntry* ClassEntryOf(int classId) {
    static std::unordered_map<int, const ClassEntry*> byId = [] {
        std::unordered_map<int, const ClassEntry*> m;
        for (const ClassEntry& e : kClassInfo) m.emplace(e.classId, &e);
        return m;
    }();
    auto it = byId.find(classId);
    return it == byId.end() ? nullptr : it->second;
}

} // namespace

const char* ClassName(int classId) {
    const ClassEntry* e = ClassEntryOf(classId);
    return e ? e->name : nullptr;
}

int NameField(int classId) {
    const ClassEntry* e = ClassEntryOf(classId);
    return e ? e->nameField : 0;
}

// ---------------------------------------------------------------------------------------------
// SerializedFile
// ---------------------------------------------------------------------------------------------
namespace {

std::string BaseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// SerializedFile.read_serialized_type: class id, script index, hashes and the type tree blob
bool ReadType(Reader& r, uint32_t version, bool typeTrees, bool isRef, SerializedType* t) {
    t->classId = r.I32();
    if (version >= 16) r.U8(); // stripped
    if (version >= 17) t->scriptIndex = r.I16();
    if (version >= 13) {
        if ((isRef && t->scriptIndex >= 0) || (version < 16 && t->classId < 0) || (version >= 16 && t->classId == 114))
            r.Skip(16); // script id
        r.Skip(16);     // old type hash
    }
    if (typeTrees) {
        int32_t count = r.I32(), strSize = r.I32();
        if (count < 0 || strSize < 0) return false;
        struct Raw {
            uint32_t typeOff, nameOff;
        };
        std::vector<Raw> raw(static_cast<size_t>(count));
        auto tree = std::make_shared<TypeTree>();
        tree->nodes.resize(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; i++) {
            TypeTree::Node& n = tree->nodes[static_cast<size_t>(i)];
            r.U16(); // version
            n.level = r.U8();
            r.U8(); // type flags
            raw[static_cast<size_t>(i)] = {r.U32(), r.U32()};
            n.byteSize = r.I32();
            r.I32(); // index
            n.metaFlag = r.U32();
            if (version >= 19) r.U64(); // ref type hash
        }
        const uint8_t* strs = r.Ptr(static_cast<size_t>(strSize));
        if (!strs) return false;
        auto str = [&](uint32_t off) -> std::string {
            if (off & 0x80000000u) {
                const std::string* s = CommonString(off & 0x7fffffffu);
                return s ? *s : std::string();
            }
            if (off >= static_cast<uint32_t>(strSize)) return std::string();
            const char* p = reinterpret_cast<const char*>(strs + off);
            return std::string(p, strnlen(p, static_cast<size_t>(strSize) - off));
        };
        for (size_t i = 0; i < raw.size(); i++) {
            tree->nodes[i].type = str(raw[i].typeOff);
            tree->nodes[i].name = str(raw[i].nameOff);
        }
        tree->Link();
        t->tree = tree;
        if (version >= 21) {
            if (isRef) {
                r.CStr();
                r.CStr();
                r.CStr();
            } else {
                int32_t deps = r.I32();
                if (deps < 0) return false;
                r.Skip(static_cast<size_t>(deps) * 4);
            }
        }
    }
    return !r.failed();
}

} // namespace

bool SerializedFile::Parse(std::string fileName, std::shared_ptr<const Bytes> data, std::string* err) {
    name = std::move(fileName);
    data_ = std::move(data);
    Reader r(data_->data(), data_->size(), true);
    r.U32(); // metadata size
    r.U32(); // file size
    uint32_t version = r.U32();
    uint64_t dataOffset = r.U32();
    if (version < 17 || version > 22) {
        *err = name + ": unsupported serialized file version " + std::to_string(version);
        return false;
    }
    bigEndian = r.U8() != 0;
    r.Skip(3);
    if (version >= 22) {
        r.U32(); // metadata size
        r.I64(); // file size
        dataOffset = r.U64();
        r.I64();
    }
    r.SetBigEndian(bigEndian);
    unityVersion = r.CStr();
    r.I32(); // target platform
    typeTrees = r.U8() != 0;
    int32_t typeCount = r.I32();
    if (typeCount < 0 || r.failed()) {
        *err = name + ": bad header";
        return false;
    }
    types.resize(static_cast<size_t>(typeCount));
    for (SerializedType& t : types)
        if (!ReadType(r, version, typeTrees, false, &t)) {
            *err = name + ": bad type table";
            return false;
        }
    int32_t objectCount = r.I32();
    if (objectCount < 0) return *err = name + ": bad object table", false;
    objects.resize(static_cast<size_t>(objectCount));
    for (size_t i = 0; i < objects.size(); i++) {
        ObjectInfo& o = objects[i];
        r.Align(4);
        o.pathId = r.I64();
        o.offset = static_cast<size_t>((version >= 22 ? r.U64() : r.U32()) + dataOffset);
        o.size = r.U32();
        o.type = r.I32();
        if (o.type < 0 || o.type >= typeCount || o.offset > data_->size() || o.size > data_->size() - o.offset) {
            *err = name + ": bad object " + std::to_string(o.pathId);
            return false;
        }
        o.classId = types[static_cast<size_t>(o.type)].classId;
        index_[o.pathId] = i;
    }
    int32_t scripts = r.I32();
    for (int32_t i = 0; i < scripts && !r.failed(); i++) {
        r.I32();
        r.Align(4);
        r.I64();
    }
    int32_t externalCount = r.I32();
    for (int32_t i = 0; i < externalCount && !r.failed(); i++) {
        r.CStr();   // temp empty
        r.Skip(16); // guid
        r.I32();    // type
        externals.push_back(BaseName(r.CStr()));
    }
    if (r.failed()) {
        *err = name + ": truncated metadata";
        return false;
    }
    return true;
}

const ObjectInfo* SerializedFile::Find(int64_t pathId) const {
    auto it = index_.find(pathId);
    return it == index_.end() ? nullptr : &objects[it->second];
}

// ---------------------------------------------------------------------------------------------
// LZ4 and UnityFS bundles
// ---------------------------------------------------------------------------------------------
bool Lz4Decompress(const uint8_t* src, size_t srcSize, uint8_t* out, size_t outSize) {
    size_t ip = 0, op = 0;
    while (ip < srcSize) {
        uint8_t token = src[ip++];
        size_t lit = token >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (ip >= srcSize) return false;
                b = src[ip++];
                lit += b;
            } while (b == 255);
        }
        if (lit > srcSize - ip || lit > outSize - op) return false;
        std::memcpy(out + op, src + ip, lit);
        ip += lit;
        op += lit;
        if (ip >= srcSize) break; // the last sequence has literals only
        if (srcSize - ip < 2) return false;
        size_t offset = src[ip] | src[ip + 1] << 8;
        ip += 2;
        if (offset == 0 || offset > op) return false;
        size_t len = token & 15;
        if (len == 15) {
            uint8_t b;
            do {
                if (ip >= srcSize) return false;
                b = src[ip++];
                len += b;
            } while (b == 255);
        }
        len += 4;
        if (len > outSize - op) return false;
        for (size_t i = 0; i < len; i++, op++) out[op] = out[op - offset]; // overlapping copies repeat
    }
    return op == outSize;
}

namespace {

bool Decompress(const uint8_t* src, size_t srcSize, size_t outSize, int method, Bytes* out) {
    out->resize(outSize);
    if (method == 0) {
        if (srcSize != outSize) return false;
        std::memcpy(out->data(), src, srcSize);
        return true;
    }
    if (method == 2 || method == 3) return Lz4Decompress(src, srcSize, out->data(), outSize);
    return false; // LZMA: not used by this game's bundles
}

} // namespace

bool ReadBundle(const Bytes& raw, std::vector<std::pair<std::string, Bytes>>* files, std::string* err) {
    Reader r(raw.data(), raw.size(), true);
    if (r.CStr() != "UnityFS") return *err = "not a UnityFS bundle", false;
    uint32_t version = r.U32();
    r.CStr(); // unity version
    r.CStr(); // revision
    r.I64();  // size
    uint32_t infoPacked = r.U32(), infoSize = r.U32(), flags = r.U32();
    if (version >= 7) r.Align(16);
    const uint8_t* infoSrc;
    if (flags & 0x80) { // blocks info at the end
        if (infoPacked > raw.size()) return *err = "bad bundle header", false;
        infoSrc = raw.data() + raw.size() - infoPacked;
    } else {
        infoSrc = r.Ptr(infoPacked);
    }
    Bytes info;
    if (!infoSrc || !Decompress(infoSrc, infoPacked, infoSize, static_cast<int>(flags & 0x3f), &info))
        return *err = "bad bundle blocks info", false;
    if (flags & 0x200) r.Align(16);

    Reader ir(info.data(), info.size(), true);
    ir.Skip(16); // hash
    struct Block {
        uint32_t size, packed;
        uint16_t flags;
    };
    std::vector<Block> blocks(static_cast<size_t>(std::max(0, ir.I32())));
    size_t total = 0;
    for (Block& b : blocks) {
        b.size = ir.U32();
        b.packed = ir.U32();
        b.flags = ir.U16();
        total += b.size;
    }
    Bytes data;
    data.reserve(total);
    for (const Block& b : blocks) {
        const uint8_t* src = r.Ptr(b.packed);
        Bytes part;
        if (!src || !Decompress(src, b.packed, b.size, b.flags & 0x3f, &part)) return *err = "bad bundle block", false;
        data.insert(data.end(), part.begin(), part.end());
    }
    int32_t nodes = ir.I32();
    for (int32_t i = 0; i < nodes && !ir.failed(); i++) {
        uint64_t offset = ir.U64(), size = ir.U64();
        ir.U32(); // flags
        std::string path = ir.CStr();
        if (offset > data.size() || size > data.size() - offset) return *err = "bad bundle node", false;
        files->emplace_back(path, Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                        data.begin() + static_cast<std::ptrdiff_t>(offset + size)));
    }
    if (ir.failed()) return *err = "truncated bundle", false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Reading objects
// ---------------------------------------------------------------------------------------------
namespace {

constexpr uint32_t kAlignBytes = 0x4000;

class TreeReader {
public:
    TreeReader(const TypeTree& t, Reader& r) : t_(t), r_(r) {}

    json Value(int i) {
        const TypeTree::Node& n = t_.nodes[static_cast<size_t>(i)];
        const std::vector<int>& kids = t_.children[static_cast<size_t>(i)];
        bool align = (n.metaFlag & kAlignBytes) != 0;
        json v;
        if (Primitive(n.type, &v)) {
        } else if (n.type == "string") {
            v = String();
        } else if (n.type == "TypelessData") {
            v = Blob(1);
        } else if (n.type == "pair" && kids.size() == 2) {
            json first = Value(kids[0]);
            v = json::array({std::move(first), Value(kids[1])});
        } else if (!kids.empty() && t_.nodes[static_cast<size_t>(kids[0])].type == "Array") {
            const std::vector<int>& arr = t_.children[static_cast<size_t>(kids[0])];
            if (t_.nodes[static_cast<size_t>(kids[0])].metaFlag & kAlignBytes) align = true;
            v = Array(arr.size() == 2 ? arr[1] : -1);
        } else {
            v = json::object();
            for (int c : kids) {
                const TypeTree::Node& cn = t_.nodes[static_cast<size_t>(c)];
                if (cn.type == "ManagedReferencesRegistry") break; // [SerializeReference] data: always last
                v[cn.name] = Value(c);
                if (r_.failed()) break;
            }
        }
        if (align) r_.Align(4);
        return v;
    }

private:
    bool Primitive(const std::string& type, json* v) {
        switch (type.size() ? type[0] : 0) {
        case 'b':
            if (type == "bool") return *v = r_.U8() != 0, true;
            break;
        case 'S':
            if (type == "SInt8") return *v = static_cast<int8_t>(r_.U8()), true;
            if (type == "SInt16") return *v = r_.I16(), true;
            if (type == "SInt32") return *v = r_.I32(), true;
            if (type == "SInt64") return *v = r_.I64(), true;
            break;
        case 'U':
            if (type == "UInt8") return *v = r_.U8(), true;
            if (type == "UInt16") return *v = r_.U16(), true;
            if (type == "UInt32") return *v = r_.U32(), true;
            if (type == "UInt64") return *v = r_.U64(), true;
            break;
        case 'c':
            if (type == "char") return *v = r_.U8(), true;
            break;
        case 's':
            if (type == "short") return *v = r_.I16(), true;
            break;
        case 'u':
            if (type == "unsigned short") return *v = r_.U16(), true;
            if (type == "unsigned int") return *v = r_.U32(), true;
            if (type == "unsigned long long") return *v = r_.U64(), true;
            break;
        case 'i':
            if (type == "int") return *v = r_.I32(), true;
            break;
        case 'l':
            if (type == "long long") return *v = r_.I64(), true;
            break;
        case 'T':
            if (type == "Type*") return *v = r_.U32(), true;
            break;
        case 'F':
            if (type == "FileSize") return *v = r_.U64(), true;
            break;
        case 'f':
            if (type == "float") return *v = static_cast<double>(r_.F32()), true;
            break;
        case 'd':
            if (type == "double") return *v = r_.F64(), true;
            break;
        }
        return false;
    }

    // read_aligned_string: the length, the bytes, then always 4-byte alignment
    std::string String() {
        int32_t len = r_.I32();
        if (len <= 0 || static_cast<size_t>(len) > r_.size() - r_.pos()) return std::string();
        const uint8_t* p = r_.Ptr(static_cast<size_t>(len));
        std::string s(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
        r_.Align(4);
        return s;
    }

    // byte arrays stay binary; subtype 1: Python gives bytes, 0: a list of ints (unaligned elements)
    json Blob(int subtype) {
        int32_t len = r_.I32();
        if (len < 0) return json::binary({});
        const uint8_t* p = r_.Ptr(static_cast<size_t>(len));
        if (!p) return json::binary({});
        return json::binary(Bytes(p, p + len), static_cast<std::uint64_t>(subtype));
    }

    json Array(int elem) {
        if (elem < 0) return json::array();
        const TypeTree::Node& e = t_.nodes[static_cast<size_t>(elem)];
        if (e.type == "UInt8" || e.type == "SInt8" || e.type == "char") return Blob((e.metaFlag & kAlignBytes) ? 1 : 0);
        int32_t size = r_.I32();
        json a = json::array();
        if (size < 0 || static_cast<size_t>(size) > r_.size()) {
            r_.Fail();
            return a;
        }
        for (int32_t k = 0; k < size && !r_.failed(); k++) a.push_back(Value(elem));
        return a;
    }

    const TypeTree& t_;
    Reader& r_;
};

} // namespace

bool ReadTypeTree(const TypeTree& tree, const uint8_t* data, size_t size, bool bigEndian, json* out, size_t* consumed) {
    if (tree.nodes.empty()) return false;
    Reader r(data, size, bigEndian);
    TreeReader tr(tree, r);
    *out = tr.Value(0);
    if (consumed) *consumed = r.pos();
    return !r.failed();
}

} // namespace dd::install
