// ECMA-335 (CLI) metadata: PE/COFF headers, the metadata root, the #~ tables and the heaps.
// Table and coded index layouts follow ECMA-335 partition II, chapters 22-24.
#include "install/dotnet.h"

#include <algorithm>
#include <cstring>

namespace dd::install::dotnet {

namespace {

// Column kinds: fixed sizes, heap indexes, table indexes (0x100 + table), coded indexes (0x200 + kind)
enum Col : int { C16 = 1, C32, CStrIdx, CGuid, CBlob };
constexpr int T(int table) { return 0x100 + table; }
enum Coded { TypeDefOrRef, HasConstant, HasCustomAttribute, HasFieldMarshal, HasDeclSecurity, MemberRefParent,
             HasSemantics, MethodDefOrRef, MemberForwarded, Implementation, CustomAttributeType, ResolutionScope,
             TypeOrMethodDef, kCodedCount };
constexpr int K(Coded c) { return 0x200 + c; }

struct CodedDef {
    int bits;
    std::vector<int> tables; // -1: unused tag
};
const CodedDef kCoded[kCodedCount] = {
    {2, {0x02, 0x01, 0x1b}},
    {2, {0x04, 0x08, 0x17}},
    {5, {0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0a, 0x00, 0x0e, 0x17, 0x14, 0x11, 0x1a, 0x1b, 0x20, 0x23, 0x26, 0x27, 0x28,
         0x2a, 0x2c, 0x2b}},
    {1, {0x04, 0x08}},
    {2, {0x02, 0x06, 0x20}},
    {3, {0x02, 0x01, 0x1a, 0x06, 0x1b}},
    {1, {0x14, 0x17}},
    {1, {0x06, 0x0a}},
    {1, {0x04, 0x06}},
    {2, {0x26, 0x23, 0x27}},
    {3, {-1, -1, 0x06, 0x0a, -1}},
    {2, {0x00, 0x1a, 0x23, 0x01}},
    {1, {0x02, 0x06}},
};

const std::vector<int> kSchema[0x2d] = {
    {C16, CStrIdx, CGuid, CGuid, CGuid},                         // 00 Module
    {K(ResolutionScope), CStrIdx, CStrIdx},                      // 01 TypeRef
    {C32, CStrIdx, CStrIdx, K(TypeDefOrRef), T(0x04), T(0x06)},  // 02 TypeDef
    {T(0x04)},                                                   // 03 FieldPtr
    {C16, CStrIdx, CBlob},                                       // 04 Field
    {T(0x06)},                                                   // 05 MethodPtr
    {C32, C16, C16, CStrIdx, CBlob, T(0x08)},                    // 06 MethodDef
    {T(0x08)},                                                   // 07 ParamPtr
    {C16, C16, CStrIdx},                                         // 08 Param
    {T(0x02), K(TypeDefOrRef)},                                  // 09 InterfaceImpl
    {K(MemberRefParent), CStrIdx, CBlob},                        // 0A MemberRef
    {C16, K(HasConstant), CBlob},                                // 0B Constant
    {K(HasCustomAttribute), K(CustomAttributeType), CBlob},      // 0C CustomAttribute
    {K(HasFieldMarshal), CBlob},                                 // 0D FieldMarshal
    {C16, K(HasDeclSecurity), CBlob},                            // 0E DeclSecurity
    {C16, C32, T(0x02)},                                         // 0F ClassLayout
    {C32, T(0x04)},                                              // 10 FieldLayout
    {CBlob},                                                     // 11 StandAloneSig
    {T(0x02), T(0x14)},                                          // 12 EventMap
    {T(0x14)},                                                   // 13 EventPtr
    {C16, CStrIdx, K(TypeDefOrRef)},                             // 14 Event
    {T(0x02), T(0x17)},                                          // 15 PropertyMap
    {T(0x17)},                                                   // 16 PropertyPtr
    {C16, CStrIdx, CBlob},                                       // 17 Property
    {C16, T(0x06), K(HasSemantics)},                             // 18 MethodSemantics
    {T(0x02), K(MethodDefOrRef), K(MethodDefOrRef)},             // 19 MethodImpl
    {CStrIdx},                                                   // 1A ModuleRef
    {CBlob},                                                     // 1B TypeSpec
    {C16, K(MemberForwarded), CStrIdx, T(0x1a)},                 // 1C ImplMap
    {C32, T(0x04)},                                              // 1D FieldRVA
    {C32, C32},                                                  // 1E EncLog
    {C32},                                                       // 1F EncMap
    {C32, C16, C16, C16, C16, C32, CBlob, CStrIdx, CStrIdx},     // 20 Assembly
    {C32},                                                       // 21 AssemblyProcessor
    {C32, C32, C32},                                             // 22 AssemblyOS
    {C16, C16, C16, C16, C32, CBlob, CStrIdx, CStrIdx, CBlob},   // 23 AssemblyRef
    {C32, T(0x23)},                                              // 24 AssemblyRefProcessor
    {C32, C32, C32, T(0x23)},                                    // 25 AssemblyRefOS
    {C32, CStrIdx, CBlob},                                       // 26 File
    {C32, C32, CStrIdx, CStrIdx, K(Implementation)},             // 27 ExportedType
    {C32, C32, CStrIdx, K(Implementation)},                      // 28 ManifestResource
    {T(0x02), T(0x02)},                                          // 29 NestedClass
    {C16, C16, K(TypeOrMethodDef), CStrIdx},                     // 2A GenericParam
    {K(MethodDefOrRef), CBlob},                                  // 2B MethodSpec
    {T(0x2a), K(TypeDefOrRef)},                                  // 2C GenericParamConstraint
};

uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

class Metadata {
public:
    bool Parse(const Bytes& pe, std::string* err) {
        pe_ = &pe;
        auto fail = [&](const char* what) {
            *err = what;
            return false;
        };
        if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') return fail("not a PE file");
        uint32_t peOff = Le32(&pe[0x3c]);
        if (peOff + 24 > pe.size() || std::memcmp(&pe[peOff], "PE\0\0", 4) != 0) return fail("bad PE header");
        uint16_t sections = Le16(&pe[peOff + 6]), optSize = Le16(&pe[peOff + 20]);
        size_t opt = peOff + 24;
        if (opt + optSize > pe.size()) return fail("bad optional header");
        uint16_t magic = Le16(&pe[opt]);
        size_t dirs = opt + (magic == 0x20b ? 112 : 96);
        if (dirs + 15 * 8 > pe.size()) return fail("no CLI header");
        uint32_t cliRva = Le32(&pe[dirs + 14 * 8]);
        size_t sec = opt + optSize;
        for (uint16_t i = 0; i < sections; i++, sec += 40) {
            if (sec + 40 > pe.size()) return fail("bad section table");
            sections_.push_back({Le32(&pe[sec + 12]), std::max(Le32(&pe[sec + 8]), Le32(&pe[sec + 16])), Le32(&pe[sec + 20])});
        }
        const uint8_t* cli = At(cliRva, 16);
        if (!cli) return fail("bad CLI header");
        uint32_t mdRva = Le32(cli + 8), mdSize = Le32(cli + 12);
        const uint8_t* md = At(mdRva, mdSize);
        if (!md || mdSize < 32 || Le32(md) != 0x424A5342) return fail("bad metadata root");
        uint32_t verLen = Le32(md + 12);
        size_t p = 16 + verLen + 2;
        if (p + 2 > mdSize) return fail("bad metadata root");
        uint16_t streams = Le16(md + p);
        p += 2;
        const uint8_t* tables = nullptr;
        size_t tablesSize = 0;
        for (uint16_t i = 0; i < streams; i++) {
            if (p + 8 > mdSize) return fail("bad stream header");
            uint32_t off = Le32(md + p), size = Le32(md + p + 4);
            const char* name = reinterpret_cast<const char*>(md + p + 8);
            size_t nameLen = strnlen(name, mdSize - p - 8);
            p += 8 + (nameLen + 4) / 4 * 4;
            if (off > mdSize || size > mdSize - off) return fail("bad stream");
            std::string n(name, nameLen);
            if (n == "#~") tables = md + off, tablesSize = size;
            else if (n == "#Strings") strings_ = md + off, stringsSize_ = size;
            else if (n == "#Blob") blobs_ = md + off, blobsSize_ = size;
            else if (n == "#-") return fail("uncompressed metadata tables are not supported");
        }
        if (!tables || !strings_ || tablesSize < 24) return fail("missing metadata streams");
        uint8_t heaps = tables[6];
        strIdx_ = heaps & 1 ? 4 : 2;
        guidIdx_ = heaps & 2 ? 4 : 2;
        blobIdx_ = heaps & 4 ? 4 : 2;
        uint64_t valid = static_cast<uint64_t>(Le32(tables + 8)) | static_cast<uint64_t>(Le32(tables + 12)) << 32;
        size_t q = 24;
        for (int t = 0; t < 64; t++) {
            if (!(valid >> t & 1)) continue;
            if (t >= 0x2d) return fail("unknown metadata table");
            if (q + 4 > tablesSize) return fail("bad table header");
            rows_[t] = Le32(tables + q);
            q += 4;
        }
        for (int t = 0; t < 0x2d; t++) {
            std::vector<int> sizes;
            for (int c : kSchema[t]) sizes.push_back(ColSize(c));
            int width = 0;
            for (int s : sizes) width += s;
            colSizes_[t] = sizes;
            rowSize_[t] = width;
            start_[t] = tables + q;
            q += static_cast<size_t>(width) * rows_[t];
        }
        if (q > tablesSize) return fail("truncated metadata tables");
        return true;
    }

