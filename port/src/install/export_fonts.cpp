// The menus' pixel fonts as tools/make_font.py draws them with Pillow: the game's TTFs at their
// native 16 px, no anti-aliasing, 16 glyphs per row in cells of the widest glyph.
#include "install/export_fonts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "install/export_data.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace dd::install {

namespace {

// ASCII, Latin-1 and a few more: Œ œ Ÿ … — ’ “ ” €
std::vector<int> Codes() {
    std::vector<int> c;
    for (int i = 32; i < 127; i++) c.push_back(i);
    for (int i = 160; i < 256; i++) c.push_back(i);
    for (int i : {0x152, 0x153, 0x178, 0x2026, 0x2014, 0x2019, 0x201C, 0x201D, 0x20AC}) c.push_back(i);
    return c;
}

bool MakeFont(const Bytes& ttf, int px, const std::string& outDir, const std::string& name, std::string* err) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
        return *err = "bad font " + name, false;
    float scale = stbtt_ScaleForMappingEmToPixels(&font, static_cast<float>(px));
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
    // FreeType's size metrics: ascender rounded up, descender down
    int ascent = static_cast<int>(std::ceil(asc * scale)), descent = -static_cast<int>(std::floor(desc * scale));
    int height = ascent + descent;
    std::vector<int> codes = Codes();
    struct Glyph {
        int index, x0, y0, x1, y1, adv;
        bool empty;
    };
    std::vector<Glyph> glyphs;
    int cellW = 1;
    for (int c : codes) {
        Glyph g{};
        g.index = stbtt_FindGlyphIndex(&font, c);
        int advance, lsb;
        stbtt_GetGlyphHMetrics(&font, g.index, &advance, &lsb);
        g.adv = static_cast<int>(std::floor(advance * scale + 0.5f));
        g.empty = stbtt_IsGlyphEmpty(&font, g.index) != 0;
        if (!g.empty) stbtt_GetGlyphBitmapBox(&font, g.index, scale, scale, &g.x0, &g.y0, &g.x1, &g.y1);
        // Pillow's getbbox right edge reaches at least the advance
        cellW = std::max(cellW, std::max(g.empty ? 0 : g.x1, g.adv) + 1);
        glyphs.push_back(g);
    }
    const int cols = 16, rows = static_cast<int>((codes.size() + cols - 1) / cols);
    int w = cols * cellW, h = rows * height;
    std::vector<uint8_t> alpha(static_cast<size_t>(w * h), 0);
    json out = json::object();
    std::vector<uint8_t> bmp;
    for (size_t i = 0; i < codes.size(); i++) {
        const Glyph& g = glyphs[i];
        if (g.empty && codes[i] != ' ' && g.adv == 0) continue; // not in the font
        int x = static_cast<int>(i % cols) * cellW, y = static_cast<int>(i / cols) * height;
        if (!g.empty) {
            int gw = g.x1 - g.x0, gh = g.y1 - g.y0;
            bmp.assign(static_cast<size_t>(gw * gh), 0);
            stbtt_MakeGlyphBitmap(&font, bmp.data(), gw, gh, gw, scale, scale, g.index);
            // the missing-glyph box has hairline strokes that FreeType's dropout control keeps
            int threshold = g.index == 0 ? 64 : 128;
            for (int r = 0; r < gh; r++)
                for (int k = 0; k < gw; k++) {
                    int tx = x + g.x0 + k, ty = y + ascent + g.y0 + r;
                    if (tx >= 0 && tx < w && ty >= 0 && ty < h && bmp[static_cast<size_t>(r * gw + k)] >= threshold)
                        alpha[static_cast<size_t>(ty * w + tx)] = 255;
                }
        }
        // rect from the bottom-left of the texture
        out[std::to_string(codes[i])] = {x, h - y - height, cellW, height, g.adv};
    }
    std::string tex = "DDTX";
    tex += static_cast<char>(w & 255), tex += static_cast<char>(w >> 8);
    tex += static_cast<char>(h & 255), tex += static_cast<char>(h >> 8);
    for (int r = h - 1; r >= 0; r--)
        for (int k = 0; k < w; k++) {
            char a = static_cast<char>(alpha[static_cast<size_t>(r * w + k)]);
            tex += '\xff', tex += '\xff', tex += '\xff', tex += a;
        }
    json meta = {{"height", height}, {"ascent", ascent}, {"glyphs", std::move(out)}};
    if (!WriteText(outDir + "/" + name + ".tex", tex) || !WriteText(outDir + "/" + name + ".json", meta.dump()))
        return *err = "cannot write " + name, false;
    return true;
}

} // namespace

bool ExportFonts(const GameFiles& g, const std::string& outDir, std::string* err) {
    // extract_fonts.py: the Font assets with embedded data in the *.assets files (later files win)
    std::vector<std::pair<std::string, Bytes>> fonts;
    for (ObjRef o : g.Objects(128)) {
        const std::string& f = o.file->name;
        if (f.size() < 7 || f.compare(f.size() - 7, 7, ".assets") != 0) continue;
        json v;
        if (!g.Read(o, &v) || !v.contains("m_FontData") || !v["m_FontData"].is_binary()) continue;
        const auto& data = v["m_FontData"].get_binary();
        if (data.empty()) continue;
        std::string n = v.value("m_Name", "");
        auto it = std::find_if(fonts.begin(), fonts.end(), [&](const auto& p) { return p.first == n; });
        if (it == fonts.end()) fonts.emplace_back(n, Bytes(data.begin(), data.end()));
        else it->second.assign(data.begin(), data.end());
    }
    for (auto [name, ttf] : {std::pair{"font_s", "EffortsPro"}, {"font_m", "SinsGold"}, {"font_l", "CompassGold"}}) {
        auto it = std::find_if(fonts.begin(), fonts.end(), [&](const auto& p) { return p.first == ttf; });
        if (it == fonts.end()) return *err = std::string("no font ") + ttf, false;
        if (!MakeFont(it->second, 16, outDir, name, err)) return false;
    }
    return true;
}

} // namespace dd::install
