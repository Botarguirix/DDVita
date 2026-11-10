// Unity UI (uGUI) layout and drawing from the original's Canvas trees.
#include "ui/canvas.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dd::ui {

using json = nlohmann::json;

namespace {

uint32_t Color(const json& c) {
    if (!c.is_array() || c.size() < 4) return 0xffffffffu;
    return gfx::PackColor(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
}

uint32_t WithAlpha(uint32_t c, float a) {
    float ca = ((c >> 24) & 255) / 255.0f * a;
    return (c & 0x00ffffffu) | (static_cast<uint32_t>(std::clamp(ca, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

void Get2(const json& n, const char* k, float* out) {
    if (n.contains(k) && n[k].is_array() && n[k].size() >= 2) {
        out[0] = n[k][0].get<float>();
        out[1] = n[k][1].get<float>();
    }
}

int I(const json& o, const char* k, int d = 0) { return o.contains(k) && o[k].is_number() ? o[k].get<int>() : d; }
float F(const json& o, const char* k, float d = 0) { return o.contains(k) && o[k].is_number() ? o[k].get<float>() : d; }
std::string S(const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }

// Canvas.referencePixelsPerUnit of the game's canvases (CanvasScaler)
constexpr float kRefPPU = 16;

} // namespace

bool Canvas::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object() || !j.contains("nodes")) return false;
    nodes_.clear();
    std::unordered_map<int64_t, int> imageIds;               // Image component path id -> node
    std::vector<std::pair<int, int64_t>> tints;               // button node -> its target's path id
    for (const json& n : j["nodes"]) {
        CNode c;
        c.name = S(n, "n");
        c.active = n.value("a", true);
        c.parent = I(n, "p", -1);
        for (const json& k : n.value("k", json::array())) c.kids.push_back(k.get<int>());
        Get2(n, "amin", c.amin);
        Get2(n, "amax", c.amax);
        Get2(n, "piv", c.piv);
        Get2(n, "pos", c.pos);
        Get2(n, "size", c.size);
        Get2(n, "scale", c.scale);
        for (const json& k : n.value("c", json::array())) {
            std::string t = S(k, "t");
            c.scripts.push_back(t);
            bool on = k.value("on", true);
            if (t == "Image") {
                c.hasImage = true;
                c.imageOn = on;
                if (k.contains("id")) imageIds[k["id"].get<int64_t>()] = static_cast<int>(nodes_.size());
                c.sprite = S(k, "sprite");
                c.imageColor = c.baseImageColor = Color(k.value("color", json()));
                c.imageType = I(k, "Type");
                c.fillMethod = I(k, "FillMethod");
                c.fillOrigin = I(k, "FillOrigin");
                c.fillCenter = I(k, "FillCenter", 1) != 0;
                c.fillClockwise = I(k, "FillClockwise", 1) != 0;
                c.preserveAspect = I(k, "PreserveAspect") != 0;
                c.fillAmount = F(k, "FillAmount", 1);
                c.ppum = F(k, "PPUM", 1);
            } else if (t == "Text") {
                c.hasText = true;
                c.textOn = on;
                c.text = S(k, "text");
                c.font = S(k, "font");
                c.textColor = c.baseTextColor = Color(k.value("color", json()));
                c.fontSize = I(k, "FontSize");
                c.alignment = I(k, "Alignment");
                c.hOverflow = I(k, "HorizontalOverflow");
                c.vOverflow = I(k, "VerticalOverflow");
                c.lineSpacing = F(k, "LineSpacing", 1);
                c.rich = I(k, "RichText") != 0;
            } else if ((t == "Shadow" || t == "Outline") && on) {
                c.hasShadow = true;
                c.outline = t == "Outline";
                c.shadowColor = Color(k.value("color", json()));
                Get2(k, "dist", c.shadowDist);
            } else if (t == "HorizontalLayoutGroup" || t == "VerticalLayoutGroup" || t == "GridLayoutGroup") {
                if (!on) continue;
                c.layout = t == "HorizontalLayoutGroup" ? 1 : t == "VerticalLayoutGroup" ? 2 : 3;
                const json& p = k.value("pad", json::array());
                for (size_t q = 0; q < 4 && q < p.size(); q++) c.pad[q] = p[q].get<float>();
                c.childAlign = I(k, "align");
                if (c.layout == 3) {
                    Get2(k, "cell", c.cell);
                    Get2(k, "spacing", c.spacing);
                    c.startCorner = I(k, "StartCorner");
                    c.startAxis = I(k, "StartAxis");
                    c.constraint = I(k, "Constraint");
                    c.constraintCount = I(k, "ConstraintCount");
                } else {
                    c.spacing[0] = c.spacing[1] = F(k, "spacing");
                    c.ctrlW = I(k, "ChildControlWidth") != 0;
                    c.ctrlH = I(k, "ChildControlHeight") != 0;
                    c.expandW = I(k, "ChildForceExpandWidth") != 0;
                    c.expandH = I(k, "ChildForceExpandHeight") != 0;
                    c.reverse = I(k, "ReverseArrangement") != 0;
                }
            } else if (t == "LayoutElement" && on) {
                c.hasElement = true;
                c.ignoreLayout = I(k, "Ignore") != 0;
                c.minW = F(k, "MinWidth", -1);
                c.minH = F(k, "MinHeight", -1);
                c.prefW = F(k, "PreferredWidth", -1);
                c.prefH = F(k, "PreferredHeight", -1);
                c.flexW = F(k, "FlexibleWidth", -1);
                c.flexH = F(k, "FlexibleHeight", -1);
            } else if (t == "ContentSizeFitter" && on) {
                c.fitH = I(k, "h");
                c.fitV = I(k, "v");
            } else if (t == "Mask" && on) {
                c.mask = true;
                c.showMaskGraphic = I(k, "show", 1) != 0;
            } else if ((t == "Button" || t == "Toggle" || t == "Slider") && I(k, "transition") == 1 && k.contains("target")) {
                c.tintNormal = Color(k.value("normal", json()));
                c.tintSelected = Color(k.value("selected", json()));
                tints.emplace_back(static_cast<int>(nodes_.size()), k["target"].get<int64_t>());
            } else if (t == "LocalizeStringEvent") {
                c.locKey = S(k, "key");
            }
        }
        nodes_.push_back(std::move(c));
    }
    for (const auto& [button, id] : tints) {
        auto it = imageIds.find(id);
        if (it != imageIds.end()) node(button).tintTarget = it->second;
    }
    return !nodes_.empty();
}

int Canvas::Find(const std::string& path, int from) const {
    size_t start = 0;
    int cur = from;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        int found = -1;
        if (cur < 0) {
            for (int i = 0; i < size(); i++)
                if (nodes_[static_cast<size_t>(i)].parent < 0 && nodes_[static_cast<size_t>(i)].name == part) {
                    found = i;
                    break;
                }
        } else {
            for (int k : node(cur).kids)
                if (node(k).name == part) {
                    found = k;
                    break;
                }
        }
        if (found < 0) return -1;
        cur = found;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return cur;
}

int Canvas::FindDeep(const std::string& name, int from) const {
    if (from < 0) return -1;
    for (int k : node(from).kids) {
        if (node(k).name == name) return k;
        int d = FindDeep(name, k);
        if (d >= 0) return d;
    }
    return -1;
}

int Canvas::Instantiate(const Canvas& src, int srcRoot, int parent) {
    if (srcRoot < 0 || srcRoot >= src.size()) return -1;
    int base = size();
    // the subtree, depth first, renumbered from base
    std::vector<int> order;
    std::vector<int> stack{srcRoot};
    while (!stack.empty()) {
        int i = stack.back();
        stack.pop_back();
        order.push_back(i);
        const auto& k = src.node(i).kids;
        for (auto it = k.rbegin(); it != k.rend(); ++it) stack.push_back(*it);
    }
    std::unordered_map<int, int> map;
    for (size_t q = 0; q < order.size(); q++) map[order[q]] = base + static_cast<int>(q);
    for (int i : order) {
        CNode c = src.node(i);
        c.parent = i == srcRoot ? parent : map[c.parent];
        for (int& k : c.kids) k = map[k];
        if (c.tintTarget >= 0) c.tintTarget = map.count(c.tintTarget) ? map[c.tintTarget] : -1;
        nodes_.push_back(std::move(c));
    }
    if (parent >= 0) node(parent).kids.push_back(base);
    return base;
}

void Canvas::Truncate(int mark) {
    if (mark >= size()) return;
    nodes_.resize(static_cast<size_t>(mark));
    for (CNode& n : nodes_) n.kids.erase(std::remove_if(n.kids.begin(), n.kids.end(), [&](int k) { return k >= mark; }), n.kids.end());
}

std::string Canvas::TextOf(const CNode& n) const {
    if (!n.locKey.empty() && loc_) return loc_->Get(n.locKey, n.text);
    return n.text;
}

// ---------------------------------------------------------------------------------------------
// Layout (RectTransform, ILayoutElement, LayoutGroup, ContentSizeFitter)
// ---------------------------------------------------------------------------------------------
bool Canvas::InLayout(int i) const {
    const CNode& n = node(i);
    return n.active && !(n.hasElement && n.ignoreLayout);
}

float Canvas::Minimum(int i, int axis) const {
    const CNode& n = node(i);
    float el = axis == 0 ? n.minW : n.minH;
    if (n.hasElement && el >= 0) return el;
    if (n.layout == 1 || n.layout == 2) {
        bool main = (n.layout == 1) == (axis == 0);
        float sum = axis == 0 ? n.pad[0] + n.pad[1] : n.pad[2] + n.pad[3], best = 0;
        int count = 0;
        for (int k : n.kids)
            if (InLayout(k)) {
                bool ctrl = axis == 0 ? n.ctrlW : n.ctrlH;
                float m = ctrl ? Minimum(k, axis) : (axis == 0 ? node(k).size[0] : node(k).size[1]);
                if (main) sum += m + (count++ ? n.spacing[0] : 0);
                else best = std::max(best, m);
            }
        return main ? sum : best + (axis == 0 ? n.pad[0] + n.pad[1] : n.pad[2] + n.pad[3]);
    }
    if (n.hasImage && n.imageOn && (n.imageType == 1 || n.imageType == 2) && ui_) {
        if (const gfx::SpriteDef* d = ui_->assets().Sprite(n.sprite)) {
            float ppu = d->ppu / kRefPPU * n.ppum;
            return axis == 0 ? (d->border[0] + d->border[2]) / ppu : (d->border[1] + d->border[3]) / ppu;
        }
    }
    return 0;
}

float Canvas::Preferred(int i, int axis) const {
    const CNode& n = node(i);
    float el = axis == 0 ? n.prefW : n.prefH;
    if (n.hasElement && el >= 0) return std::max(el, Minimum(i, axis));
    if (n.layout == 1 || n.layout == 2) {
        bool main = (n.layout == 1) == (axis == 0);
        bool ctrl = axis == 0 ? n.ctrlW : n.ctrlH;
        float sum = 0, best = 0;
        int count = 0;
        for (int k : n.kids)
            if (InLayout(k)) {
                float p = ctrl ? Preferred(k, axis) : (axis == 0 ? node(k).size[0] : node(k).size[1]);
                if (main) sum += p + (count++ ? n.spacing[0] : 0);
                else best = std::max(best, p);
            }
        return (main ? sum : best) + (axis == 0 ? n.pad[0] + n.pad[1] : n.pad[2] + n.pad[3]);
    }
    if (n.layout == 3) {
        int count = 0;
        for (int k : n.kids) count += InLayout(k);
        int cols = n.constraint == 1 ? std::max(1, n.constraintCount) : std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count)))));
        int rows = n.constraint == 2 ? std::max(1, n.constraintCount) : (count + cols - 1) / std::max(1, cols);
        if (n.constraint == 2) cols = (count + rows - 1) / rows;
        return axis == 0 ? n.pad[0] + n.pad[1] + cols * n.cell[0] + std::max(0, cols - 1) * n.spacing[0]
                         : n.pad[2] + n.pad[3] + rows * n.cell[1] + std::max(0, rows - 1) * n.spacing[1];
    }
    if (n.hasText && n.textOn && ui_) {
        FontSize f = Ui::FontFor(n.font);
        std::string t = TextOf(n);
        if (axis == 0) {
            float w = 0;
            size_t s = 0;
            while (s <= t.size()) {
                size_t nl = t.find('\n', s);
                w = std::max(w, ui_->Width(f, t.substr(s, nl == std::string::npos ? std::string::npos : nl - s)));
                if (nl == std::string::npos) break;
                s = nl + 1;
            }
            return w;
        }
        float width = n.rect.w > 0 ? n.rect.w : 10000;
        size_t lines = n.hOverflow == 0 ? ui_->Wrap(f, t, width).size() : static_cast<size_t>(std::count(t.begin(), t.end(), '\n') + 1);
        return static_cast<float>(lines) * ui_->LineHeight(f) * n.lineSpacing;
    }
    if (n.hasImage && n.imageOn && ui_) {
        if (const gfx::SpriteDef* d = ui_->assets().Sprite(n.sprite)) {
            if (n.imageType == 1 || n.imageType == 2) return Minimum(i, axis);
            return (axis == 0 ? d->w : d->h) / (d->ppu / kRefPPU * n.ppum);
        }
    }
    return Minimum(i, axis);
}

