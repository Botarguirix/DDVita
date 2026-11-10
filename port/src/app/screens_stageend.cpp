// Canvas_StageEnd's StageEndFlash ("Touchdown!" / "Made It!" / "You Lose"), MenuPage_RestartOrGiveUp
// and the blackout behind the stage end pages (Canvas (blackout) / BlackFadeBG).
#include <algorithm>

#include "app/app.h"

namespace dd::app {

// BlackFadeBG.Show: the stage darkened behind the results and the team pages
void App::DrawBlackFade() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas (blackout)");
    if (root < 0) return;
    c.SetActive(c.Find("BlackFadeBG/Square", root), true);
    c.Draw(ui_, root, loc_);
}

bool App::DrawFlashCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0) return false;
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    int flash = c.Find("StageEndFlash", root);
    c.SetActive(flash, true);
    // the strip opens over a quarter of a second (the flash's Animator)
    c.node(flash).scale[1] = std::min(1.0f, timer_ / 0.25f);
    c.SetText(c.Find("Text", flash), flashText_);
    c.SetActive(c.Find("Text", flash), timer_ >= 0.25f);
    c.Draw(ui_, root, loc_);
    c.SetActive(flash, false);
    return true;
}

bool App::DrawDefeatCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0) return false;
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage_RestartOrGiveUp");
    c.SetActive(c.Find("StageEndFlash", root), false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    int buttons = c.Find("MenuPage_RestartOrGiveUp/PageArea/Buttons", pages);
    c.SetActive(c.Find("Button (Restart)/SelectedOutline", buttons), sel_ == 0);
    c.SetActive(c.Find("Button (Give Up)/SelectedOutline", buttons), sel_ == 1);
    c.Draw(ui_, root, loc_);
    return true;
}

} // namespace dd::app
