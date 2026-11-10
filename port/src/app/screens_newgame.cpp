// Canvas_TitleScreen's new game pages: MenuPage_SelectMode, MenuPage_NewGame (NewGameInfoPanel) and
// MenuPage_Teams (TeamSelectInfo, mooDeathballTeamInfo, TeamProgressionDisplay, TeamHighScoresDisplay,
// TeamUnlockRequirement).
#include <algorithm>
#include <cstdlib>
#include <functional>

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

bool IsPage(const ui::CNode& n) { return std::find(n.scripts.begin(), n.scripts.end(), "MenuPage") != n.scripts.end(); }

void Select(ui::Canvas& c, int button, bool on) {
    if (button >= 0) c.SetActive(c.FindDeep("SelectedOutline", button), on);
}

const char* const kModeDesc[4][2] = {{"FrontEnd/GameMode_Normal_Desc1", "FrontEnd/GameMode_Normal_Desc2"},
                                     {"FrontEnd/GameMode_Daily_Desc1", "FrontEnd/GameMode_Daily_Desc2"},
                                     {"FrontEnd/GameMode_Boss_Desc1", "FrontEnd/GameMode_Boss_Desc2"},
                                     {"FrontEnd/GameMode_Action_Desc1", "FrontEnd/GameMode_Action_Desc2"}};
const GameMode kModes[4] = {GameMode::Normal, GameMode::DailyChallenge, GameMode::Boss, GameMode::Action};
} // namespace

// Canvas_TitleScreen with one page showing (pages sit in MenuPages or in its groups: NewGame, Stats...)
int App::TitlePage(const char* page) {
    ui::Canvas& c = titleUi_;
    int root = c.Find("Canvas_TitleScreen");
    if (root < 0) return -1;
    int found = -1;
    std::function<void(int)> walk = [&](int n) {
        for (int k : c.node(n).kids) {
            if (IsPage(c.node(k))) {
                bool on = c.node(k).name == page;
                c.SetActive(k, on);
                if (on) found = k;
            } else {
                walk(k);
            }
        }
    };
    walk(c.Find("MenuPages", root));
    // the logo strip belongs to the main menu; the other pages have the screen to themselves
    bool main = std::string(page) == "MenuPage_MainMenu";
    c.SetActive(c.Find("TitleScreenAnimator/TitleStrip", root), main);
    for (int i = 0; i < c.size(); i++) {
        for (const std::string& s : c.node(i).scripts)
            if (s == "ExpireByPlatform" && !(platform::CanQuit() && c.node(i).name == "Quit")) c.SetActive(i, false);
        if (c.node(i).name.rfind("InfoPopup", 0) == 0) c.SetActive(i, false); // the mouse-over popups
    }
    return found;
}

bool App::DrawModesCanvas() {
    ui::Canvas& c = titleUi_;
    int page = TitlePage("MenuPage_SelectMode");
    if (page < 0) return false;
    int buttons = c.Find("PageArea/Buttons", page);
    const auto& kids = c.node(buttons).kids;
    for (size_t i = 0; i < kids.size() && i < 4; i++) {
        int b = c.FindDeep("Button_NewGame_SelectMode", kids[i]);
        const GameModeConfig* m = data_.GetGameModeConfig(kModes[i]);
        bool unlocked = meta_.ModeUnlocked(m);
        int anim = c.Find("AnimRoot", b);
        c.SetText(c.Find("Label", anim), ModeName(kModes[i]));
        c.SetActive(c.Find("Complete", anim), unlocked && meta_.ModeComplete(kModes[i]));
        c.SetActive(c.Find("Locked", anim), !unlocked);
        int label = c.Find("Label", anim);
        if (label >= 0) c.SetTextColor(label, unlocked ? c.node(label).baseTextColor : (c.node(label).baseTextColor & 0x00ffffffu) | 0x60000000u);
        Select(c, b, sel_ == static_cast<int>(i));
        c.SetActive(c.FindDeep("InfoPopup_S_DailyChallengePopout", kids[i]), false);
    }
    c.Draw(ui_, c.Find("Canvas_TitleScreen"), loc_);
    return true;
}

