// MenuPage_HowToPlay (HowToPlayManager): the page buttons in two columns and the selected page,
// a HowToPlayPage prefab with a HowToPlayItem per HowToPlayItemConfig (its images and its text with
// the sub header). The Controls page is the port's (the Vita's buttons).
#include <algorithm>

#include "app/app.h"

namespace dd::app {

namespace {
// the button's name -> HowToPlayPageConfig name
std::string PageNameFor(const std::string& button) {
    if (button == "AP") return "Actions";
    std::string s;
    for (char ch : button) {
        if (ch == ' ') continue;
        if (ch == '&') s += "And";
        else s += ch;
    }
    return s;
}
} // namespace

// the page buttons in the order the cursor walks them (left column, then right)
std::vector<int> App::HowToPlayButtons(const ui::Canvas& c, int page) const {
    std::vector<int> out;
    for (const char* col : {"PageArea/Buttons/L", "PageArea/Buttons/R"})
        for (int k : c.node(c.Find(col, page)).kids)
            if (c.node(k).name.rfind("HowToPlayLabel", 0) != 0) out.push_back(k);
    return out;
}

bool App::DrawHowToPlayCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0 || !optionsUi_.loaded()) return false;
    int pages = c.Find("MenuPages", root), page = c.Find("MenuPage_HowToPlay", pages);
    if (page < 0) return false;
    int base = c.Mark();
    std::vector<int> kids = c.node(root).kids;
    std::vector<bool> was;
    for (int k : kids) was.push_back(c.node(k).active), c.SetActive(k, k == pages);
    for (int k : c.node(pages).kids) c.SetActive(k, k == page);
    c.SetActive(c.Find("Back/Button_Simple_Back/SelectedOutline", page), false);

    std::vector<int> buttons = HowToPlayButtons(c, page);
    int sel = std::clamp(sel_, 0, static_cast<int>(buttons.size()) - 1);
    for (size_t i = 0; i < buttons.size(); i++) {
        c.SetActive(c.FindDeep("SelectedOutline", buttons[i]), static_cast<int>(i) == sel);
        // mooDeathballHowToPlayPage: the label is its page's header
        std::string pn = PageNameFor(c.node(buttons[i]).name), label = T("General/Help_Controls", "Controls");
        for (const HelpPage& p : help_.pages)
            if (p.name == pn) label = T(p.header, p.headerFallback);
        c.SetText(c.FindDeep("Label", buttons[i]), label);
    }

    // the page
    int pagesRoot = c.Find("HowToPlayPagesRoot", page);
    int pg = c.Instantiate(optionsUi_, optionsUi_.Find("HowToPlayPage"), pagesRoot);
    c.node(pg).amin[0] = c.node(pg).amin[1] = 0, c.node(pg).amax[0] = c.node(pg).amax[1] = 1; // MaximiseRectOnStart
    c.node(pg).size[0] = c.node(pg).size[1] = 0, c.node(pg).pos[0] = c.node(pg).pos[1] = 0;
    int items = c.Find("Body/Items", pg);
    std::string name = PageNameFor(c.node(buttons[static_cast<size_t>(sel)]).name);
    const HelpPage* hp = nullptr;
    for (const HelpPage& p : help_.pages)
        if (p.name == name) hp = &p;
    auto item = [&](const std::string& text, uint32_t color) {
        int it = c.Instantiate(optionsUi_, optionsUi_.Find("HowToPlayItem"), items);
        c.SetText(c.Find("TextArea/Text_S", it), text);
        c.SetTextColor(c.Find("TextArea/Text_S", it), color);
        return it;
    };
    if (hp) {
        c.SetText(c.Find("HeadedPanel/Header/Text_L", pg), T(hp->header, hp->headerFallback));
        for (const HelpItem& hi : hp->items) {
            std::string text = T(hi.text, hi.textFallback);
            if (hi.hasSub && !hi.sub.empty())
                text = "<color=" + hi.subColor + ">" + T(hi.sub, "") + "</color>" + (hi.inlineSub ? " - " : "\n") + text;
            int it = item(text, hi.color.empty() ? ui::col::White : ui::ParseColor(hi.color));
            int images = c.Find("Images", it);
            bool any = false;
            for (const HelpImage& im : hi.images) {
                if (im.sprite.empty()) continue; // generated players and controller prompts
                int n = c.Instantiate(optionsUi_, optionsUi_.Find("HowToPlayImage"), images);
                c.SetSprite(n, im.sprite);
                c.node(n).pos[0] = im.x;
                c.node(n).pos[1] = im.y;
                if (const gfx::SpriteDef* d = assets_.Sprite(im.sprite)) c.node(n).size[0] = d->w, c.node(n).size[1] = d->h; // SetImageRectToSpriteSize
                any = true;
            }
            if (!any && hi.images.empty()) { // no images: the text takes the whole item
                c.SetActive(images, false);
                ui::CNode& ta = c.node(c.Find("TextArea", it));
                ta.size[0] = 0, ta.pos[0] = 0;
            }
        }
    } else {
        // the port's Controls page
        c.SetText(c.Find("HeadedPanel/Header/Text_L", pg), T("General/Help_Controls", "Controls"));
        std::pair<Input, std::string> rows[] = {
            {Input::Up, T("Gameplay/Action_Move", "Move")},
            {Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")},
            {Input::PassTake, T("Gameplay/Action_Pass", "Pass") + " / " + T("Gameplay/Action_Take", "Take")},
            {Input::Shoot, T("Gameplay/Action_Shoot", "Shoot")},
            {Input::Rest, T("Gameplay/Action_Rest", "Rest") + " / " + T("General/MenuNav_Back", "Back")},
            {Input::Undo, T("Gameplay/Action_Undo", "Undo")},
            {Input::Switch, T("Gameplay/Action_SwitchPlayer", "Switch Player")},
            {Input::Info, T("Gameplay/Help", "Help") + " (popups)"},
            {Input::Help, "Team Info"},
            {Input::Bars, T("General/Setting_HealthBars", "Health Bars")},
            {Input::Menu, T("Gameplay/Pause", "Pause")},
        };
        for (const auto& [in, label] : rows) {
            int it = item(label, ui::col::White);
            int images = c.Find("Images", it);
            c.node(it).size[1] = 24;
            if (const char* s = platform::ButtonSprite(in)) {
                int n = c.Instantiate(optionsUi_, optionsUi_.Find("HowToPlayImage"), images);
                c.SetSprite(n, s);
                if (const gfx::SpriteDef* d = assets_.Sprite(s)) c.node(n).size[0] = d->w, c.node(n).size[1] = d->h;
            } else {
                c.SetText(c.Find("TextArea/Text_S", it), std::string("[") + platform::ButtonName(in) + "]  " + label);
            }
        }
    }
    // scrolling: the items move up by helpScroll_; the ones outside the body are not drawn
    c.Layout(ui_, root, loc_);
    int body = c.Find("Body", pg);
    const ui::CRect br = c.node(body).rect;
    float top = br.y + br.h, contentBottom = top;
    for (int k : c.node(items).kids) contentBottom = std::min(contentBottom, c.node(k).rect.y);
    float k = c.node(items).worldScale > 0 ? c.node(items).worldScale : 1;
    helpMaxScroll_ = std::max(0.0f, (br.y - contentBottom) / k + 4);
    helpScroll_ = std::clamp(helpScroll_, 0.0f, helpMaxScroll_);
    c.node(items).pos[1] += helpScroll_;
    c.Layout(ui_, root, loc_);
    for (int it : c.node(items).kids) {
        const ui::CRect& r = c.node(it).rect;
        float mid = r.y + r.h * 0.5f;
        c.SetActive(it, mid >= br.y && mid <= top);
    }
    float appear = c.appear;
    c.Draw(ui_, root, loc_);
    c.appear = appear;
    c.Truncate(base);
    c.SetActive(page, false);
    for (size_t i = 0; i < kids.size(); i++) c.SetActive(kids[i], was[i]);
    return true;
}

} // namespace dd::app
