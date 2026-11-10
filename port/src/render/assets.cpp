#include "render/assets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace dd::gfx {

using json = nlohmann::json;

static bool ReadJson(const std::string& path, json* out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "cannot open " + path;
        return false;
    }
    try {
        f >> *out;
    } catch (const std::exception& e) {
        *err = path + ": " + e.what();
        return false;
    }
    return true;
}

bool Assets::Load(const std::string& dir, Renderer* r, std::string* err) {
    dir_ = dir;
    r_ = r;
    json j;
    if (!ReadJson(dir + "/sprites.json", &j, err)) return false;
    for (auto& [name, v] : j.items()) {
        SpriteDef d;
        d.texture = v[0].is_null() ? "" : v[0].get<std::string>();
        d.x = v[1];
        d.y = v[2];
        d.w = v[3];
        d.h = v[4];
        d.px = v[5];
        d.py = v[6];
        d.ppu = v[7];
        if (v.size() > 8)
            for (int i = 0; i < 4; i++) d.border[i] = v[8][i];
        sprites_[name] = d;
    }
    const char* sizes[3] = {"S", "M", "L"};
    for (int s = 0; s < 3; s++) {
        if (!ReadJson(dir + "/arena_" + sizes[s] + ".json", &j, err)) return false;
        Arena& a = arenas_[s];
        for (auto& d : j["draws"]) {
            ArenaDraw ad;
            ad.texture = d["texture"].is_null() ? "" : d["texture"].get<std::string>();
            for (int i = 0; i < 4; i++) ad.color[i] = d["color"][i];
            const auto& pos = d["pos"];
            const auto& uv = d["uv"];
            uint32_t col = PackColor(ad.color[0], ad.color[1], ad.color[2], 1);
            for (size_t i = 0; i * 3 < pos.size(); i++)
                ad.verts.push_back({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2], uv[i * 2], uv[i * 2 + 1], col});
            for (auto& i : d["idx"]) ad.idx.push_back(static_cast<uint16_t>(i.get<int>()));
            a.draws.push_back(std::move(ad));
        }
        for (auto& p : j["props"]) {
            ArenaProp ap;
            ap.sprite = p["sprite"];
            ap.name = p["name"];
            for (int i = 0; i < 16; i++) ap.m[i] = p["matrix"][i];
            for (int i = 0; i < 4; i++) ap.color[i] = p["color"][i];
            ap.flipX = p["flipX"];
            ap.flipY = p["flipY"];
            ap.billboard = p["billboard"];
            a.props.push_back(ap);
        }
    }
    if (!ReadJson(dir + "/trial_swaps.json", &j, err)) return false;
    for (auto& t : j) {
        std::vector<std::pair<std::string, std::string>> v;
        for (auto& s : t["swaps"])
            if (!s[0].is_null() && !s[1].is_null()) v.emplace_back(s[0], s[1]);
        swaps_.push_back(std::move(v));
    }
    if (!ReadJson(dir + "/player_look.json", &j, err)) return false;
    for (auto& [k, v] : j["faces"].items()) faces_[k] = v;
    for (auto& [k, v] : j["female"]["clothes"].items()) clothes_[0][k] = v;
    for (auto& [k, v] : j["male"]["clothes"].items()) clothes_[1][k] = v;
    if (!ReadJson(dir + "/animations.json", &j, err)) return false;
    for (auto& [name, frames] : j.items())
        for (auto& f : frames)
            anims_[name].push_back({f[0].is_null() ? "" : f[0].get<std::string>(), f[1].get<float>()});
    if (!LoadFont("font", &font_, err)) return false; // data/render/font.tex
    const char* names[3] = {"font_s", "font_m", "font_l"};
    for (int i = 0; i < 3; i++) {
        std::string ignored;
        if (!LoadFont(names[i], &menuFonts_[i], &ignored)) std::fprintf(stderr, "no %s: menus use the HUD font\n", names[i]);
    }
    return true;
}

bool Assets::LoadFont(const std::string& name, Font* f, std::string* err) {
    json j;
    if (!ReadJson(dir_ + "/" + name + ".json", &j, err)) return false;
    f->height = j["height"];
    f->ascent = j.value("ascent", f->height);
    for (auto& [code, g] : j["glyphs"].items()) f->glyphs[std::stoi(code)] = {g[0], g[1], g[2], g[3], g[4]};
    // the glyphs again with a transparent texel around each: drawn bilinear at fractional scales (the
    // Vita's 1.5) a glyph then blends into nothing instead of its neighbours in the atlas
    const Image* src = Img("../" + name);
    if (!src) return false;
    Image img;
    img.w = 1024;
    int x = 0, y = 0, rowH = 0;
    std::vector<std::pair<int, int>> at; // where each glyph goes (in glyphs order)
    for (auto& [code, g] : f->glyphs) {
        int w = static_cast<int>(g.w) + 2, h = static_cast<int>(g.h) + 2;
        if (x + w > img.w) x = 0, y += rowH, rowH = 0;
        at.emplace_back(x, y);
        x += w;
        rowH = std::max(rowH, h);
    }
    img.h = 1;
    while (img.h < y + rowH) img.h <<= 1;
    img.rgba.assign(static_cast<size_t>(img.w) * img.h * 4, 0);
    size_t i = 0;
    for (auto& [code, g] : f->glyphs) {
        auto [dx, dy] = at[i++];
        int gx = static_cast<int>(g.x), gy = static_cast<int>(g.y), gw = static_cast<int>(g.w), gh = static_cast<int>(g.h);
        for (int r = 0; r < gh; r++) {
            if (gy + r < 0 || gy + r >= src->h || gx < 0 || gx + gw > src->w) continue;
            std::memcpy(&img.rgba[(static_cast<size_t>(dy + 1 + r) * img.w + dx + 1) * 4], &src->rgba[(static_cast<size_t>(gy + r) * src->w + gx) * 4],
                        static_cast<size_t>(gw) * 4);
        }
        g.x = static_cast<float>(dx + 1);
        g.y = static_cast<float>(dy + 1);
    }
    f->tex = r_->Upload(img, false);
    return f->tex.id != 0;
}