// NewGameInfoPanel: sel_ 0 Begin, 1 Change Team
bool App::DrawNewGameCanvas() {
    ui::Canvas& c = titleUi_;
    int page = TitlePage("MenuPage_NewGame");
    if (page < 0) return false;
    int mi = newMode_ == GameMode::Normal ? 0 : newMode_ == GameMode::DailyChallenge ? 1 : newMode_ == GameMode::Boss ? 2 : 3;
    bool daily = newMode_ == GameMode::DailyChallenge;
    int area = c.Find("PageArea", page);
    int panel = c.Find("InfoPanel/HeadedPanel", area);
    c.SetText(c.Find("Header/Text_L", panel), ModeName(newMode_));
    int about = c.Find("Body/AboutThisMode/Contents", panel);
    c.SetText(c.Find("Text_M", about), T(kModeDesc[mi][0], ""));
    c.SetText(c.Find("Text_S", about), T(kModeDesc[mi][1], ""));
    int teamOr = c.Find("Body/TeamOrNotAllowedInDC", panel);
    c.SetActive(c.Find("Team", teamOr), !daily);
    c.SetActive(c.Find("NotAllowedInDC", teamOr), daily);
    int team = c.Find("Team", teamOr);
    std::string teamName = ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(newTeam_)}});
    c.SetText(c.Find("Contents/CurrentTeam/Text_M (Contents)", team), teamName);
    c.SetActive(c.Find("Contents/Flash", team), false);
    // TeamStageCompletionGauge
    const TeamProgress& tp = meta_.teams[static_cast<size_t>(std::clamp(newTeam_, 0, static_cast<int>(meta_.teams.size()) - 1))];
    int gauge = c.Find("Contents/TeamStageCompletionGauge_FrontEnd", team);
    c.SetText(c.Find("Text_S (Counter)", gauge), std::to_string(tp.stagesCompleted) + " / 12");
    if (int fill = c.Find("CompletionProgress/Fill", gauge); fill >= 0) c.node(fill).amax[0] = std::min(1.0f, tp.stagesCompleted / 12.0f);
    // Change Team: locked until the first Skull Key or in modes without team selection
    const GameModeConfig* m = data_.GetGameModeConfig(newMode_);
    bool teamsLocked = (m && !m->allowTeamSelection) || meta_.TeamSelectionLocked();
    int change = c.Find("Button (Teams)/Button_ChangePage_ChangeTeam", team);
    int changeLabel = c.Find("AnimRoot/Label", change);
    if (changeLabel >= 0) c.SetTextColor(changeLabel, teamsLocked ? (c.node(changeLabel).baseTextColor & 0x00ffffffu) | 0x60000000u : c.node(changeLabel).baseTextColor);
    Select(c, change, sel_ == 1);
    Select(c, c.Find("Body/Button (Start)/Button", panel), sel_ == 0);
    // ShowLocalHighScores
    int best = 0;
    for (size_t t = 0; t < meta_.teams.size(); t++) best = std::max(best, meta_.BestScore(static_cast<int>(t), newMode_));
    int hs = c.Find("HighScores/HeadedPanel/Body/HighScore (Local)/Contents", area);
    c.SetText(c.Find("HighScoreMode/Text_M", hs), ModeName(newMode_));
    c.SetText(c.Find("HighScoreMode/Text_M (Contents)", hs), WithCommas(best));
    c.SetActive(c.Find("HighScoreTeam", hs), !daily);
    c.SetText(c.Find("HighScoreTeam/Text_M", hs), teamName);
    c.SetText(c.Find("HighScoreTeam/Text_M (Contents)", hs), WithCommas(meta_.BestScore(newTeam_, newMode_)));
    Select(c, c.Find("Back/Button_Simple", page), false);
    c.Draw(ui_, c.Find("Canvas_TitleScreen"), loc_);
    return true;
}