float Canvas::Flexible(int i, int axis) const {
    const CNode& n = node(i);
    float el = axis == 0 ? n.flexW : n.flexH;
    if (n.hasElement && el >= 0) return el;
    if (n.layout == 1 || n.layout == 2) {
        bool main = (n.layout == 1) == (axis == 0);
        float sum = 0, best = 0;
        for (int k : n.kids)
            if (InLayout(k)) {
                float f = Flexible(k, axis);
                if (main) sum += f;
                else best = std::max(best, f);
            }
        return main ? sum : best;
    }
    return 0;
}

void Canvas::LayoutNode(int i, const CRect& p, float s) {
    CNode& n = node(i);
    // RectTransform: anchors on the parent's rect, sizeDelta, pivot and anchoredPosition
    for (int a = 0; a < 2; a++) {
        float pmin = a == 0 ? p.x : p.y, plen = a == 0 ? p.w : p.h;
        float a0 = pmin + plen * n.amin[a], a1 = pmin + plen * n.amax[a];
        float len = (a1 - a0) + n.size[a] * s;
        float pivot = a0 + (a1 - a0) * n.piv[a] + n.pos[a] * s;
        float k = n.scale[a];
        len *= k;
        float lo = pivot - len * n.piv[a];
        if (a == 0) n.rect.x = lo, n.rect.w = len;
        else n.rect.y = lo, n.rect.h = len;
    }
    n.worldScale = s * n.scale[0];
    LayoutChildren(i);
}

