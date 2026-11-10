// Menu primitives on a virtual 640x360 canvas, integer scaled.
#include <algorithm>
#include <cmath>

#include "ui/ui.h"

namespace dd::ui {

using gfx::PackColor;

namespace col {
uint32_t Rgb(int r, int g, int b, float a) { return PackColor(r / 255.0f, g / 255.0f, b / 255.0f, a); }
// ColourManager.arne*
const uint32_t Red = Rgb(190, 38, 51), Green = Rgb(163, 206, 39), Blue = Rgb(49, 162, 242), Yellow = Rgb(247, 226, 107),
               Orange = Rgb(235, 137, 49), Grey = Rgb(157, 157, 157), DarkGrey = Rgb(101, 109, 113),
               Panel = Rgb(27, 38, 50, 0.94f), PanelDark = Rgb(20, 12, 28, 0.96f), Shade = Rgb(0, 0, 0, 0.6f),
               Gold = Rgb(247, 226, 107), Purple = Rgb(153, 100, 249);
} // namespace col

void Ui::Begin() {
    r_.Begin2D();
    float ow = static_cast<float>(r_.outW()), oh = static_cast<float>(r_.outH());
    // the largest integer scale that keeps at least 640x360 (x1 on the Vita's 960x544, which then has
    // more room: the canvas grows to the screen instead of letterboxing)
    // whole multiples where they fit (PC); the Vita's 960x544 is 1.5x, the same factor as the 3D frame
    float fit = std::min(ow / 640.0f, oh / 360.0f);
    s_ = fit >= 2 ? std::floor(fit) : std::max(1.0f, fit);
    w_ = std::floor(ow / s_);
    h_ = std::floor(oh / s_);
    ox_ = std::floor((ow - w_ * s_) * 0.5f);
    oy_ = std::floor((oh - h_ * s_) * 0.5f);
}

void Ui::Fill(float x, float y, float w, float h, uint32_t color) { r_.Fill(ox_ + x * s_, oy_ + y * s_, w * s_, h * s_, color); }

void Ui::Frame(float x, float y, float w, float h, uint32_t color, float t) {
    Fill(x, y, w, t, color);
    Fill(x, y + h - t, w, t, color);
    Fill(x, y + t, t, h - 2 * t, color);
    Fill(x + w - t, y + t, t, h - 2 * t, color);
}

void Ui::Panel(float x, float y, float w, float h, uint32_t fill, uint32_t border) {
    Fill(x, y, w, h, fill);
    Frame(x, y, w, h, border);
}

void Ui::Shade(float alpha) { Fill(0, 0, w_, h_, PackColor(0, 0, 0, alpha)); }

void Ui::SpriteTex(const gfx::Texture& tex, const gfx::SpriteDef& d, float x, float y, float w, float h, uint32_t color) {
    if (!tex.id || d.w <= 0 || d.h <= 0) return;
    // fit, keeping the aspect; whole multiples of the pixel art when it is smaller than the box
    float k = std::min(w / d.w, h / d.h);
    if (k >= 1) k = std::floor(k);
    float dw = d.w * k, dh = d.h * k;
    float px = std::floor(x + (w - dw) * 0.5f), py = std::floor(y + (h - dh) * 0.5f);
    float u0 = d.x / tex.w, u1 = (d.x + d.w) / tex.w;
    float v1 = d.y / tex.h, v0 = (d.y + d.h) / tex.h; // bottom-up texture
    r_.Rect(tex, ox_ + px * s_, oy_ + py * s_, dw * s_, dh * s_, u0, v0, u1, v1, color);
}

void Ui::Quad(const gfx::Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color) {
    r_.Rect(tex, ox_ + x * s_, oy_ + y * s_, w * s_, h * s_, u0, v0, u1, v1, color);
}

void Ui::Radial(const gfx::Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color,
                float amount, int origin, bool clockwise) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (amount <= 0) return;
    if (amount >= 1) return Quad(tex, x, y, w, h, u0, v0, u1, v1, color);
    // angles on screen (y down): 0 is up, clockwise positive
    static const float kStart[4] = {3.14159265f, 1.5707963f, 0.0f, -1.5707963f}; // bottom, right, top, left
    float a0 = kStart[origin & 3], sweep = amount * 6.2831853f * (clockwise ? 1.0f : -1.0f);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    auto edge = [&](float a, float* px, float* py) { // the rect's border along the ray from the centre
        float dx = std::sin(a), dy = -std::cos(a);
        float tx = std::fabs(dx) > 1e-6f ? (w * 0.5f) / std::fabs(dx) : 1e9f;
        float ty = std::fabs(dy) > 1e-6f ? (h * 0.5f) / std::fabs(dy) : 1e9f;
        float t = std::min(tx, ty);
        *px = cx + dx * t;
        *py = cy + dy * t;
    };
    auto push = [&](std::vector<float>& v, float px, float py) {
        v.push_back(ox_ + px * s_);
        v.push_back(oy_ + py * s_);
        v.push_back(u0 + (u1 - u0) * (px - x) / w);
        v.push_back(v0 + (v1 - v0) * (py - y) / h);
    };
    const int steps = 48;
    std::vector<float> v;
    float px0, py0;
    edge(a0, &px0, &py0);
    int n = std::max(1, static_cast<int>(std::ceil(steps * amount)));
    for (int i = 1; i <= n; i++) {
        float px1, py1;
        edge(a0 + sweep * static_cast<float>(i) / static_cast<float>(n), &px1, &py1);
        push(v, cx, cy);
        push(v, px0, py0);
        push(v, px1, py1);
        px0 = px1, py0 = py1;
    }
    r_.Tris(tex, v.data(), static_cast<int>(v.size() / 4), color);
}

