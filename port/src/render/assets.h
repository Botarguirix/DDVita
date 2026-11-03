// Render assets exported by tools/export_render.py (data/render): textures, sprites, arenas,
// per-trial texture swaps, player sprite layers and SimpleAnimationConfig frames.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "render/gfx.h"

namespace dd::gfx {

struct SpriteDef {
    std::string texture;
    float x = 0, y = 0, w = 0, h = 0; // texture rect in pixels (bottom-left origin)
    float px = 0, py = 0;             // pivot in pixels, relative to the rect
    float ppu = 32;
    float border[4] = {0, 0, 0, 0}; // 9-slice: left, bottom, right, top (pixels)
};

// Placeholder HUD font (tools/make_font.py).
struct Font {
    Texture tex;
    int height = 13;
    struct Glyph { float x, y, w, h, advance; };
    std::map<int, Glyph> glyphs;
    float Width(const std::string& s, float scale) const;
    void Draw(Renderer& r, float x, float y, float scale, uint32_t color, const std::string& s) const;
};

struct ArenaDraw {
    std::string texture;
    float color[4] = {1, 1, 1, 1};
    std::vector<Vertex> verts;
    std::vector<uint16_t> idx;
};
struct ArenaProp {
    std::string sprite, name;
    float m[16]; // world matrix, row-major
    float color[4];
    bool flipX = false, flipY = false, billboard = false;
};
struct Arena {
    std::vector<ArenaDraw> draws;
    std::vector<ArenaProp> props;
};

struct AnimFrame {
    std::string sprite;
    float duration = 0.1f;
};

// A sprite made at runtime (SpriteMaker composites the player sprites).
struct RuntimeSprite {
    Texture tex;
    SpriteDef def;
};

class Assets {
public:
    bool Load(const std::string& dir, Renderer* r, std::string* err);

    const SpriteDef* Sprite(const std::string& name) const;
    const Texture& Tex(const std::string& name, bool repeat = false);
    const Image* Img(const std::string& name);
    const Arena& ArenaFor(FieldSize size) const { return arenas_[static_cast<int>(size)]; }
    std::string SwapForTrial(int trial, const std::string& texture) const;
    const std::vector<AnimFrame>* Animation(const std::string& name) const;
    const Font& font() const { return font_; }
    // SpriteMaker.MakeTexture: body, clothes, face and hair layered (later layers win where alpha > 0).
    RuntimeSprite MakePlayerSprite(bool male, PlayerClass cls, const std::string& body, const std::string& hair,
                                   const std::string& expression);

private:
    std::string dir_;
    Renderer* r_ = nullptr;
    std::map<std::string, SpriteDef> sprites_;
    std::map<std::string, Texture> textures_;
    std::map<std::string, std::unique_ptr<Image>> images_;
    Arena arenas_[3];
    std::vector<std::vector<std::pair<std::string, std::string>>> swaps_;
    std::map<std::string, std::string> faces_;
    std::map<std::string, std::string> clothes_[2]; // [female, male][class name]
    std::map<std::string, std::vector<AnimFrame>> anims_;
    Font font_;
};

} // namespace dd::gfx