void Canvas::LayoutChildren(int i) {
    CNode& n = node(i);
    float s = n.worldScale;
    // ContentSizeFitter (preferred size on the fitted axes, about the pivot)
    if (n.fitH == 2) {
        float w = Preferred(i, 0) * s;
        n.rect.x += (n.rect.w - w) * n.piv[0];
        n.rect.w = w;
    }
    if (n.fitV == 2) {
        float h = Preferred(i, 1) * s;
        n.rect.y += (n.rect.h - h) * n.piv[1];
        n.rect.h = h;
    }
    std::vector<int> laid;
    if (n.layout) {
        for (int k : n.kids)
            if (InLayout(k)) laid.push_back(k);
    }
    // children outside any layout group keep their own RectTransform
    for (int k : n.kids)
        if (std::find(laid.begin(), laid.end(), k) == laid.end()) LayoutNode(k, n.rect, s);
    if (laid.empty()) return;

    float innerX = n.rect.x + n.pad[0] * s, innerW = n.rect.w - (n.pad[0] + n.pad[1]) * s;
    float innerTop = n.rect.y + n.rect.h - n.pad[2] * s, innerH = n.rect.h - (n.pad[2] + n.pad[3]) * s;
    float alignX = (n.childAlign % 3) * 0.5f, alignY = (n.childAlign / 3) * 0.5f; // 0 left / upper
    auto place = [&](int k, float x, float yTop, float w, float h) {
        CNode& c = node(k);
        c.rect = {x, yTop - h, w, h};
        c.worldScale = s * c.scale[0];
        LayoutChildren(k);
    };

    if (n.layout == 3) { // GridLayoutGroup
        int count = static_cast<int>(laid.size());
        float cw = n.cell[0] * s, ch = n.cell[1] * s, sx = n.spacing[0] * s, sy = n.spacing[1] * s;
        int cols;
        if (n.constraint == 1) cols = std::max(1, n.constraintCount);
        else if (n.constraint == 2) cols = (count + std::max(1, n.constraintCount) - 1) / std::max(1, n.constraintCount);
        else cols = std::max(1, static_cast<int>((innerW + sx) / (cw + sx)));
        int rows = (count + cols - 1) / cols;
        float usedW = cols * cw + (cols - 1) * sx, usedH = rows * ch + (rows - 1) * sy;
        float x0 = innerX + (innerW - usedW) * alignX, y0 = innerTop - (innerH - usedH) * alignY;
        for (int q = 0; q < count; q++) {
            int col = n.startAxis == 0 ? q % cols : q / rows;
            int row = n.startAxis == 0 ? q / cols : q % rows;
            if (n.startCorner == 1 || n.startCorner == 3) col = cols - 1 - col;
            if (n.startCorner >= 2) row = rows - 1 - row;
            place(laid[static_cast<size_t>(q)], x0 + col * (cw + sx), y0 - row * (ch + sy), cw, ch);
        }
        return;
    }

    // Horizontal / VerticalLayoutGroup (HorizontalOrVerticalLayoutGroup.SetChildrenAlongAxis)
    bool horizontal = n.layout == 1;
    if (n.reverse) std::reverse(laid.begin(), laid.end());
    float sizes[2][64] = {};
    float pos[2][64] = {};
    size_t count = std::min<size_t>(laid.size(), 64);
    for (int axis = 0; axis < 2; axis++) {
        bool main = horizontal == (axis == 0);
        bool ctrl = axis == 0 ? n.ctrlW : n.ctrlH;
        bool expand = axis == 0 ? n.expandW : n.expandH;
        float inner = axis == 0 ? innerW : innerH;
        float align = axis == 0 ? alignX : alignY;
        if (!main) {
            for (size_t q = 0; q < count; q++) {
                int k = laid[q];
                float own = (axis == 0 ? node(k).size[0] : node(k).size[1]) * s;
                float size = own;
                if (ctrl) {
                    float mn = Minimum(k, axis) * s, pf = Preferred(k, axis) * s;
                    float fl = Flexible(k, axis) + (expand ? 1.0f : 0.0f);
                    size = std::max(mn, fl > 0 ? inner : std::min(pf, inner));
                }
                sizes[axis][q] = size;
                pos[axis][q] = (inner - size) * align;
            }
            continue;
        }
        float totalMin = 0, totalPref = 0, totalFlex = 0, spacing = n.spacing[0] * s;
        float mins[64], prefs[64], flexs[64];
        for (size_t q = 0; q < count; q++) {
            int k = laid[q];
            if (ctrl) {
                mins[q] = Minimum(k, axis) * s;
                prefs[q] = std::max(mins[q], Preferred(k, axis) * s);
                flexs[q] = Flexible(k, axis) + (expand ? 1.0f : 0.0f);
            } else {
                mins[q] = prefs[q] = (axis == 0 ? node(k).size[0] : node(k).size[1]) * s;
                flexs[q] = expand ? 1.0f : 0.0f;
            }
            totalMin += mins[q];
            totalPref += prefs[q];
            totalFlex += flexs[q];
        }
        float spaces = spacing * (count > 0 ? count - 1 : 0);
        totalMin += spaces;
        totalPref += spaces;
        float minToPref = totalPref > totalMin ? std::clamp((inner - totalMin) / (totalPref - totalMin), 0.0f, 1.0f) : 0.0f;
        float extra = inner > totalPref && totalFlex > 0 ? (inner - totalPref) / totalFlex : 0.0f;
        float used = 0;
        for (size_t q = 0; q < count; q++) {
            float size = mins[q] + (prefs[q] - mins[q]) * minToPref + flexs[q] * extra;
            if (!ctrl && flexs[q] > 0 && extra > 0) {
                // a child that does not get resized still gets the space: it is centred in its slot
                float slot = size;
                size = prefs[q];
                pos[axis][q] = used + (slot - size) * align;
                used += slot + spacing;
                sizes[axis][q] = size;
                continue;
            }
            sizes[axis][q] = size;
            pos[axis][q] = used;
            used += size + spacing;
        }
        used -= count ? spacing : 0;
        float offset = totalFlex > 0 && extra > 0 ? 0 : (inner - used) * align;
        for (size_t q = 0; q < count; q++) pos[axis][q] += offset;
    }
    for (size_t q = 0; q < count; q++)
        place(laid[q], innerX + pos[0][q], innerTop - pos[1][q], sizes[0][q], sizes[1][q]);
}