void Ui::RotatedQuad(const gfx::Texture& tex, float cx, float cy, const float corners[4][2], const float uv[4][2], float cs, float sn, uint32_t color) {
    float p[4][4];
    for (int i = 0; i < 4; i++) {
        float x = corners[i][0] * cs - corners[i][1] * sn, y = corners[i][0] * sn + corners[i][1] * cs;
        p[i][0] = ox_ + (cx + x) * s_;
        p[i][1] = oy_ + (cy + y) * s_;
        p[i][2] = uv[i][0];
        p[i][3] = uv[i][1];
    }
    float v[6 * 4];
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 4; k++) v[i * 4 + k] = p[order[i]][k];
    r_.Tris(tex, v, 6, color);
}

void Ui::Clip(float x, float y, float w, float h) {
    if (w <= 0 || h <= 0) r_.Clip(0, 0, 0, 0);
    else r_.Clip(std::floor(ox_ + x * s_), std::floor(oy_ + y * s_), std::ceil(w * s_), std::ceil(h * s_));
}

FontSize Ui::FontFor(const std::string& fontName) {
    // the installer rasterises these three (install/export_fonts.cpp)
    if (fontName == "CompassGold") return Large;
    if (fontName == "SinsGold") return Medium;
    return Small; // EffortsPro and the rest
}

bool Ui::Sprite(const std::string& name, float x, float y, float w, float h, uint32_t color) {
    if (name.empty()) return false;
    const gfx::SpriteDef* d = assets_.Sprite(name);
    if (!d) return false;
    SpriteTex(assets_.Tex(d->texture), *d, x, y, w, h, color);
    return true;
}

float Ui::LineHeight(FontSize f) const { return static_cast<float>(F(f).height); }

float Ui::Width(FontSize f, const std::string& s) const { return F(f).Width(StripTags(s), 1); }

namespace {
uint32_t NamedColor(const std::string& v, uint32_t current) {
    if (!v.empty() && v[0] == '#') return ParseColor(v.substr(0, 7), ((current >> 24) & 255) / 255.0f);
    if (v == "white") return col::White;
    if (v == "red") return col::Red;
    if (v == "yellow") return col::Yellow;
    if (v == "orange") return col::Orange;
    if (v == "green") return col::Green;
    if (v == "grey" || v == "gray") return col::Grey;
    return current;
}
} // namespace

