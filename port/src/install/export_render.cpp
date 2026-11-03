#include "install/export_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "install/export_data.h"

namespace dd::install {

namespace {

// Unity TextureFormat values
enum TexFormat { kAlpha8 = 1, kARGB4444 = 2, kRGB24 = 3, kRGBA32 = 4, kARGB32 = 5, kDXT1 = 10, kDXT5 = 12, kBGRA32 = 14 };
constexpr int kMaxPixels = 1024 * 1024; // larger textures are named but not written

const json& At(const json& o, const char* k) {
    static const json null;
    if (!o.is_object()) return null;
    auto it = o.find(k);
    return it == o.end() ? null : *it;
}
double Num(const json& v) { return v.is_number() ? v.get<double>() : 0.0; }
int64_t PathId(const json& pptr) { return At(pptr, "m_PathID").is_number() ? At(pptr, "m_PathID").get<int64_t>() : 0; }

std::string ShortClass(const std::string& full) {
    size_t dot = full.find_last_of('.');
    return dot == std::string::npos ? full : full.substr(dot + 1);
}

// Pillow's BC1 colour block (BC3 colour blocks always use the four-colour mode)
void DecodeBc1Colour(const uint8_t* src, bool fourColours, uint8_t out[16][4]) {
    uint16_t c0 = static_cast<uint16_t>(src[0] | src[1] << 8), c1 = static_cast<uint16_t>(src[2] | src[3] << 8);
    uint32_t lut = static_cast<uint32_t>(src[4] | src[5] << 8 | src[6] << 16) | static_cast<uint32_t>(src[7]) << 24;
    auto rgb565 = [](uint16_t v, int* c) {
        int r = (v & 0xf800) >> 8, g = (v & 0x07e0) >> 3, b = (v & 0x001f) << 3;
        c[0] = r | r >> 5, c[1] = g | g >> 6, c[2] = b | b >> 5;
    };
    int p[4][4], a[3], b[3];
    rgb565(c0, a), rgb565(c1, b);
    for (int i = 0; i < 3; i++) {
        p[0][i] = a[i], p[1][i] = b[i];
        if (c0 > c1 || fourColours) p[2][i] = (2 * a[i] + b[i]) / 3, p[3][i] = (a[i] + 2 * b[i]) / 3;
        else p[2][i] = (a[i] + b[i]) / 2, p[3][i] = 0;
    }
    p[0][3] = p[1][3] = p[2][3] = 255;
    p[3][3] = c0 > c1 || fourColours ? 255 : 0;
    for (int n = 0; n < 16; n++)
        for (int i = 0; i < 4; i++) out[n][i] = static_cast<uint8_t>(p[(lut >> (2 * n)) & 3][i]);
}

void DecodeBc3Alpha(const uint8_t* src, uint8_t out[16][4]) {
    int a0 = src[0], a1 = src[1], a[8] = {a0, a1};
    if (a0 > a1)
        for (int i = 1; i <= 6; i++) a[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else {
        for (int i = 1; i <= 4; i++) a[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        a[6] = 0, a[7] = 255;
    }
    uint32_t lut1 = static_cast<uint32_t>(src[2] | src[3] << 8 | src[4] << 16);
    uint32_t lut2 = static_cast<uint32_t>(src[5] | src[6] << 8 | src[7] << 16);
    for (int n = 0; n < 8; n++) out[n][3] = static_cast<uint8_t>(a[(lut1 >> (3 * n)) & 7]);
    for (int n = 0; n < 8; n++) out[8 + n][3] = static_cast<uint8_t>(a[(lut2 >> (3 * n)) & 7]);
}

// 3x4 affine part of a Unity transform as a 4x4 matrix (Python lists of rows)
using Mat = std::array<std::array<double, 4>, 4>;

Mat LocalMatrix(const json& tr) {
    const json &q = At(tr, "m_LocalRotation"), &s = At(tr, "m_LocalScale"), &p = At(tr, "m_LocalPosition");
    double x = Num(At(q, "x")), y = Num(At(q, "y")), z = Num(At(q, "z")), w = Num(At(q, "w"));
    double r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                      {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                      {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    double sc[3] = {Num(At(s, "x")), Num(At(s, "y")), Num(At(s, "z"))};
    double pos[3] = {Num(At(p, "x")), Num(At(p, "y")), Num(At(p, "z"))};
    Mat m{};
    for (int i = 0; i < 3; i++) m[i] = {r[i][0] * sc[0], r[i][1] * sc[1], r[i][2] * sc[2], pos[i]};
    m[3] = {0, 0, 0, 1};
    return m;
}

Mat Mul(const Mat& a, const Mat& b) {
    Mat m{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0; // sum() from 0, left to right
            for (int k = 0; k < 4; k++) s += a[i][k] * b[k][j];
            m[i][j] = s;
        }
    return m;
}

struct Mesh {
    std::vector<std::array<double, 3>> verts;
    std::vector<std::array<double, 2>> uvs;
    std::vector<std::vector<int64_t>> subs;
};

double HalfToDouble(uint16_t h) {
    int e = (h >> 10) & 31, m = h & 1023;
    double v = e == 0 ? std::ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(m + 1024, e - 25);
    return h & 0x8000 ? -v : v;
}

class Exporter {
public:
    Exporter(const GameFiles& g, const Extracted& ex, std::string out) : g_(g), ex_(ex), out_(std::move(out)) {}

    bool Run(std::string* err, const std::function<void(int, int)>& progress) {
        if (!MakeDirs(out_ + "/tex")) return *err = "cannot create " + out_ + "/tex", false;
        std::vector<ObjRef> textures = g_.Objects(28), spriteObjs = g_.Objects(213);
        for (size_t i = 0; i < textures.size(); i++) {
            if (!Texture(textures[i], err)) return false;
            if (progress) progress(static_cast<int>(i + 1), static_cast<int>(textures.size()));
        }
        json sprites = json::object();
        for (ObjRef o : spriteObjs) {
            json s;
            if (!g_.Read(o, &s)) continue;
            const json& rd = At(s, "m_RD");
            json tex = nullptr;
            if (ObjRef t = g_.Resolve(*o.file, At(rd, "texture"))) {
                if (!Texture(t, err)) return false;
                tex = texNames_[t.obj];
            }
            const json &tr = At(rd, "textureRect"), &off = At(rd, "textureRectOffset"), &rect = At(s, "m_Rect");
            const json &piv = At(s, "m_Pivot"), &b = At(s, "m_Border");
            // m_Pivot is normalised over m_Rect; the packed textureRect may be trimmed (textureRectOffset)
            double px = Num(At(piv, "x")) * Num(At(rect, "width")) - Num(At(off, "x"));
            double py = Num(At(piv, "y")) * Num(At(rect, "height")) - Num(At(off, "y"));
            std::string name = At(s, "m_Name").is_string() ? At(s, "m_Name").get<std::string>() : std::string();
            if (sprites.contains(name)) continue;
            sprites[name] = {tex,
                             At(tr, "x"),
                             At(tr, "y"),
                             At(tr, "width"),
                             At(tr, "height"),
                             RoundTo(px, 4),
                             RoundTo(py, 4),
                             At(s, "m_PixelsToUnits"),
                             json::array({At(b, "x"), At(b, "y"), At(b, "z"), At(b, "w")})};
        }
        if (!Write("sprites.json", sprites.dump(), err)) return false;
        for (auto [size, level] : {std::pair{"S", "level9"}, {"M", "level8"}, {"L", "level7"}})
            if (!Arena(size, level, err)) return false;
        return Configs(err);
    }

private:
    const GameFiles& g_;
    const Extracted& ex_;
    std::string out_;
    std::unordered_map<const ObjectInfo*, std::string> texNames_;
    std::set<std::string> written_;
    std::map<const ObjectInfo*, Mesh> meshes_;

    bool Write(const std::string& name, const std::string& text, std::string* err) {
        if (!WriteText(out_ + "/" + name, text)) return *err = "cannot write " + name, false;
        return true;
    }

    // Names the texture (repeated names get "__1", "__2"...) and writes it once
    bool Texture(ObjRef o, std::string* err) {
        if (texNames_.count(o.obj)) return true;
        json t;
        if (!g_.Read(o, &t)) return *err = "cannot read texture in " + o.file->name, false;
        std::string name = At(t, "m_Name").is_string() ? At(t, "m_Name").get<std::string>() : std::string();
        if (name.empty()) name = "tex_" + std::to_string(o.obj->pathId);
        if (written_.count(name)) {
            int i = 1;
            while (written_.count(name + "__" + std::to_string(i))) i++;
            name += "__" + std::to_string(i);
        }
        texNames_[o.obj] = name;
        int w = static_cast<int>(Num(At(t, "m_Width"))), h = static_cast<int>(Num(At(t, "m_Height")));
        if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > kMaxPixels) return true;
        const uint8_t* data = nullptr;
        size_t size = 0;
        const json& img = At(t, "image data");
        Bytes streamed;
        if (img.is_binary() && !img.get_binary().empty()) {
            data = img.get_binary().data(), size = img.get_binary().size();
        } else {
            const json& sd = At(t, "m_StreamData");
            size = static_cast<size_t>(Num(At(sd, "size")));
            if (!At(sd, "path").is_string() ||
                !g_.ReadResource(At(sd, "path").get<std::string>(), static_cast<uint64_t>(Num(At(sd, "offset"))), size, &streamed))
                return true; // no data: named only
            data = streamed.data();
        }
        std::vector<uint8_t> rgba;
        if (!DecodeTexture(static_cast<int>(Num(At(t, "m_TextureFormat"))), data, size, w, h, &rgba)) {
            std::fprintf(stderr, "texture %s: format %d not supported\n", name.c_str(), static_cast<int>(Num(At(t, "m_TextureFormat"))));
            return true;
        }
        std::string bytes = "DDTX";
        bytes += static_cast<char>(w & 255), bytes += static_cast<char>(w >> 8);
        bytes += static_cast<char>(h & 255), bytes += static_cast<char>(h >> 8);
        bytes.append(reinterpret_cast<const char*>(rgba.data()), rgba.size());
        if (!Write("tex/" + name + ".tex", bytes, err)) return false;
        written_.insert(name);
        return true;
    }

    json TexName(const SerializedFile& from, const json& pptr, std::string* err, bool* ok) {
        ObjRef t = g_.Resolve(from, pptr);
        if (!t) return nullptr;
        if (!Texture(t, err)) *ok = false;
        return texNames_[t.obj];
    }

    bool Get(const SerializedFile& from, const json& pptr, ObjRef* r, json* v) {
        *r = g_.Resolve(from, pptr);
        return *r && g_.Read(*r, v);
    }

    // first component of a GameObject with that Unity class name
    bool Component(ObjRef go, const json& gov, const char* cls, ObjRef* r, json* v) {
        for (const json& c : At(gov, "m_Component")) {
            ObjRef cr = g_.Resolve(*go.file, At(c, "component"));
            const char* n = cr ? ClassName(cr.obj->classId) : nullptr;
            if (n && std::strcmp(n, cls) == 0) {
                *r = cr;
                return g_.Read(cr, v);
            }
        }
        return false;
    }

    bool HasScript(ObjRef go, const json& gov, const char* cls) {
        for (const json& c : At(gov, "m_Component")) {
            ObjRef cr = g_.Resolve(*go.file, At(c, "component"));
            if (cr && cr.obj->classId == 114 && ShortClass(g_.ScriptOf(cr).second) == cls) return true;
        }
        return false;
    }

    bool ActiveInHierarchy(ObjRef go, json gov) {
        ObjRef tr;
        json trv;
        bool hasTr = false;
        for (const json& c : At(gov, "m_Component")) {
            ObjRef cr = g_.Resolve(*go.file, At(c, "component"));
            if (cr && (cr.obj->classId == 4 || cr.obj->classId == 224)) hasTr = g_.Read(tr = cr, &trv);
        }
        while (true) {
            if (!At(gov, "m_IsActive").get<bool>()) return false;
            if (!hasTr || PathId(At(trv, "m_Father")) == 0) return true;
            ObjRef f;
            json fv;
            if (!Get(*tr.file, At(trv, "m_Father"), &f, &fv)) return true;
            tr = f, trv = std::move(fv);
            if (!Get(*tr.file, At(trv, "m_GameObject"), &go, &gov)) return true;
        }
    }

    Mat WorldMatrix(ObjRef tr, json trv) {
        Mat m = LocalMatrix(trv);
        while (PathId(At(trv, "m_Father"))) {
            ObjRef f;
            json fv;
            if (!Get(*tr.file, At(trv, "m_Father"), &f, &fv)) break;
            tr = f, trv = std::move(fv);
            m = Mul(LocalMatrix(trv), m);
        }
        return m;
    }

    // MeshHandler: vertex positions, UV0 and the triangles of each submesh
    const Mesh* MeshData(ObjRef o) {
        auto it = meshes_.find(o.obj);
        if (it != meshes_.end()) return &it->second;
        json m;
        if (!g_.Read(o, &m)) return nullptr;
        Mesh& out = meshes_[o.obj];
        const json& vd = At(m, "m_VertexData");
        size_t count = static_cast<size_t>(Num(At(vd, "m_VertexCount")));
        const uint8_t* data = nullptr;
        size_t dataSize = 0;
        const json& raw = At(vd, "m_DataSize");
        const json& sd = At(m, "m_StreamData");
        Bytes streamed;
        if (At(sd, "path").is_string() && !At(sd, "path").get<std::string>().empty()) {
            dataSize = static_cast<size_t>(Num(At(sd, "size")));
            if (g_.ReadResource(At(sd, "path").get<std::string>(), static_cast<uint64_t>(Num(At(sd, "offset"))), dataSize, &streamed))
                data = streamed.data();
        } else if (raw.is_binary()) {
            data = raw.get_binary().data(), dataSize = raw.get_binary().size();
        }
        static const int compSize[] = {4, 2, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4};
        const json& channels = At(vd, "m_Channels");
        // get_streams: each stream holds its channels interleaved, streams aligned to 16 bytes
        std::vector<size_t> streamOffset, stride;
        int streams = 0;
        for (const json& c : channels) streams = std::max(streams, static_cast<int>(Num(At(c, "stream"))) + 1);
        size_t offset = 0;
        for (int s = 0; s < streams; s++) {
            size_t st = 0;
            for (const json& c : channels) {
                int dim = static_cast<int>(Num(At(c, "dimension"))), fmt = static_cast<int>(Num(At(c, "format")));
                if (static_cast<int>(Num(At(c, "stream"))) == s && dim > 0 && fmt >= 0 && fmt < 12)
                    st += static_cast<size_t>((dim & 15) * compSize[fmt]);
            }
            streamOffset.push_back(offset), stride.push_back(st);
            offset = (offset + count * st + 15) & ~static_cast<size_t>(15);
        }
        auto channel = [&](size_t chn, int want, auto store) {
            if (chn >= channels.size() || !data) return;
            const json& c = channels[chn];
            int dim = static_cast<int>(Num(At(c, "dimension"))) & 15, fmt = static_cast<int>(Num(At(c, "format")));
            if (dim == 0 || fmt < 0 || fmt >= 12) return;
            size_t s = static_cast<size_t>(Num(At(c, "stream")));
            for (size_t v = 0; v < count; v++) {
                size_t at = streamOffset[s] + v * stride[s] + static_cast<size_t>(Num(At(c, "offset")));
                double vals[4] = {};
                for (int k = 0; k < std::min(dim, want); k++) {
                    const uint8_t* p = data + at + static_cast<size_t>(k * compSize[fmt]);
                    if (at + static_cast<size_t>((k + 1) * compSize[fmt]) > dataSize) return;
                    switch (fmt) {
                    case 0: { float f; std::memcpy(&f, p, 4); vals[k] = f; break; }
                    case 1: vals[k] = HalfToDouble(static_cast<uint16_t>(p[0] | p[1] << 8)); break;
                    case 2: case 6: vals[k] = p[0]; break;
                    case 3: case 7: vals[k] = static_cast<int8_t>(p[0]); break;
                    case 4: case 8: vals[k] = static_cast<uint16_t>(p[0] | p[1] << 8); break;
                    case 5: case 9: vals[k] = static_cast<int16_t>(p[0] | p[1] << 8); break;
                    case 10: { uint32_t u; std::memcpy(&u, p, 4); vals[k] = u; break; }
                    default: { int32_t i; std::memcpy(&i, p, 4); vals[k] = i; break; }
                    }
                }
                store(vals);
            }
        };
        channel(0, 3, [&](const double* v) { out.verts.push_back({v[0], v[1], v[2]}); });
        channel(4, 2, [&](const double* v) { out.uvs.push_back({v[0], v[1]}); });
        if (out.uvs.empty()) out.uvs.assign(out.verts.size(), {0, 0});
        // index buffer: 16 or 32 bit
        const json& ib = At(m, "m_IndexBuffer");
        bool wide = Num(At(m, "m_IndexFormat")) != 0;
        std::vector<int64_t> idx;
        if (ib.is_binary()) {
            const auto& b = ib.get_binary();
            for (size_t i = 0; i + (wide ? 4 : 2) <= b.size(); i += wide ? 4 : 2)
                idx.push_back(wide ? static_cast<int64_t>(b[i] | b[i + 1] << 8 | b[i + 2] << 16 | static_cast<uint32_t>(b[i + 3]) << 24)
                                   : static_cast<int64_t>(b[i] | b[i + 1] << 8));
        }
        for (const json& sm : At(m, "m_SubMeshes")) {
            size_t start = static_cast<size_t>(Num(At(sm, "firstByte"))) / (wide ? 4 : 2);
            int64_t base = static_cast<int64_t>(Num(At(sm, "baseVertex")));
            std::vector<int64_t> tris;
            for (size_t i = 0; i < static_cast<size_t>(Num(At(sm, "indexCount"))) && start + i < idx.size(); i++)
                tris.push_back(idx[start + i] + base);
            out.subs.push_back(std::move(tris));
        }
        return &out;
    }

    // (texture, colour, shader) of a material
    bool Material(ObjRef mat, json* tex, json* color, std::string* shader, std::string* err) {
        json m;
        *tex = nullptr, *color = json::array({1, 1, 1, 1}), shader->clear();
        if (!g_.Read(mat, &m)) return true;
        const json& props = At(m, "m_SavedProperties");
        bool ok = true;
        for (const json& te : At(props, "m_TexEnvs"))
            if (te.is_array() && te.size() == 2 && te[0] == "_MainTex") *tex = TexName(*mat.file, At(te[1], "m_Texture"), err, &ok);
        for (const json& c : At(props, "m_Colors"))
            if (c.is_array() && c.size() == 2 && c[0] == "_Color")
                *color = {RoundTo(Num(At(c[1], "r")), 4), RoundTo(Num(At(c[1], "g")), 4), RoundTo(Num(At(c[1], "b")), 4),
                          RoundTo(Num(At(c[1], "a")), 4)};
        ObjRef sh;
        json shv;
        if (PathId(At(m, "m_Shader")) && Get(*mat.file, At(m, "m_Shader"), &sh, &shv)) {
            const json& n = At(At(shv, "m_ParsedForm"), "m_Name");
            if (n.is_string()) *shader = n.get<std::string>();
        }
        return ok;
    }

    bool Arena(const char* size, const char* level, std::string* err) {
        const SerializedFile* f = g_.File(level);
        if (!f) return *err = std::string("no ") + level, false;
        json draws = json::array(), props = json::array();
        for (const ObjectInfo& o : f->objects) {
            ObjRef r{f, &o};
            if (o.classId == 23) { // MeshRenderer
                json mr, gov, mfv;
                ObjRef go, mf;
                if (!g_.Read(r, &mr) || !Get(*f, At(mr, "m_GameObject"), &go, &gov)) continue;
                if (!At(mr, "m_Enabled").get<bool>() || !ActiveInHierarchy(go, gov)) continue;
                if (!Component(go, gov, "MeshFilter", &mf, &mfv) || !PathId(At(mfv, "m_Mesh"))) continue;
                ObjRef meshRef = g_.Resolve(*mf.file, At(mfv, "m_Mesh"));
                const Mesh* mesh = meshRef ? MeshData(meshRef) : nullptr;
                if (!mesh) continue;
                const json& sbi = At(mr, "m_StaticBatchInfo");
                size_t first = 0, n = mesh->subs.size();
                bool world = Num(At(sbi, "subMeshCount")) != 0; // static batches are already in world space
                Mat mtx{};
                if (world) {
                    first = static_cast<size_t>(Num(At(sbi, "firstSubMesh"))), n = static_cast<size_t>(Num(At(sbi, "subMeshCount")));
                } else {
                    ObjRef tr;
                    json trv;
                    if (!Component(go, gov, "Transform", &tr, &trv)) continue;
                    mtx = WorldMatrix(tr, trv);
                }
                const json& mats = At(mr, "m_Materials");
                for (size_t k = 0; k < n; k++) {
                    if (mats.empty() || first + k >= mesh->subs.size()) break;
                    ObjRef mat = g_.Resolve(*f, mats[std::min(k, mats.size() - 1)]);
                    json tex, color;
                    std::string shader;
                    if (!Material(mat, &tex, &color, &shader, err)) return false;
                    const std::vector<int64_t>& tris = mesh->subs[first + k];
                    std::vector<int64_t> used(tris);
                    std::sort(used.begin(), used.end());
                    used.erase(std::unique(used.begin(), used.end()), used.end());
                    std::unordered_map<int64_t, size_t> remap;
                    json pos = json::array(), uv = json::array(), idx = json::array();
                    for (int64_t v : used) {
                        size_t next = remap.size();
                        remap[v] = next;
                        if (v < 0 || static_cast<size_t>(v) >= mesh->verts.size()) return *err = "bad mesh index", false;
                        std::array<double, 3> p = mesh->verts[static_cast<size_t>(v)];
                        if (!world) {
                            std::array<double, 3> q;
                            for (int i = 0; i < 3; i++) q[i] = mtx[i][0] * p[0] + mtx[i][1] * p[1] + mtx[i][2] * p[2] + mtx[i][3];
                            p = q;
                        }
                        for (double c : p) pos.push_back(RoundTo(c, 4));
                        uv.push_back(RoundTo(mesh->uvs[static_cast<size_t>(v)][0], 5));
                        uv.push_back(RoundTo(mesh->uvs[static_cast<size_t>(v)][1], 5));
                    }
                    for (int64_t v : tris) idx.push_back(remap[v]);
                    draws.push_back({{"texture", tex}, {"color", color}, {"shader", shader}, {"pos", std::move(pos)},
                                     {"uv", std::move(uv)}, {"idx", std::move(idx)}});
                }
            } else if (o.classId == 212) { // SpriteRenderer
                json sr, gov, trv, spv;
                ObjRef go, tr, sp;
                if (!g_.Read(r, &sr) || !Get(*f, At(sr, "m_GameObject"), &go, &gov)) continue;
                if (!At(sr, "m_Enabled").get<bool>() || !ActiveInHierarchy(go, gov) || !PathId(At(sr, "m_Sprite"))) continue;
                if (!Component(go, gov, "Transform", &tr, &trv) || !Get(*f, At(sr, "m_Sprite"), &sp, &spv)) continue;
                bool billboard = HasScript(go, gov, "Billboard");
                if (!billboard && PathId(At(trv, "m_Father"))) {
                    ObjRef ft, fgo;
                    json ftv, fgov;
                    if (Get(*tr.file, At(trv, "m_Father"), &ft, &ftv) && Get(*ft.file, At(ftv, "m_GameObject"), &fgo, &fgov))
                        billboard = HasScript(fgo, fgov, "Billboard");
                }
                Mat m = WorldMatrix(tr, trv);
                json matrix = json::array();
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) matrix.push_back(m[i][j]);
                const json& c = At(sr, "m_Color");
                props.push_back({{"sprite", At(spv, "m_Name")},
                                 {"matrix", std::move(matrix)},
                                 {"color", {RoundTo(Num(At(c, "r")), 4), RoundTo(Num(At(c, "g")), 4), RoundTo(Num(At(c, "b")), 4),
                                            RoundTo(Num(At(c, "a")), 4)}},
                                 {"flipX", At(sr, "m_FlipX").get<bool>()},
                                 {"flipY", At(sr, "m_FlipY").get<bool>()},
                                 {"billboard", billboard},
                                 {"name", At(gov, "m_Name")}});
            }
        }
        return Write(std::string("arena_") + size + ".json", json{{"draws", std::move(draws)}, {"props", std::move(props)}}.dump(), err);
    }

    const std::vector<json>& ConfigList(const char* cls) {
        static const std::vector<json> none;
        auto it = ex_.configs.find(cls);
        return it == ex_.configs.end() ? none : it->second;
    }

    json TexRef(const json& cfg, const json& ref, std::string* err, bool* ok) {
        const SerializedFile* f = g_.File(At(cfg, "_file").get<std::string>());
        return f ? TexName(*f, ref, err, ok) : json(nullptr);
    }

    bool Configs(std::string* err) {
        bool ok = true;
        // SwapMaterialByTrial.backgroundTextureSwapConfigs: one per trial, in name order
        std::map<std::string, const json*> swapByName;
        for (const json& c : ConfigList("BackgroundTextureSwapConfig")) swapByName[At(c, "m_Name").get<std::string>()] = &c;
        json trialSwaps = json::array();
        for (const auto& [name, c] : swapByName) {
            json pairs = json::array();
            if (!(At(*c, "skipSwaps").is_number() && At(*c, "skipSwaps").get<double>() != 0) && !(At(*c, "skipSwaps").is_boolean() && At(*c, "skipSwaps").get<bool>()))
                for (const json& s : At(*c, "textureSwaps"))
                    pairs.push_back({TexRef(*c, At(s, "originalTexture"), err, &ok), TexRef(*c, At(s, "swappedTexture"), err, &ok)});
            trialSwaps.push_back({{"name", name}, {"swaps", std::move(pairs)}});
        }
        if (!Write("trial_swaps.json", trialSwaps.dump(1), err)) return false;

        // SpriteMaker layers: body, clothes (by class), face (by expression), hair
        const std::vector<json>& generals = ConfigList("HumanPlayerGeneralConfig");
        if (generals.empty()) return *err = "no HumanPlayerGeneralConfig", false;
        const json& general = generals.front();
        json look = json::object(), bodies = json::array(), faces = json::object();
        for (const json& r : At(general, "bodyTextures")) bodies.push_back(TexRef(general, r, err, &ok));
        for (const char* k : {"Plain", "Fear", "Joy", "Tired", "Yell"})
            faces[k] = TexRef(general, At(general, (std::string("face") + k).c_str()), err, &ok);
        look["bodyTextures"] = std::move(bodies);
        look["faces"] = std::move(faces);
        for (const json& c : ConfigList("HumanPlayerConfig")) {
            json clothes = json::object(), hair = json::array();
            for (const char* k : {"Brawler", "Carrier", "Runner"})
                clothes[k] = TexRef(c, At(c, (std::string("clothesTexture") + k).c_str()), err, &ok);
            for (const json& h : At(c, "hairTextures")) hair.push_back(TexRef(c, At(h, "texture"), err, &ok));
            std::string n = At(c, "m_Name").get<std::string>();
            look[n.find("Male") != std::string::npos ? "male" : "female"] = {{"clothes", std::move(clothes)}, {"hair", std::move(hair)}};
        }
        if (!Write("player_look.json", look.dump(1), err)) return false;

        json anims = json::object();
        for (const json& c : ConfigList("SimpleAnimationConfig")) {
            json frames = json::array();
            const SerializedFile* f = g_.File(At(c, "_file").get<std::string>());
            for (const json& fr : At(c, "frames")) {
                json name = nullptr, sv;
                ObjRef s;
                if (f && Get(*f, At(fr, "sprite"), &s, &sv)) name = At(sv, "m_Name");
                frames.push_back({name, At(fr, "duration")});
            }
            anims[At(c, "m_Name").get<std::string>()] = std::move(frames);
        }
        return ok && Write("animations.json", anims.dump(1), err);
    }
};

} // namespace

double RoundTo(double x, int digits) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.*f", digits, x);
    return std::strtod(buf, nullptr);
}

bool DecodeTexture(int format, const uint8_t* data, size_t size, int width, int height, std::vector<uint8_t>* rgba) {
    size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height), n = w * h;
    rgba->assign(n * 4, 0);
    uint8_t* o = rgba->data();
    auto need = [&](size_t bytes) { return size >= bytes; };
    switch (format) {
    case kAlpha8: // Pillow "A" into RGBA: black with alpha
        if (!need(n)) return false;
        for (size_t i = 0; i < n; i++) o[i * 4 + 3] = data[i];
        return true;
    case kARGB4444:
        if (!need(n * 2)) return false;
        for (size_t i = 0; i < n; i++) {
            unsigned p = data[i * 2] | data[i * 2 + 1] << 8;
            o[i * 4] = static_cast<uint8_t>(((p >> 8) & 15) * 17), o[i * 4 + 1] = static_cast<uint8_t>(((p >> 4) & 15) * 17);
            o[i * 4 + 2] = static_cast<uint8_t>((p & 15) * 17), o[i * 4 + 3] = static_cast<uint8_t>((p >> 12) * 17);
        }
        return true;
    case kRGB24:
        if (!need(n * 3)) return false;
        for (size_t i = 0; i < n; i++) o[i * 4] = data[i * 3], o[i * 4 + 1] = data[i * 3 + 1], o[i * 4 + 2] = data[i * 3 + 2], o[i * 4 + 3] = 255;
        return true;
    case kRGBA32:
        if (!need(n * 4)) return false;
        std::memcpy(o, data, n * 4);
        return true;
    case kARGB32:
    case kBGRA32: {
        if (!need(n * 4)) return false;
        static const int argb[4] = {1, 2, 3, 0}, bgra[4] = {2, 1, 0, 3};
        const int* src = format == kARGB32 ? argb : bgra;
        for (size_t i = 0; i < n; i++)
            for (int c = 0; c < 4; c++) o[i * 4 + static_cast<size_t>(c)] = data[i * 4 + static_cast<size_t>(src[c])];
        return true;
    }
    case kDXT1:
    case kDXT5: {
        size_t bw = (w + 3) / 4, bh = (h + 3) / 4, blockSize = format == kDXT1 ? 8 : 16;
        if (!need(bw * bh * blockSize)) return false;
        for (size_t by = 0; by < bh; by++)
            for (size_t bx = 0; bx < bw; bx++) {
                const uint8_t* b = data + (by * bw + bx) * blockSize;
                uint8_t px[16][4];
                if (format == kDXT1) {
                    DecodeBc1Colour(b, false, px);
                } else {
                    DecodeBc1Colour(b + 8, true, px);
                    DecodeBc3Alpha(b, px);
                }
                for (size_t k = 0; k < 16; k++) {
                    size_t x = bx * 4 + k % 4, y = by * 4 + k / 4;
                    if (x < w && y < h) std::memcpy(o + (y * w + x) * 4, px[k], 4);
                }
            }
        return true;
    }
    default:
        return false;
    }
}

bool ExportRender(const GameFiles& g, const Extracted& ex, const std::string& outDir, std::string* err,
                  const std::function<void(int, int)>& progress) {
    return Exporter(g, ex, outDir).Run(err, progress);
}

} // namespace dd::install
