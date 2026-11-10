// OpenGL fixed-function renderer (desktop GL 1.x compatibility profile or vitaGL on PS Vita).
//
// The 3D scene is drawn at the Flashback94 post-process resolution (640x360, point sampled, as the
// gameplay camera's Flashback94_PostProcess is configured) and then scaled to the output.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "render/math3d.h"

namespace dd::gfx {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba; // bottom row first (Unity UV convention)
    bool Load(const std::string& path); // .tex written by tools/export_render.py
};

struct Vertex {
    float x, y, z, u, v;
    uint32_t rgba; // byte order r, g, b, a in memory
};
inline uint32_t PackColor(float r, float g, float b, float a) {
    auto c = [](float f) { return static_cast<uint32_t>(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}
constexpr uint32_t kWhite = 0xffffffffu;

struct Texture {
    unsigned id = 0;
    int w = 0, h = 0;
};

enum class Blend { Opaque, Cutout, Alpha };

class Renderer {
public:
    bool Init(int outW, int outH, int lowW = 640, int lowH = 360);
    void Resize(int outW, int outH) { outW_ = outW; outH_ = outH; }
    Texture Upload(const Image& img, bool repeat);
    void Free(Texture& t);

    void BeginScene(const Mat4& proj, const Mat4& view, float r, float g, float b);
    void Draw(const Texture& tex, const Vertex* v, int count, Blend blend, bool depthWrite = true);
    void DrawIndexed(const Texture& tex, const Vertex* v, const uint16_t* idx, int count, Blend blend);
    void EndScene();                       // scales the low-res frame to the output
    void Begin2D();                        // pixel coordinates of the output, origin top-left
    void Rect(const Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color);
    void Fill(float x, float y, float w, float h, uint32_t color);
    // Textured triangles in output pixels (x, y, u, v per vertex; batched like Rect)
    void Tris(const Texture& tex, const float* xyuv, int vertices, uint32_t color);
    // Rect / Fill quads are batched per texture (one draw call per run instead of per glyph): drawn
    // when the texture or the GL state changes, and at the end of the frame by Flush().
    void Flush();
    // 2D clipping rectangle in output pixels (origin top-left); w <= 0 turns it off
    void Clip(float x, float y, float w, float h);
    int outW() const { return outW_; }
    int outH() const { return outH_; }
    int lowW() const { return lowW_; }
    int lowH() const { return lowH_; }

private:
    int outW_ = 0, outH_ = 0, lowW_ = 640, lowH_ = 360;
    Texture frame_;   // the low-res frame (render target on the Vita, copy of the back buffer on PC)
    Texture white_;
    unsigned fbo_ = 0, depth_ = 0;
    void SetBlend(Blend b, bool depthWrite);
    void DrawNow(const Texture& tex, const Vertex* v, int count, Blend blend, bool depthWrite);
    std::vector<Vertex> batch_;
    Texture batchTex_;
};

} // namespace dd::gfx