float Ui::Text(FontSize f, float x, float y, uint32_t color, const std::string& s, Align a, bool shadow) {
    const gfx::Font& font = F(f);
    float w = font.Width(StripTags(s), 1);
    if (a == Align::Center) x -= std::floor(w * 0.5f);
    else if (a == Align::Right) x -= w;
    x = std::floor(x);
    y = std::floor(y);
    // runs of text between colour tags
    std::vector<uint32_t> stack{color};
    std::string run;
    float cx = x;
    auto flush = [&]() {
        if (run.empty()) return;
        // the drop shadow the original's Text components have
        if (shadow) font.Draw(r_, ox_ + (cx + 1) * s_, oy_ + (y + 1) * s_, s_, PackColor(0, 0, 0, ((stack.back() >> 24) & 255) / 255.0f * 0.8f), run);
        font.Draw(r_, ox_ + cx * s_, oy_ + y * s_, s_, stack.back(), run);
        cx += font.Width(run, 1);
        run.clear();
    };
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '<') {
            size_t e = s.find('>', i);
            if (e != std::string::npos) {
                std::string tag = s.substr(i + 1, e - i - 1);
                if (tag.rfind("color=", 0) == 0) {
                    flush();
                    stack.push_back(NamedColor(tag.substr(6), stack.back()));
                    i = e;
                    continue;
                }
                if (tag == "/color") {
                    flush();
                    if (stack.size() > 1) stack.pop_back();
                    i = e;
                    continue;
                }
                if (tag == "b" || tag == "/b" || tag == "i" || tag == "/i" || tag.rfind("size", 0) == 0 || tag.rfind("/size", 0) == 0) {
                    i = e;
                    continue;
                }
            }
        }
        run += s[i];
    }
    flush();
    return w;
}

std::vector<std::string> Ui::Wrap(FontSize f, const std::string& s, float width) const {
    const gfx::Font& font = F(f);
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string test = line.empty() ? word : line + " " + word;
            if (!line.empty() && font.Width(StripTags(test), 1) > width) {
                lines.push_back(line);
                // a colour still open carries on to the next line
                size_t open = line.rfind("<color="), close = line.rfind("</color>");
                std::string carry;
                if (open != std::string::npos && (close == std::string::npos || close < open))
                    carry = line.substr(open, line.find('>', open) - open + 1);
                line = carry + word;
            } else {
                line = test;
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

float Ui::Paragraph(FontSize f, float x, float y, float width, uint32_t color, const std::string& s, Align a) {
    float lh = LineHeight(f) + 2, y0 = y;
    float ax = a == Align::Center ? x + width * 0.5f : a == Align::Right ? x + width : x;
    for (const std::string& l : Wrap(f, s, width)) {
        Text(f, ax, y, color, l, a);
        y += lh;
    }
    return y - y0;
}

void Ui::Button(float x, float y, float w, float h, const std::string& label, bool selected, bool enabled, FontSize f) {
    uint32_t fill = selected ? (enabled ? col::Rgb(190, 38, 51, 0.95f) : col::Rgb(101, 109, 113, 0.95f)) : col::Rgb(27, 38, 50, 0.9f);
    uint32_t border = selected ? col::White : col::DarkGrey;
    Panel(x, y, w, h, fill, border);
    uint32_t text = enabled ? col::White : col::Grey;
    Text(f, x + w * 0.5f, y + std::floor((h - LineHeight(f)) * 0.5f), text, label, Align::Center);
}

float Ui::Prompt(float x, float y, const std::string& buttonName, const char* buttonSprite) {
    float lh = LineHeight(Small);
    if (buttonSprite && Sprite(buttonSprite, x, y - 1, lh + 2, lh + 2)) return lh + 4;
    return Text(Small, x, y, col::Yellow, "[" + buttonName + "]") + 3;
}

void Ui::Hints(const std::vector<std::pair<std::string, std::string>>& hints) {
    float lh = LineHeight(Small);
    float y = h_ - lh - 6;
    float total = 0;
    for (const auto& [b, label] : hints) total += Width(Small, "[" + b + "]") + 3 + Width(Small, label) + 12;
    float x = std::floor((w_ - total) * 0.5f);
    Fill(0, y - 4, w_, lh + 8, PackColor(0, 0, 0, 0.55f));
    for (const auto& [b, label] : hints) {
        x += Text(Small, x, y, col::Yellow, "[" + b + "]") + 3;
        x += Text(Small, x, y, col::White, label) + 12;
    }
}

} // namespace dd::ui
