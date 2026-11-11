// Canvas_StageEnd's MenuPage_4_ContinueOrQuit (Keep Going?) and MenuPage_5_ContinueOrQuit_Confirmation
// (Overwrite Existing Save Data?, with SaveInfoPanel's lines about the older save).
#include "app/app.h"
#include "app/save.h"

namespace dd::app {

using namespace dd::sim;

namespace {
// shows `page` alone under Canvas_StageEnd/MenuPages
int ShowPage(ui::Canvas& c, int root, const char* page) {
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == page);
    c.SetActive(c.Find("StageEndFlash", root), false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    return c.Find(page, pages);
}

void Button(ui::Canvas& c, int button, const std::string& label, bool selected) {
    if (button < 0) return;
    if (!label.empty()) c.SetText(c.Find("AnimRoot/Label", button), label);
    c.SetActive(c.Find("SelectedOutline", button), selected);
}
} // namespace

bool App::DrawKeepGoingCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0) return false;
    int page = ShowPage(c, root, "MenuPage_4_ContinueOrQuit");
    int buttons = c.Find("PageArea/Buttons", page);
    Button(c, c.Find("Button_StageEnd_Continue", buttons), T("Gameplay/StageEnd_SaveAndQuit_ContinuePlaying", "Continue Playing"), sel_ == 0);
    // SaveAndQuitButtonHider: straight to saving without a save, through the confirmation with one
    bool exists = RecordExists(savePath_);
    int direct = c.Find("SaveAndQuit/Button_SaveAndQuit (NoDataExists)", buttons);
    int confirm = c.Find("SaveAndQuit/Button_ChangePage_SaveConfirmation", buttons);
    c.SetActive(direct, !exists);
    c.SetActive(confirm, exists);
    Button(c, exists ? confirm : direct, T("Gameplay/StageEnd_SaveAndQuit", "Save & Quit"), sel_ == 1);
    c.Draw(ui_, root, loc_);
    return true;
}

bool App::DrawOverwriteCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0) return false;
    int page = ShowPage(c, root, "MenuPage_5_ContinueOrQuit_Confirmation");
    // SaveInfoPanel: the save that would be replaced
    RunRecord old;
    int info = c.Find("PageArea/Panel/Info", page);
    bool have = LoadRecord(savePath_, &old);
    c.SetActive(c.Find("Text_M (1)", info), false); // the save's date (not kept by the port's saves)
    c.SetActive(c.Find("Text_M (2)", info), have);
    c.SetActive(c.Find("Text_M (3)", info), have);
    if (have) {
        c.SetText(c.Find("Text_M (2)", info), ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(old.team)}}));
        int done = static_cast<int>(old.stages.size());
        c.SetText(c.Find("Text_M (3)", info), ui::Format(T("FrontEnd/Continue_Progress", "{0.gameMode} - Stage {1.trial}-{2.stage}"),
                                                         {{"gameMode", ModeName(old.mode)}, {"trial", ui::Int(done / 3 + 1)}, {"stage", ui::Int(done % 3 + 1)}}));
    }
    int buttons = c.Find("PageArea/Buttons", page);
    Button(c, c.Find("SaveAndQuit/Button_SaveAndQuit (NoDataExists)", buttons), T("Gameplay/StageEnd_SaveAndQuit_Overwrite", "Overwrite Old Data"), sel_ == 0);
    Button(c, c.Find("QuitWithoutSaving/Button", buttons), T("Gameplay/StageEnd_SaveAndQuit_QuitWithoutSaving", "Quit Without Saving"), sel_ == 1);
    Button(c, c.Find("ContinuePlaying/Button", buttons), T("Gameplay/StageEnd_SaveAndQuit_ContinuePlaying", "Continue Playing"), sel_ == 2);
    c.Draw(ui_, root, loc_);
    return true;
}

} // namespace dd::app
