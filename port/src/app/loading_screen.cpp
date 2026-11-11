// The loading screen: the boot art (assets/loading.tex, made from the user's Steam install by
// tools/make_livearea_steam.py; a plain background without it), what is being done, and a progress
// bar with moving stripes and a ball rolling along it. Drawn while the data is installed from the
// game copy and while the data loads.
#include "app/loading_screen.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "app/platform.h"

namespace dd::app {

using namespace dd::gfx;

LoadingScreen::LoadingScreen(Renderer& r, const std::string& assetsDir) : r_(r) {
    start_ = platform::Seconds();
    Image img;
    if (img.Load(assetsDir + "/loading.tex")) bg_ = r_.Upload(img, false);
    // the HUD font shipped in the VPK (the game's own fonts come with the data)
    std::ifstream f(assetsDir + "/font.json");
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    Image ft;
    if (j.is_object() && ft.Load(assetsDir + "/font.tex")) {
        font_.height = j["height"];
        font_.ascent = j.value("ascent", font_.height);
        for (auto& [code, g] : j["glyphs"].items()) font_.glyphs[std::stoi(code)] = {g[0], g[1], g[2], g[3], g[4]};
        font_.tex = r_.Upload(ft, false);
        font_.tex.pixel = true;
    }
}

LoadingScreen::~LoadingScreen() {
    r_.Free(bg_);
    r_.Free(font_.tex);
}

void LoadingScreen::Draw(const std::string& msg, float fraction, bool force) {
    double now = platform::Seconds();
    if (!force && now - last_ < 1.0 / 30) return; // the install calls this very often
    last_ = now;
    float t = static_cast<float>(now - start_);
    const float W = static_cast<float>(r_.outW()), H = static_cast<float>(r_.outH());
    r_.Begin2D();
    r_.SetPixelFonts(true); // (the app sets its own choice once it runs)
    r_.Fill(0, 0, W, H, PackColor(0.07f, 0.05f, 0.1f, 1));
    if (bg_.id) {
        // cover the screen, bottom-up texture
        float k = std::max(W / bg_.w, H / bg_.h), w = bg_.w * k, h = bg_.h * k;
        r_.Rect(bg_, (W - w) * 0.5f, (H - h) * 0.5f, w, h, 0, 1, 1, 0, kWhite);
    }
    // the strip at the bottom
    float sh = 64, sy = H - sh;
    r_.Fill(0, sy, W, sh, PackColor(0.04f, 0.03f, 0.07f, 0.85f));
    r_.Fill(0, sy, W, 2, PackColor(0.97f, 0.89f, 0.42f, 1));
    float scale = 2;
    auto text = [&](float x, float y, uint32_t c, const std::string& s) {
        if (font_.tex.id) font_.Draw(r_, x, y, scale, c, s);
    };
    auto width = [&](const std::string& s) { return font_.tex.id ? font_.Width(s, scale) : 0.0f; };
    // the message, with dots that come and go
    std::string dots(static_cast<size_t>(t * 2.5f) % 4, '.');
    float bx = 40, bw = W - 80, by = sy + 40, bh = 10;
    text(bx, sy + 12, kWhite, msg + dots);
    bool determinate = fraction >= 0;
    if (determinate) {
        std::string pct = std::to_string(static_cast<int>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * 100))) + "%";
        text(bx + bw - width(pct), sy + 12, PackColor(0.97f, 0.89f, 0.42f, 1), pct);
    }
    // the bar: its frame, the fill with stripes moving along, the ball rolling at its end
    r_.Fill(bx - 2, by - 2, bw + 4, bh + 4, PackColor(0, 0, 0, 1));
    r_.Fill(bx, by, bw, bh, PackColor(0.16f, 0.12f, 0.22f, 1));
    float f0 = 0, f1 = determinate ? std::clamp(fraction, 0.0f, 1.0f) : 1;
    if (!determinate) { // a block sweeping back and forth
        float p = 0.5f + 0.5f * std::sin(t * 2.2f);
        f0 = p * 0.75f, f1 = f0 + 0.25f;
    }
    float fx = bx + bw * f0, fw = bw * (f1 - f0);
    uint32_t fill = PackColor(0.75f, 0.15f, 0.2f, 1), stripe = PackColor(0.92f, 0.33f, 0.27f, 1);
    r_.Fill(fx, by, fw, bh, fill);
    const float period = 24, sw = 10;
    for (float x = std::fmod(t * 40.0f, period) - period; x < fw; x += period) {
        float a = std::max(0.0f, x), b = std::min(fw, x + sw);
        if (b > a) r_.Fill(fx + a, by, b - a, bh, stripe);
    }
    // the ball (a blue disc, as in the game), bouncing on the bar's end
    float cx = fx + fw, bounce = std::abs(std::sin(t * 6.0f)) * 10, r = 9;
    float cy = by - r - 2 - bounce;
    for (float dy = -r; dy <= r; dy += 1) {
        float half = std::sqrt(std::max(0.0f, r * r - dy * dy));
        r_.Fill(cx - half, cy + dy, half * 2, 1, PackColor(0.19f, 0.64f, 0.95f, 1));
    }
    r_.Fill(cx - 4, cy - 5, 3, 3, kWhite);
    r_.Flush();
    platform::Swap();
}

} // namespace dd::app