void Canvas::Layout(Ui& ui, int root, const Loc& loc) {
    if (root < 0) return;
    ui_ = &ui;
    loc_ = &loc;
    screenH_ = ui.H();
    CNode& r = node(root);
    // the Canvas fills the screen
    r.rect = {0, 0, ui.W(), ui.H()};
    r.worldScale = 1;
    LayoutChildren(root);
}

CRect Canvas::ScreenRect(const Ui& ui, int i) const {
    const CRect& r = node(i).rect;
    return {r.x, ui.H() - (r.y + r.h), r.w, r.h};
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
void Canvas::Draw(Ui& ui, int root, const Loc& loc) {
    if (root < 0) return;
    Layout(ui, root, loc);
    // Selectable.DoStateTransition (ColorTint): selected while the button's outline shows
    for (CNode& n : nodes_) n.tint = 0xffffffffu;
    for (int i = 0; i < size(); i++) {
        const CNode& b = node(i);
        if (b.tintTarget < 0) continue;
        int outline = FindDeep("SelectedOutline", i);
        node(b.tintTarget).tint = outline >= 0 && node(outline).active ? b.tintSelected : b.tintNormal;
    }
    DrawNode(root, 1);
    ui.Clip(0, 0, 0, 0);
}

void Canvas::DrawNode(int i, float alpha) {
    CNode& n = node(i);
    if (!n.active) return;
    if (n.hasImage && n.imageOn && (!n.mask || n.showMaskGraphic)) DrawImage(n, alpha);
    if (n.hasText && n.textOn) DrawText(n, alpha);
    if (n.mask) {
        float top = screenH_ - (n.rect.y + n.rect.h);
        ui_->Clip(n.rect.x, top, n.rect.w, n.rect.h);
    }
    for (int k : n.kids) DrawNode(k, alpha);
    if (n.mask) ui_->Clip(0, 0, 0, 0); // nested masks are not intersected
}

void Canvas::DrawImage(const CNode& n, float alpha) {
    if (n.rect.w <= 0 || n.rect.h <= 0) return;
    float x = n.rect.x, y = screenH_ - (n.rect.y + n.rect.h), w = n.rect.w, h = n.rect.h;
    const gfx::SpriteDef* d = n.rtTex ? &n.rtDef : n.sprite.empty() ? nullptr : ui_->assets().Sprite(n.sprite);
    auto draw = [&](float dx, float dy, uint32_t color) {
        if (!d) { // no sprite: Unity draws a plain rect in the colour
            ui_->Fill(x + dx, y + dy, w, h, color);
            return;
        }
        const gfx::Texture& tex = n.rtTex ? *n.rtTex : ui_->assets().Tex(d->texture);
        if (!tex.id) return;
        float tw = static_cast<float>(tex.w), th = static_cast<float>(tex.h);
        auto quad = [&](float qx, float qy, float qw, float qh, float px0, float py0, float px1, float py1) {
            // sprite pixel rect (texture pixels, y up) -> UVs (the texture is stored bottom-up)
            if (qw <= 0 || qh <= 0) return;
            ui_->Quad(tex, qx + dx, qy + dy, qw, qh, px0 / tw, py1 / th, px1 / tw, py0 / th, color);
        };
        if (n.imageType == 1 || n.imageType == 2) { // Sliced (Tiled drawn as sliced)
            float ppu = d->ppu / kRefPPU * n.ppum;
            float bl = d->border[0], bb = d->border[1], br = d->border[2], bt = d->border[3];
            float L = bl / ppu, R = br / ppu, T = bt / ppu, B = bb / ppu;
            // borders shrink together when the rect is smaller than them
            float kx = L + R > w ? w / (L + R) : 1, ky = T + B > h ? h / (T + B) : 1;
            L *= kx, R *= kx, T *= ky, B *= ky;
            float xs[4] = {x, x + L, x + w - R, x + w}, ys[4] = {y, y + T, y + h - B, y + h};
            float us[4] = {d->x, d->x + bl, d->x + d->w - br, d->x + d->w};
            float vs[4] = {d->y + d->h, d->y + d->h - bt, d->y + bb, d->y}; // top to bottom in texture pixels
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) {
                    if (r == 1 && c == 1 && !n.fillCenter) continue;
                    quad(xs[c], ys[r], xs[c + 1] - xs[c], ys[r + 1] - ys[r], us[c], vs[r + 1], us[c + 1], vs[r]);
                }
            return;
        }
        float qx = x, qy = y, qw = w, qh = h;
        if (n.preserveAspect && d->w > 0 && d->h > 0) {
            float k = std::min(w / d->w, h / d->h);
            qw = d->w * k, qh = d->h * k;
            qx = x + (w - qw) * n.piv[0];
            qy = y + (h - qh) * (1 - n.piv[1]);
        }
        float u0 = d->x, u1 = d->x + d->w, v0 = d->y, v1 = d->y + d->h;
        if (n.rotation != 0) { // about the rect's centre (y down on screen: counter-clockwise is negative)
            float a = -n.rotation * 3.14159265f / 180.0f, cs = std::cos(a), sn = std::sin(a);
            float cx = qx + dx + qw * 0.5f, cy = qy + dy + qh * 0.5f;
            float corners[4][2] = {{-qw * 0.5f, -qh * 0.5f}, {qw * 0.5f, -qh * 0.5f}, {qw * 0.5f, qh * 0.5f}, {-qw * 0.5f, qh * 0.5f}};
            float uv[4][2] = {{u0 / tw, v1 / th}, {u1 / tw, v1 / th}, {u1 / tw, v0 / th}, {u0 / tw, v0 / th}};
            ui_->RotatedQuad(tex, cx, cy, corners, uv, cs, sn, color);
            return;
        }
        if (n.imageType == 3 && n.fillMethod == 4 && n.fillAmount < 1) { // Filled, Radial360
            ui_->Radial(tex, qx + dx, qy + dy, qw, qh, u0 / tw, v1 / th, u1 / tw, v0 / th, color, n.fillAmount, n.fillOrigin, n.fillClockwise);
            return;
        }
        if (n.imageType == 3 && n.fillAmount < 1) { // Filled: horizontal / vertical (90 / 180 radial drawn the same way)
            float a = std::clamp(n.fillAmount, 0.0f, 1.0f);
            if (n.fillMethod == 1) { // vertical: origin 0 bottom, 1 top
                float cut = qh * (1 - a), vcut = (v1 - v0) * (1 - a);
                if (n.fillOrigin == 0) qy += cut, v1 -= vcut;
                else v0 += vcut;
                qh -= cut;
            } else { // horizontal: origin 0 left, 1 right
                float cut = qw * (1 - a), ucut = (u1 - u0) * (1 - a);
                if (n.fillOrigin == 0) u1 -= ucut;
                else qx += cut, u0 += ucut;
                qw -= cut;
            }
        }
        ui_->Quad(tex, qx + dx, qy + dy, qw, qh, u0 / tw, v1 / th, u1 / tw, v0 / th, color);
    };
    auto mul = [](uint32_t a, uint32_t b) {
        uint32_t r = 0;
        for (int s = 0; s < 32; s += 8) r |= ((((a >> s) & 255) * ((b >> s) & 255) + 127) / 255) << s;
        return r;
    };
    uint32_t color = WithAlpha(mul(n.imageColor, n.tint), alpha);
    if (n.hasShadow) {
        uint32_t sc = WithAlpha(n.shadowColor, ((n.imageColor >> 24) & 255) / 255.0f * alpha);
        float sx = n.shadowDist[0], sy = -n.shadowDist[1];
        if (n.outline) {
            draw(sx, sy, sc), draw(-sx, sy, sc), draw(sx, -sy, sc), draw(-sx, -sy, sc);
        } else {
            draw(sx, sy, sc);
        }
    }
    draw(0, 0, color);
}