// TeamSelectInfo: the list on the right (scrolled to the selection), the selected team on the left
bool App::DrawTeamsCanvas() {
    ui::Canvas& c = titleUi_;
    int page = TitlePage("MenuPage_Teams");
    if (page < 0) return false;
    int n = static_cast<int>(data_.teams.size());
    int area = c.Find("PageArea", page), panels = c.Find("Panels", area);
    int content = c.Find("Scroll View/Viewport/Content", panels);
    const auto& rows = c.node(content).kids;
    for (size_t i = 0; i < rows.size(); i++) {
        int b = rows[i];
        bool has = static_cast<int>(i) < n;
        c.SetActive(b, has);
        if (!has) continue;
        const TeamProgress& tp = meta_.teams[i];
        const TeamConfig* t = data_.teams[i];
        int anim = c.Find("AnimRoot", b);
        c.SetText(c.Find("Label", anim), TeamName(static_cast<int>(i)));
        c.SetActive(c.Find("SelectedTick", anim), static_cast<int>(i) == newTeam_);
        c.SetActive(c.Find("SelectedTick/BG", anim), static_cast<int>(i) == newTeam_);
        c.SetActive(c.Find("SelectedTick/Tick", anim), static_cast<int>(i) == newTeam_);
        int price = c.Find("Price_Keys", anim);
        c.SetActive(price, !tp.unlocked);
        c.SetText(c.Find("Text_M - Price", price), std::to_string(t->unlockCost));
        Select(c, b, sel_ == static_cast<int>(i));
    }
    // ScrollToSelected: the content moves so the selected row stays in the viewport
    c.Layout(ui_, c.Find("Canvas_TitleScreen"), loc_);
    if (sel_ >= 0 && sel_ < static_cast<int>(rows.size())) {
        int view = c.Find("Scroll View/Viewport", panels);
        const ui::CRect& vr = c.node(view).rect;
        const ui::CRect& rr = c.node(rows[static_cast<size_t>(sel_)]).rect;
        float& y = c.node(content).pos[1];
        if (rr.y < vr.y) y += vr.y - rr.y;                               // below the view (y up)
        else if (rr.y + rr.h > vr.y + vr.h) y -= rr.y + rr.h - (vr.y + vr.h); // above it
        y = std::max(0.0f, y);
    }

    // the selected team
    int sel = std::clamp(sel_, 0, n - 1);
    const TeamConfig* t = data_.teams[static_cast<size_t>(sel)];
    const TeamProgress& tp = meta_.teams[static_cast<size_t>(sel)];
    int info = c.Find("TeamInfoArea/Contents/Team Info", panels);
    c.SetText(c.Find("TeamInfoArea/HeadedPanel/Header/Text_L", panels), ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(sel)}}));
    c.SetText(c.Find("Main Info/Description/Text_S (Description)", info), t->description);
    int traits = c.Find("Main Info/TeamTraits", info);
    auto trait = [&](const char* path, const char* text, const TeamTraitConfig* tr) {
        int k = c.Find(path, traits);
        c.SetActive(k, tr && !tr->description.empty());
        if (tr) c.SetText(c.Find(text, k), ui::Format(tr->description, {{"value", ui::Int(tr->valueInt)}}));
    };
    trait("Positive", "Text_M (PositiveTrait)", t->positiveTrait);
    trait("Negative", "Text_M (NegativeTrait)", t->negativeTrait);
    for (int p = 0; p < 2; p++) {
        int s = c.Find("Player Setups/TeamPlayerInfo (" + std::to_string(p + 1) + ")", info);
        bool has = p < static_cast<int>(t->players.size());
        c.SetActive(s, has);
        if (!has) continue;
        const auto& ps = t->players[static_cast<size_t>(p)];
        c.SetText(c.Find("Index/Text_S (Index)", s), ui::Format(T("FrontEnd/Teams_PlayerIndex", "Player {value}"), {{"value", ui::Int(p + 1)}}));
        c.SetSprite(c.Find("Class/ClassIcon", s), ps.playerClassConfig ? std::string("Icons_Class_") + ToString(ps.playerClassConfig->playerClass) : "Icons_Class_Random");
        c.SetText(c.Find("Class/Text_M (Class)", s), ps.playerClassConfig ? ClassName(ps.playerClassConfig) : T("General/Random", "Random"));
        c.SetSprite(c.Find("Perk/PerkIcon", s), ps.playerPerk ? ps.playerPerk->icon : std::string("Perks_Random"));
        c.SetText(c.Find("Perk/Text_M (Perk)", s), T("General/Perk", "Perk") + ": " + (ps.playerPerk ? PerkName(ps.playerPerk) : T("General/Random", "Random")));
    }
    int ctx = c.Find("TeamInfoArea/Contents/Contextual", panels);
    int un = c.Find("Root_Unlocked", ctx), lo = c.Find("Root_Locked", ctx);
    c.SetActive(un, tp.unlocked);
    c.SetActive(lo, !tp.unlocked);
    if (tp.unlocked) {
        int prog = c.Find("Top/Progress/TeamProgressionDisplay", un);
        int g = c.Find("TeamStageCompletionGauge", prog);
        c.SetText(c.Find("Text_S (Counter)", g), std::to_string(tp.stagesCompleted) + " / 12");
        if (int fill = c.Find("CompletionProgress/Fill", g); fill >= 0) c.node(fill).amax[0] = std::min(1.0f, tp.stagesCompleted / 12.0f);
        // the Skull Keys this team has earned (a key per trial: sprites in the key icons)
        int keys = c.Find("Keys/KeyIcons", prog);
        const auto& icons = c.node(keys).kids;
        for (size_t k = 0; k < icons.size(); k++)
            c.SetImageColor(icons[k], static_cast<int>(k) < tp.keysAcquired ? c.node(icons[k]).baseImageColor : col::Rgb(0, 0, 0, 0.5f));
        int list = c.Find("Bottom/HighScores/TeamHighScoresDisplay/List", un);
        static const char* const rowsHs[3] = {"Score (NormalMode)", "Score (BossMode)", "Score (ActionMode)"};
        static const GameMode modes[3] = {GameMode::Normal, GameMode::Boss, GameMode::Action};
        for (int r = 0; r < 3; r++) {
            int row = c.Find(rowsHs[r], list);
            const auto& k = c.node(row < 0 ? list : row).kids;
            if (row < 0 || k.size() < 2) continue;
            c.SetText(k[0], ModeName(modes[r]));
            c.SetText(k[1], WithCommas(meta_.BestScore(sel, modes[r])));
        }
        Select(c, c.Find("Bottom/Button (Unlocked)/Button_SelectTeam", un), false);
    } else {
        int reqs = c.Find("UnlockInfo/Contents", lo);
        int keysReq = c.Find("TeamUnlockRequirement (SkullKeys)", reqs);
        bool enough = meta_.keys >= t->unlockCost;
        c.SetSprite(c.Find("RequirementMetIcon", keysReq), enough ? "Icons_Tick" : "Icons_Cross");
        c.SetText(c.Find("Text_S (Label)", keysReq), ui::Format(T("FrontEnd/Teams_Req_SkullKeys", "{value} Skull Keys"), {{"value", ui::Int(t->unlockCost)}}));
        c.SetText(c.Find("Text_S (Info)", keysReq),
                  enough ? std::string() : ui::Format(T("FrontEnd/Teams_SkullKeysDiff", "({value} more required)"), {{"value", ui::Int(t->unlockCost - meta_.keys)}}));
        c.SetText(c.Find("Text_S (Help)", keysReq), T("FrontEnd/Teams_SkullKeysHelp", "Complete more Trials using different Teams."));
        int achReq = c.Find("TeamUnlockRequirement (Achievement)", reqs);
        const AchievementDef* d = t->requiredAchievement > 0 ? meta_.Def(static_cast<Achievement>(t->requiredAchievement)) : nullptr;
        c.SetActive(achReq, d != nullptr);
        if (d) {
            bool got = meta_.Unlocked(d->id);
            c.SetSprite(c.Find("RequirementMetIcon", achReq), got ? "Icons_Tick" : "Icons_Cross");
            c.SetText(c.Find("Text_S (Label)", achReq), T("FrontEnd/Teams_Req_Achievement", "Achievement:"));
            c.SetText(c.Find("Text_S (Info)", achReq), T(d->name, d->nameFallback));
            c.SetText(c.Find("Text_S (Help)", achReq), T(d->desc, d->descFallback));
        }
        Select(c, c.Find("Button (Locked)/Button_UnlockTeam", lo), false);
    }
    // CurrentTeamDisplay and KeysDisplay
    c.SetText(c.Find("CurrentTeam/Text_L (TeamName)", area), TeamName(newTeam_));
    c.SetActive(c.Find("CurrentTeam/Wipe", area), false);
    c.SetText(c.Find("KeysDisplay/Parent/Text_L (Number)", page), std::to_string(meta_.keys));
    Select(c, c.Find("Back/Button_Simple_Back", area), false);
    c.Draw(ui_, c.Find("Canvas_TitleScreen"), loc_);
    return true;
}