    uint32_t Rows(int t) const { return rows_[t]; }
    // column `col` of row `row` (1-based) of table `t`
    uint32_t Get(int t, uint32_t row, int col) const {
        const uint8_t* p = start_[t] + static_cast<size_t>(rowSize_[t]) * (row - 1);
        for (int i = 0; i < col; i++) p += colSizes_[t][static_cast<size_t>(i)];
        return colSizes_[t][static_cast<size_t>(col)] == 2 ? Le16(p) : Le32(p);
    }
    std::string Str(uint32_t idx) const {
        if (idx >= stringsSize_) return std::string();
        const char* s = reinterpret_cast<const char*>(strings_ + idx);
        return std::string(s, strnlen(s, stringsSize_ - idx));
    }
    // a blob's bytes (after its compressed length)
    std::pair<const uint8_t*, size_t> Blob(uint32_t idx) const {
        if (!blobs_ || idx >= blobsSize_) return {nullptr, 0};
        const uint8_t* p = blobs_ + idx;
        size_t avail = blobsSize_ - idx, len, hdr;
        if ((p[0] & 0x80) == 0) len = p[0], hdr = 1;
        else if ((p[0] & 0xc0) == 0x80 && avail >= 2) len = (p[0] & 0x3f) << 8 | p[1], hdr = 2;
        else if (avail >= 4) len = static_cast<size_t>(p[0] & 0x1f) << 24 | p[1] << 16 | p[2] << 8 | p[3], hdr = 4;
        else return {nullptr, 0};
        if (hdr + len > avail) return {nullptr, 0};
        return {p + hdr, len};
    }
    // coded index -> token (table << 24 | row)
    uint32_t Token(Coded kind, uint32_t v) const {
        const CodedDef& d = kCoded[kind];
        uint32_t tag = v & ((1u << d.bits) - 1), row = v >> d.bits;
        if (tag >= d.tables.size() || d.tables[tag] < 0 || row == 0) return 0;
        return static_cast<uint32_t>(d.tables[tag]) << 24 | row;
    }

private:
    struct Section {
        uint32_t va, size, raw;
    };
    const uint8_t* At(uint32_t rva, size_t n) const {
        for (const Section& s : sections_)
            if (rva >= s.va && rva - s.va < s.size) {
                size_t off = s.raw + (rva - s.va);
                return off + n <= pe_->size() ? pe_->data() + off : nullptr;
            }
        return nullptr;
    }
    int ColSize(int c) const {
        if (c == C16) return 2;
        if (c == C32) return 4;
        if (c == CStrIdx) return strIdx_;
        if (c == CGuid) return guidIdx_;
        if (c == CBlob) return blobIdx_;
        if (c >= 0x200) {
            const CodedDef& d = kCoded[c - 0x200];
            uint32_t maxRows = 0;
            for (int t : d.tables)
                if (t >= 0) maxRows = std::max(maxRows, rows_[t]);
            return maxRows < (1u << (16 - d.bits)) ? 2 : 4;
        }
        return rows_[c - 0x100] < 0x10000 ? 2 : 4;
    }

