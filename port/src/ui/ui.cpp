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
    s_ = std::max(1.0f, std::floor(std::min(ow / 640.0f, oh / 360.0f)));
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