void Canvas::DrawText(const CNode& n, float alpha) {
    std::string t = TextOf(n);
    if (t.empty()) return;
    FontSize f = Ui::FontFor(n.font);
    float lh = ui_->LineHeight(f) * n.lineSpacing;
    std::vector<std::string> lines;
    if (n.hOverflow == 0) {
        lines = ui_->Wrap(f, t, std::max(1.0f, n.rect.w));
    } else {
        size_t s = 0;
        while (s <= t.size()) {
            size_t nl = t.find('\n', s);
            lines.push_back(t.substr(s, nl == std::string::npos ? std::string::npos : nl - s));
            if (nl == std::string::npos) break;
            s = nl + 1;
        }
    }
    // the font's own line height for the last line, the spaced one between lines
    float blockH = (lines.size() - 1) * lh + ui_->LineHeight(f);
    float top = screenH_ - (n.rect.y + n.rect.h);
    int v = n.alignment / 3, hAlign = n.alignment % 3;
    float y = v == 0 ? top : v == 1 ? top + (n.rect.h - blockH) * 0.5f : top + n.rect.h - blockH;
    Align a = hAlign == 0 ? Align::Left : hAlign == 1 ? Align::Center : Align::Right;
    float x = hAlign == 0 ? n.rect.x : hAlign == 1 ? n.rect.x + n.rect.w * 0.5f : n.rect.x + n.rect.w;
    uint32_t color = WithAlpha(n.textColor, alpha);
    uint32_t sc = WithAlpha(n.shadowColor, ((n.textColor >> 24) & 255) / 255.0f * alpha);
    float sx = n.shadowDist[0], sy = -n.shadowDist[1];
    for (const std::string& line : lines) {
        if (n.hasShadow) {
            if (n.outline) {
                for (float ox : {sx, -sx})
                    for (float oy : {sy, -sy}) ui_->Text(f, x + ox, y + oy, sc, StripTags(line), a, false);
            } else {
                ui_->Text(f, x + sx, y + sy, sc, StripTags(line), a, false);
            }
        }
        ui_->Text(f, x, y, color, line, a, false);
        y += lh;
    }
}

} // namespace dd::ui
