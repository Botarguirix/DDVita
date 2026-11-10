// Canvas_Gameplay's MenuPage_Pause and its MenuPage_QuitConfirm / MenuPage_RestartConfirm.
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
std::string WithCommas(int v) {
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}
} // namespace

// Canvas_Gameplay with only `page` of its menus showing (the HUD stays behind it, as in the original)
int App::GameplayMenuPage(const char* page) {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0) return -1;
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == page);
    c.SetActive(pages, true);
    c.SetActive(c.Find("StageHUD", root), false); // drawn by DrawHudCanvas underneath
    c.SetActive(c.Find("GameControls", root), false);
    c.SetActive(c.Find("SeasonalEventPopups", root), false);
    c.SetActive(c.Find("UndoPopup", root), false);
    return c.Find(page, pages);
}

void App::DrawGameplayMenu() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    c.Draw(ui_, root, loc_);
    // back to the HUD's own setup for the next frame
    c.SetActive(c.Find("StageHUD", root), true);
    c.SetActive(c.Find("MenuPages", root), false);
}

bool App::DrawPauseCanvas() {
    ui::Canvas& c = gameUi_;
    int page = GameplayMenuPage("MenuPage_Pause");
    if (page < 0 || !run_) return false;
    int area = c.Find("PageArea", page), buttons = c.Find("Buttons", area);
    c.SetActive(c.Find("WebLinks", area), false); // ExpireByPlatform
    auto button = [&](const char* path, int index, const std::string& label, bool locked) {
        int b = c.Find(path, buttons);
        if (b < 0) return;
        c.SetActive(c.Find("SelectedOutline", b), sel_ == index);
        int l = c.Find("AnimRoot/Label", b);
        if (!label.empty()) c.SetText(l, label);
        if (l >= 0) c.SetTextColor(l, locked ? (c.node(l).baseTextColor & 0x00ffffffu) | 0x60000000u : c.node(l).baseTextColor);
    };
    button("Button (Resume)/Button", 0, "", false);
    button("Button (Options)/Button_ChangePage_Spawn", 1, T("General/Options", "Options"), false);
    button("Button (RestartStage)/Button_ChangePage (RestartStage)", 2, "", !(RestartAllowed() && st_.started));
    button("Button (Quit)/Button_ChangePage (Quit)", 3, "", false);
    // SeedDisplay and ScoreDisplay
    int seed = c.Find("SeedDisplay", area);
    c.SetText(c.Find("Text_S (Mode)", seed), ModeName(rec_.mode));
    c.SetText(c.Find("Text_S (Seed)", seed), std::to_string(rec_.seed));
    c.SetText(c.Find("ScoreDisplay/Parent/Text_L (Number)", area), WithCommas(run_->GetStageScores()));
    DrawGameplayMenu();
    return true;
}

// sel2_: 0 quit, 1 restart; sel_: 0 = do it, 1 = stay (the pages' first button)
bool App::DrawPauseConfirmCanvas() {
    ui::Canvas& c = gameUi_;
    int page = GameplayMenuPage(sel2_ == 0 ? "MenuPage_QuitConfirm" : "MenuPage_RestartConfirm");
    if (page < 0) return false;
    int buttons = c.Find("PageArea/Buttons", page);
    const auto& kids = c.node(buttons).kids;
    for (size_t i = 0; i < kids.size() && i < 2; i++) {
        bool stay = i == 0;
        c.SetText(c.Find("AnimRoot/Label", kids[i]), stay ? T("Gameplay/Pause_QuitConfirm_Stay", "Stay")
                                                     : sel2_ == 0 ? T("General/Quit", "Quit")
                                                                  : T("Gameplay/Pause_RestartStage", "Restart Stage"));
        c.SetActive(c.Find("SelectedOutline", kids[i]), (sel_ == 1) == stay);
    }
    DrawGameplayMenu();
    return true;
}

} // namespace dd::app
