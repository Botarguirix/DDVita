// Menu drawing: the game's pixel fonts, localised text (data/loc/en.json) with Unity "smart string"
// formatting, and the panels, buttons and sprites the screens are built from.
//
// Layout works in a virtual 640x360 canvas (the gameplay camera's Flashback94 resolution) scaled to
// the output: by whole multiples on PC (1920x1080 is x3), by 1.5 on the Vita's 960x544 like the 3D
// frame.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "render/assets.h"
#include "render/gfx.h"

namespace dd::ui {

// ---- text ----
using Args = std::vector<std::pair<std::string, std::string>>;
inline std::string Int(int v) { return std::to_string(v); }
// "{0.trial}", "{trial}", "{0}" and plural "{0:plural:one|many}" placeholders filled from args.
std::string Format(const std::string& pattern, const Args& args);

// Rich text: the <color=#rrggbb> / <color=name>, <b>, <i>, <size> tags of the game's texts removed.
std::string StripTags(const std::string& s);

// Localization table: "Table/Key" -> text (installer: data/loc/<code>.json).
class Loc {
public:
    bool Load(const std::string& path);
    const std::string& Get(const std::string& key, const std::string& fallback) const;
    bool Has(const std::string& key) const { return table_.count(key) != 0; }

private:
    std::map<std::string, std::string> table_;
};

// Parses "#rrggbb" ("" or malformed: white).
uint32_t ParseColor(const std::string& hex, float alpha = 1);

enum class Align { Left, Center, Right };
enum FontSize { Small = 0, Medium = 1, Large = 2 };

// The game's palette (ColourManager: "arne" colours of the original's UI)
namespace col {
constexpr uint32_t White = 0xffffffffu;
constexpr uint32_t Black = 0xff000000u;
uint32_t Rgb(int r, int g, int b, float a = 1);
extern const uint32_t Red, Green, Blue, Yellow, Orange, Grey, DarkGrey, Panel, PanelDark, Shade, Gold, Purple;
} // namespace col

class Ui {
public:
    Ui(gfx::Renderer& r, gfx::Assets& assets) : r_(r), assets_(assets) {}

    // Call once per frame before drawing menus: picks the scale for the output size.
    void Begin();
    float W() const { return w_; } // virtual canvas size
    float H() const { return h_; }
    float scale() const { return s_; }

    // ---- primitives (virtual coordinates) ----
    void Fill(float x, float y, float w, float h, uint32_t color);
    void Frame(float x, float y, float w, float h, uint32_t color, float t = 1); // outline
    // The original's dark panel with a light border.
    void Panel(float x, float y, float w, float h, uint32_t fill = col::Panel, uint32_t border = col::Grey);
    void Shade(float alpha = 0.6f); // darkens the whole screen
    // A sprite fitted into the box (keeps its aspect, pixel snapped); false when it is missing.
    bool Sprite(const std::string& name, float x, float y, float w, float h, uint32_t color = col::White);
    void SpriteTex(const gfx::Texture& tex, const gfx::SpriteDef& def, float x, float y, float w, float h, uint32_t color = col::White);
    // A textured quad in virtual coordinates with explicit UVs (no fitting)
    void Quad(const gfx::Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color);
    // Image.Type Filled with FillMethod Radial360: the part of a quad swept clockwise (or not) from
    // `origin` (0 bottom, 1 right, 2 top, 3 left) through amount * 360 degrees
    void Radial(const gfx::Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color,
                float amount, int origin, bool clockwise);
    // Clip drawing to a virtual rectangle (w <= 0: off)
    void Clip(float x, float y, float w, float h);
    // The menu font for a font asset of the game (SinsGold, CompassGold, EffortsPro...)
    static FontSize FontFor(const std::string& fontName);

    // ---- text ----
    float LineHeight(FontSize f) const;
    float Width(FontSize f, const std::string& s) const;
    // Draws one line; returns its width.
    float Text(FontSize f, float x, float y, uint32_t color, const std::string& s, Align a = Align::Left, bool shadow = true);
    // Word-wraps to width; "\n" breaks lines. Returns the lines.
    std::vector<std::string> Wrap(FontSize f, const std::string& s, float width) const;
    // Draws wrapped text; returns the height used.
    float Paragraph(FontSize f, float x, float y, float width, uint32_t color, const std::string& s, Align a = Align::Left);

    // ---- widgets ----
    // A menu button: highlighted when selected, greyed when locked.
    void Button(float x, float y, float w, float h, const std::string& label, bool selected, bool enabled = true,
                FontSize f = Medium);
    // A row of button hints at the bottom of the screen ("(X) Select  (O) Back").
    void Hints(const std::vector<std::pair<std::string, std::string>>& hints);
    // The button's prompt: the game's button sprite when it has one, else its name in brackets.
    float Prompt(float x, float y, const std::string& buttonName, const char* buttonSprite);

    gfx::Renderer& renderer() { return r_; }
    gfx::Assets& assets() { return assets_; }

private:
    gfx::Renderer& r_;
    gfx::Assets& assets_;
    float s_ = 1, ox_ = 0, oy_ = 0, w_ = 640, h_ = 360;
    const gfx::Font& F(FontSize f) const { return assets_.menuFont(f); }
};

} // namespace dd::ui
