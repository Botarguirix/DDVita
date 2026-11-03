#include "render/gfx.h"

#include <cstdio>
#include <cstring>

#ifdef __vita__
#include <vitaGL.h>
#else
#include <SDL_opengl.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace dd::gfx {

bool Image::Load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char hdr[8];
    bool ok = std::fread(hdr, 1, 8, f) == 8 && std::memcmp(hdr, "DDTX", 4) == 0;
    if (ok) {
        w = hdr[4] | (hdr[5] << 8);
        h = hdr[6] | (hdr[7] << 8);
        rgba.resize(static_cast<size_t>(w) * h * 4);
        ok = std::fread(rgba.data(), 1, rgba.size(), f) == rgba.size();
    }
    std::fclose(f);
    return ok;
}

static int NextPow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

bool Renderer::Init(int outW, int outH, int lowW, int lowH) {
    outW_ = outW;
    outH_ = outH;
    lowW_ = lowW;
    lowH_ = lowH;
    Image frame;
    frame.w = NextPow2(lowW);
    frame.h = NextPow2(lowH);
    frame.rgba.assign(static_cast<size_t>(frame.w) * frame.h * 4, 0);
    frame_ = Upload(frame, false);
    Image white;
    white.w = white.h = 1;
    white.rgba = {255, 255, 255, 255};
    white_ = Upload(white, false);
#ifdef __vita__
    // Render straight into the frame texture: reading the back buffer back would stall the GPU.
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame_.id, 0);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, frame.w, frame.h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    return true;
}

Texture Renderer::Upload(const Image& img, bool repeat) {
    Texture t;
    t.w = img.w;
    t.h = img.h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    return t;
}

void Renderer::Free(Texture& t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t = Texture{};
}

void Renderer::SetBlend(Blend b, bool depthWrite) {
    switch (b) {
    case Blend::Opaque:
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        break;
    case Blend::Cutout:
        glDisable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        break;
    case Blend::Alpha:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.01f);
        break;
    }
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
}

void Renderer::BeginScene(const Mat4& proj, const Mat4& view, float r, float g, float b) {
#ifdef __vita__
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
#endif
    glViewport(0, 0, lowW_, lowH_);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, lowW_, lowH_);
    glClearColor(r, g, b, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
}

void Renderer::Draw(const Texture& tex, const Vertex* v, int count, Blend blend, bool depthWrite) {
    if (count <= 0) return;
    SetBlend(blend, depthWrite);
    glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : white_.id);
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &v->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &v->u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &v->rgba);
    glDrawArrays(GL_TRIANGLES, 0, count);
}

void Renderer::DrawIndexed(const Texture& tex, const Vertex* v, const uint16_t* idx, int count, Blend blend) {
    if (count <= 0) return;
    SetBlend(blend, true);
    glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : white_.id);
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &v->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &v->u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &v->rgba);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, idx);
}

void Renderer::EndScene() {
#ifdef __vita__
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
    glBindTexture(GL_TEXTURE_2D, frame_.id);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, lowW_, lowH_);
#endif
    Begin2D();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // Fit the 16:9 frame into the output, centred.
    float scale = static_cast<float>(outW_) / lowW_;
    if (lowH_ * scale > outH_) scale = static_cast<float>(outH_) / lowH_;
    float w = lowW_ * scale, h = lowH_ * scale;
    float x = (outW_ - w) * 0.5f, y = (outH_ - h) * 0.5f;
    float u1 = static_cast<float>(lowW_) / frame_.w, v1 = static_cast<float>(lowH_) / frame_.h;
    // The copied frame is bottom-up: v1 at the top.
    Rect(frame_, x, y, w, h, 0, v1, u1, 0, kWhite);
}

void Renderer::Begin2D() {
    glViewport(0, 0, outW_, outH_);
    glMatrixMode(GL_PROJECTION);
    Mat4 o = Mat4::Ortho(0, static_cast<float>(outW_), static_cast<float>(outH_), 0, -1, 1);
    glLoadMatrixf(o.m);
    glMatrixMode(GL_MODELVIEW);
    Mat4 id;
    glLoadMatrixf(id.m);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
}

void Renderer::Rect(const Texture& tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
    Vertex v[6] = {
        {x, y, 0, u0, v0, c},     {x + w, y, 0, u1, v0, c},     {x + w, y + h, 0, u1, v1, c},
        {x, y, 0, u0, v0, c},     {x + w, y + h, 0, u1, v1, c}, {x, y + h, 0, u0, v1, c},
    };
    Draw(tex, v, 6, Blend::Alpha, false);
}

void Renderer::Fill(float x, float y, float w, float h, uint32_t c) { Rect(white_, x, y, w, h, 0, 0, 1, 1, c); }

} // namespace dd::gfx
