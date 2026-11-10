// How to Play pages, tile descriptions and the info popups' texts (help.json, made by the installer).
#include <fstream>

#include <nlohmann/json.hpp>

#include "app/app.h"

namespace dd::app {

using json = nlohmann::json;

namespace {
std::string Str(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
} // namespace

bool Help::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return false;
    for (const json& p : j.value("pages", json::array())) {
        HelpPage page;
        page.name = Str(p, "name");
        page.header = Str(p, "header");
        page.headerFallback = Str(p, "headerText");
        for (const json& it : p.value("items", json::array())) {
            HelpItem item;
            item.text = Str(it, "text");
            item.textFallback = Str(it, "fallback");
            item.color = Str(it, "color");
            item.hasSub = it.value("hasSub", false);
            item.sub = Str(it, "sub");
            item.subColor = Str(it, "subColor");
            item.inlineSub = it.value("inline", false);
            for (const json& im : it.value("images", json::array())) {
                HelpImage h;
                h.sprite = Str(im, "sprite");
                h.x = im.value("x", 0.0f);
                h.y = im.value("y", 0.0f);
                h.control = im.value("control", 0);
                h.player = im.value("player", false);
                item.images.push_back(h);
            }
            page.items.push_back(std::move(item));
        }
        pages.push_back(std::move(page));
    }
    const json tiles_ = j.value("tiles", json::object());
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        const json& t = it.value();
        TileInfo ti;
        ti.header = Str(t, "header");
        ti.headerFallback = Str(t, "headerText");
        ti.text = Str(t, "text");
        ti.textFallback = Str(t, "fallback");
        ti.color = Str(t, "color");
        ti.isBall = t.value("isBall", false);
        ti.isHazard = t.value("isHazard", false);
        tiles[it.key()] = ti;
    }
    const json popups_ = j.value("popups", json::object());
    for (auto it = popups_.begin(); it != popups_.end(); ++it)
        for (auto f2 = it.value().begin(); f2 != it.value().end(); ++f2)
            if (f2->is_string()) popups[it.key()][f2.key()] = f2->get<std::string>();
    return true;
}

const std::string& Help::Popup(const std::string& popup, const std::string& field, const std::string& fallback) const {
    auto p = popups.find(popup);
    if (p == popups.end()) return fallback;
    auto f = p->second.find(field);
    return f == p->second.end() || f->second.empty() ? fallback : f->second;
}

} // namespace dd::app