int NextCodePoint(const std::string& s, size_t* i) {
    unsigned char c = static_cast<unsigned char>(s[(*i)++]);
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (!extra || *i + extra > s.size()) return c;
    int cp = c & (0x3f >> extra);
    for (int k = 0; k < extra; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[(*i)++]) & 0x3f);
    return cp;
}

float Font::Width(const std::string& s, float scale) const {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        auto it = glyphs.find(NextCodePoint(s, &i));
        if (it != glyphs.end()) w += it->second.advance * scale;
    }
    return w;
}

void Font::Draw(Renderer& r, float x, float y, float scale, uint32_t color, const std::string& s) const {
    for (size_t i = 0; i < s.size();) {
        auto it = glyphs.find(NextCodePoint(s, &i));
        if (it == glyphs.end()) continue;
        const Glyph& g = it->second;
        if (it->first == ' ') {
            x += g.advance * scale;
            continue;
        }
        // a hair inside the cell: at non-integer scales the edge texels of the neighbouring glyphs bleed in
        const float e = 0.0f;
        float u0 = (g.x + e) / tex.w, u1 = (g.x + g.w - e) / tex.w;
        float v1 = (g.y + e) / tex.h, v0 = (g.y + g.h - e) / tex.h; // texture is bottom-up
        r.Rect(tex, x, y, g.w * scale, g.h * scale, u0, v0, u1, v1, color);
        x += g.advance * scale;
    }
}

const SpriteDef* Assets::Sprite(const std::string& name) const {
    auto it = sprites_.find(name);
    return it == sprites_.end() ? nullptr : &it->second;
}

const Image* Assets::Img(const std::string& name) {
    auto it = images_.find(name);
    if (it != images_.end()) return it->second.get();
    auto img = std::make_unique<Image>();
    if (!img->Load(dir_ + "/tex/" + name + ".tex")) {
        std::fprintf(stderr, "missing texture %s\n", name.c_str());
        img.reset();
    }
    return (images_[name] = std::move(img)).get();
}

const Texture& Assets::Tex(const std::string& name, bool repeat) {
    std::string key = name + (repeat ? "#r" : "");
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    Texture t;
    if (const Image* img = Img(name)) t = r_->Upload(*img, repeat);
    return textures_[key] = t;
}

std::string Assets::SwapForTrial(int trial, const std::string& texture) const {
    if (trial < 0 || trial >= static_cast<int>(swaps_.size())) return texture;
    for (const auto& [from, to] : swaps_[trial])
        if (from == texture) return to;
    return texture;
}

const std::vector<AnimFrame>* Assets::Animation(const std::string& name) const {
    auto it = anims_.find(name);
    return it == anims_.end() ? nullptr : &it->second;
}

RuntimeSprite Assets::MakePlayerSprite(bool male, PlayerClass cls, const std::string& body, const std::string& hair,
                                       const std::string& expression) {
    RuntimeSprite rs;
    const Image* layers[4] = {Img(body), nullptr, Img(faces_[expression]), Img(hair)};
    auto& clothes = clothes_[male ? 1 : 0];
    auto it = clothes.find(ToString(cls));
    if (it != clothes.end()) layers[1] = Img(it->second);
    if (!layers[0]) return rs;
    // new Texture2D(body.height, body.height); pixel index j + k * width over every layer.
    Image out;
    out.w = out.h = layers[0]->h;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    for (const Image* l : layers) {
        if (!l) continue;
        for (int k = 0; k < out.h; k++)
            for (int jx = 0; jx < out.w; jx++) {
                size_t n = static_cast<size_t>(jx + k * out.w);
                if ((n + 1) * 4 > l->rgba.size()) continue;
                const uint8_t* c = &l->rgba[n * 4];
                if (c[3] > 0) std::copy(c, c + 4, &out.rgba[n * 4]);
            }
    }
    rs.tex = r_->Upload(out, false);
    rs.def.w = static_cast<float>(out.w);
    rs.def.h = static_cast<float>(out.h);
    rs.def.px = out.w * 0.5f; // SpriteMaker pivot (0.5, 0), 32 pixels per unit
    rs.def.py = 0;
    rs.def.ppu = 32;
    return rs;
}

} // namespace dd::gfx