    const Bytes* pe_ = nullptr;
    std::vector<Section> sections_;
    const uint8_t *strings_ = nullptr, *blobs_ = nullptr;
    size_t stringsSize_ = 0, blobsSize_ = 0;
    int strIdx_ = 2, guidIdx_ = 2, blobIdx_ = 2;
    uint32_t rows_[64] = {};
    std::vector<int> colSizes_[0x2d];
    int rowSize_[0x2d] = {};
    const uint8_t* start_[0x2d] = {};
};

// Signature blobs (ECMA-335 II.23.2)
class SigReader {
public:
    SigReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    uint8_t Byte() {
        if (pos_ >= n_) return ok_ = false, 0;
        return p_[pos_++];
    }
    uint8_t Peek() const { return pos_ < n_ ? p_[pos_] : 0; }
    uint32_t Compressed() {
        uint8_t b = Byte();
        if ((b & 0x80) == 0) return b;
        if ((b & 0xc0) == 0x80) return static_cast<uint32_t>(b & 0x3f) << 8 | Byte();
        uint32_t v = static_cast<uint32_t>(b & 0x1f) << 24;
        v |= static_cast<uint32_t>(Byte()) << 16;
        v |= static_cast<uint32_t>(Byte()) << 8;
        return v | Byte();
    }
    uint32_t TypeDefOrRef() {
        uint32_t v = Compressed();
        static const uint32_t tables[3] = {kTypeDef, kTypeRef, kTypeSpec};
        if ((v & 3) == 3) return ok_ = false, 0;
        return tables[v & 3] << 24 | v >> 2;
    }
    void SkipCustomMods() {
        while (ok_ && (Peek() == 0x1f || Peek() == 0x20)) {
            Byte();
            TypeDefOrRef();
        }
    }
    TypeSig Type(int depth = 0) {
        TypeSig t;
        SkipCustomMods();
        t.code = Byte();
        if (depth > 32) return ok_ = false, t;
        switch (t.code) {
        case 0x0f: // PTR
        case 0x10: // BYREF
        case 0x45: // PINNED
            t.args.push_back(Type(depth + 1));
            break;
        case kValueType:
        case kClass:
            t.typeToken = TypeDefOrRef();
            break;
        case kVar:
        case kMVar:
            t.number = Compressed();
            break;
        case kArray: {
            t.args.push_back(Type(depth + 1));
            uint32_t rank = Compressed();
            (void)rank;
            uint32_t sizes = Compressed();
            for (uint32_t i = 0; i < sizes && ok_; i++) Compressed();
            uint32_t bounds = Compressed();
            for (uint32_t i = 0; i < bounds && ok_; i++) Compressed();
            break;
        }
        case kGenericInst: {
            t.valueType = Byte() == kValueType;
            t.typeToken = TypeDefOrRef();
            uint32_t count = Compressed();
            for (uint32_t i = 0; i < count && ok_; i++) t.args.push_back(Type(depth + 1));
            break;
        }
        case kSzArray:
            t.args.push_back(Type(depth + 1));
            break;
        case 0x1b: // FNPTR: not serializable; give up on this signature
            ok_ = false;
            break;
        default:
            break; // primitives, string, object, typedbyref, I, U
        }
        return t;
    }

private:
    const uint8_t* p_;
    size_t n_, pos_ = 0;
    bool ok_ = true;
};

} // namespace

std::string TypeDef::FullName(const std::vector<TypeDef>& all) const {
    if (enclosing >= 0) return all[static_cast<size_t>(enclosing)].FullName(all) + "/" + name;
    return ns.empty() ? name : ns + "." + name;
}

bool Assembly::Load(const Bytes& pe, std::string* err) {
    Metadata m;
    if (!m.Parse(pe, err)) return false;
    if (m.Rows(0x20)) name = m.Str(m.Get(0x20, 1, 7));
    for (uint32_t r = 1; r <= m.Rows(0x23); r++) assemblyRefs.push_back(m.Str(m.Get(0x23, r, 6)));
    for (uint32_t r = 1; r <= m.Rows(0x01); r++)
        typeRefs.push_back({m.Str(m.Get(0x01, r, 1)), m.Str(m.Get(0x01, r, 2)), m.Token(ResolutionScope, m.Get(0x01, r, 0))});
    for (uint32_t r = 1; r <= m.Rows(0x1b); r++) {
        auto [p, n] = m.Blob(m.Get(0x1b, r, 0));
        SigReader s(p, n);
        typeSpecs.push_back(p ? s.Type() : TypeSig{});
    }

    uint32_t typeCount = m.Rows(0x02), fieldCount = m.Rows(0x04), methodCount = m.Rows(0x06);
    types.resize(typeCount);
    std::vector<uint32_t> methodOwner(methodCount + 1, 0); // MethodDef row -> TypeDef row
    std::vector<std::pair<uint32_t, uint32_t>> fieldRows(typeCount);
    for (uint32_t r = 1; r <= typeCount; r++) {
        TypeDef& t = types[r - 1];
        t.flags = m.Get(0x02, r, 0);
        t.name = m.Str(m.Get(0x02, r, 1));
        t.ns = m.Str(m.Get(0x02, r, 2));
        t.extends = m.Token(TypeDefOrRef, m.Get(0x02, r, 3));
        uint32_t fStart = m.Get(0x02, r, 4), mStart = m.Get(0x02, r, 5);
        uint32_t fEnd = r < typeCount ? m.Get(0x02, r + 1, 4) : fieldCount + 1;
        uint32_t mEnd = r < typeCount ? m.Get(0x02, r + 1, 5) : methodCount + 1;
        fieldRows[r - 1] = {fStart, std::min(fEnd, fieldCount + 1)};
        for (uint32_t k = mStart; k < mEnd && k <= methodCount; k++) methodOwner[k] = r;
    }
    // fields, with their signatures
    std::vector<std::pair<uint32_t, uint32_t>> fieldOwner(fieldCount + 1, {0, 0}); // field row -> (type row, index)
    for (uint32_t r = 1; r <= typeCount; r++) {
        TypeDef& t = types[r - 1];
        for (uint32_t f = fieldRows[r - 1].first; f < fieldRows[r - 1].second; f++) {
            Field fd;
            fd.flags = static_cast<uint16_t>(m.Get(0x04, f, 0));
            fd.name = m.Str(m.Get(0x04, f, 1));
            auto [p, n] = m.Blob(m.Get(0x04, f, 2));
            SigReader s(p, n);
            if (p && s.Byte() == 0x06) fd.type = s.Type();
            if (!p || !s.ok()) fd.type.code = 0; // unreadable: never serialized
            fieldOwner[f] = {r, static_cast<uint32_t>(t.fields.size())};
            t.fields.push_back(std::move(fd));
        }
    }
    for (uint32_t r = 1; r <= m.Rows(0x29); r++) {
        uint32_t nested = m.Get(0x29, r, 0), outer = m.Get(0x29, r, 1);
        if (nested >= 1 && nested <= typeCount && outer >= 1 && outer <= typeCount) types[nested - 1].enclosing = static_cast<int>(outer - 1);
    }
    for (uint32_t r = 1; r <= m.Rows(0x2a); r++) {
        uint32_t owner = m.Token(TypeOrMethodDef, m.Get(0x2a, r, 2));
        if (owner >> 24 == kTypeDef && (owner & 0xffffff) <= typeCount) types[(owner & 0xffffff) - 1].genericParams++;
    }
    // custom attributes on types and fields: the attribute's type name
    auto typeName = [&](uint32_t token) -> std::string {
        uint32_t row = token & 0xffffff;
        if (token >> 24 == kTypeDef && row >= 1 && row <= typeCount) return types[row - 1].FullName(types);
        if (token >> 24 == kTypeRef && row >= 1 && row <= typeRefs.size()) {
            const TypeRef& tr = typeRefs[row - 1];
            return tr.ns.empty() ? tr.name : tr.ns + "." + tr.name;
        }
        return std::string();
    };
    for (uint32_t r = 1; r <= m.Rows(0x0c); r++) {
        uint32_t parent = m.Token(HasCustomAttribute, m.Get(0x0c, r, 0));
        uint32_t ctor = m.Token(CustomAttributeType, m.Get(0x0c, r, 1));
        std::string attr;
        if (ctor >> 24 == 0x06) {
            uint32_t row = ctor & 0xffffff;
            if (row <= methodCount && methodOwner[row]) attr = typeName(kTypeDef << 24 | methodOwner[row]);
        } else if (ctor >> 24 == 0x0a) {
            uint32_t row = ctor & 0xffffff;
            if (row >= 1 && row <= m.Rows(0x0a)) attr = typeName(m.Token(MemberRefParent, m.Get(0x0a, row, 0)));
        }
        if (attr.empty()) continue;
        uint32_t prow = parent & 0xffffff;
        if (parent >> 24 == 0x04 && prow >= 1 && prow <= fieldCount && fieldOwner[prow].first)
            types[fieldOwner[prow].first - 1].fields[fieldOwner[prow].second].attributes.push_back(attr);
        else if (parent >> 24 == kTypeDef && prow >= 1 && prow <= typeCount)
            types[prow - 1].attributes.push_back(attr);
    }
    for (size_t i = 0; i < types.size(); i++) byName_[types[i].FullName(types)] = static_cast<int>(i);
    return true;
}

int Assembly::Find(const std::string& fullName) const {
    auto it = byName_.find(fullName);
    return it == byName_.end() ? -1 : it->second;
}

} // namespace dd::install::dotnet
