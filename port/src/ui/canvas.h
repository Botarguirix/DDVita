// The original game's menus, drawn from their own Canvas trees (render/ui_*.json, exported by the
// installer from the scenes): RectTransform layout, Image (simple / sliced / filled), Text with
// Unity's alignment and its Shadow / Outline, Horizontal / Vertical / Grid layout groups,
// LayoutElement, ContentSizeFitter and Mask. The app finds nodes by path and changes their texts,
// sprites, colours and visibility; the layout is the original's.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/ui.h"

namespace dd::ui {

struct CRect {
    float x = 0, y = 0, w = 0, h = 0; // canvas units, y up (Unity), origin bottom-left
};

struct CNode {
    std::string name;
    bool active = true;
    int parent = -1;
    std::vector<int> kids;
    float amin[2] = {0, 0}, amax[2] = {1, 1}, piv[2] = {0.5f, 0.5f}, pos[2] = {0, 0}, size[2] = {0, 0}, scale[2] = {1, 1};

    // ---- Image ----
    bool hasImage = false, imageOn = true;
    std::string sprite;
    uint32_t imageColor = 0xffffffffu;
    int imageType = 0; // Simple, Sliced, Tiled, Filled
    int fillMethod = 0, fillOrigin = 0;
    float fillAmount = 1, ppum = 1;
    bool fillCenter = true, preserveAspect = false;

    // ---- Text ----
    bool hasText = false, textOn = true;
    std::string text, locKey, font;
    uint32_t textColor = 0xffffffffu, baseTextColor = 0xffffffffu; // current / as authored
    int fontSize = 0, alignment = 0, hOverflow = 0, vOverflow = 0;
    float lineSpacing = 1;
    bool rich = false;

    // ---- effects ----
    bool hasShadow = false, outline = false;
    uint32_t shadowColor = 0x80000000u;
    float shadowDist[2] = {1, -1};

    // ---- layout ----
    int layout = 0; // 0 none, 1 horizontal, 2 vertical, 3 grid
    float pad[4] = {0, 0, 0, 0}; // left, right, top, bottom
    int childAlign = 0;
    float spacing[2] = {0, 0};
    bool ctrlW = false, ctrlH = false, expandW = false, expandH = false, reverse = false;
    float cell[2] = {0, 0};
    int startCorner = 0, startAxis = 0, constraint = 0, constraintCount = 0;
    bool hasElement = false, ignoreLayout = false;
    float minW = -1, minH = -1, prefW = -1, prefH = -1, flexW = -1, flexH = -1;
    int fitH = 0, fitV = 0; // ContentSizeFitter: 0 unconstrained, 1 min, 2 preferred
    bool mask = false, showMaskGraphic = true;

    std::vector<std::string> scripts; // every MonoBehaviour's class (MenuPage, MenuObject...)

    // ---- computed each layout pass ----
    CRect rect;
    float worldScale = 1;
};

class Canvas {
public:
    bool Load(const std::string& path);
    bool loaded() const { return !nodes_.empty(); }

    // "Canvas_TitleScreen/MenuPages/MenuPage_MainMenu" (from a root) or relative to `from`; -1 if none
    int Find(const std::string& path, int from = -1) const;
    // the first node under `from` (depth first) with that name
    int FindDeep(const std::string& name, int from) const;
    CNode& node(int i) { return nodes_[static_cast<size_t>(i)]; }
    const CNode& node(int i) const { return nodes_[static_cast<size_t>(i)]; }
    int size() const { return static_cast<int>(nodes_.size()); }

    void SetActive(int i, bool on) {
        if (i >= 0) node(i).active = on;
    }
    void SetText(int i, const std::string& t) { // replaces the localised text too
        if (i < 0) return;
        node(i).text = t;
        node(i).locKey.clear();
    }

    void SetSprite(int i, const std::string& sprite) {
        if (i >= 0) node(i).sprite = sprite;
    }
    void SetTextColor(int i, uint32_t c) {
        if (i >= 0) node(i).textColor = c;
    }

    // Lays out and draws the subtree of `root` over the whole virtual screen (the root is the
    // Canvas: its rect is the screen). `loc` supplies the localised texts.
    void Draw(Ui& ui, int root, const Loc& loc);
    // Lays out only (rects valid afterwards; for hit areas and positions)
    void Layout(Ui& ui, int root, const Loc& loc);
    // a node's rect in virtual screen coordinates (y down)
    CRect ScreenRect(const Ui& ui, int i) const;

private:
    std::vector<CNode> nodes_;
    const Loc* loc_ = nullptr;
    Ui* ui_ = nullptr;
    float screenH_ = 360;

    std::string TextOf(const CNode& n) const;
    void LayoutNode(int i, const CRect& parent, float parentScale);
    void LayoutChildren(int i);
    float Preferred(int i, int axis) const;
    float Minimum(int i, int axis) const;
    float Flexible(int i, int axis) const;
    bool InLayout(int i) const;
    void DrawNode(int i, float alpha);
    void DrawImage(const CNode& n, float alpha);
    void DrawText(const CNode& n, float alpha);
};

} // namespace dd::ui