// UnlockPopupNewMode / UnlockPopup (Music Unlocked): the panel over the title with its rays turning
bool App::DrawUnlockCanvas() {
    ui::Canvas& c = titleUi_;
    int root = c.Find("Canvas_TitleScreen");
    if (root < 0) return false;
    int u = unlockPopups_.front();
    bool music = u == kMusicPopup;
    int mode = c.Find("UnlockPopup_NewMode", root), pack = c.Find("UnlockPopup_MusicPack", root);
    int popup = music ? pack : mode;
    if (popup < 0) return false;
    DrawTitleCanvas(); // the main menu behind, as it is
    int pages = c.Find("MenuPages", root), anim = c.Find("TitleScreenAnimator", root);
    c.SetActive(pages, false);
    c.SetActive(anim, false);
    c.SetActive(c.Find("Blackout", root), true);
    c.SetActive(popup, true);
    c.SetActive(music ? mode : pack, false);
    if (!music)
        c.SetText(c.Find("AnimRoot/Contents/Text_M (Info)", popup),
                  ui::Format(T("FrontEnd/GameMode_Unlocked", "{value} is now available when starting a new game."), {{"value", ModeName(static_cast<GameMode>(u))}}));
    // RotateConstantly
    const auto& rays = c.node(c.Find("ShinyRays", popup)).kids;
    for (size_t i = 0; i < rays.size(); i++) c.node(rays[i]).rotation = static_cast<float>(i) * 72.0f + timer_ * (i % 2 ? -12.0f : 12.0f);
    c.Draw(ui_, root, loc_);
    c.SetActive(popup, false);
    c.SetActive(c.Find("Blackout", root), false);
    c.SetActive(pages, true);
    c.SetActive(anim, true);
    return true;
}

} // namespace dd::app
