// Canvas_StageStart's "MenuPage - StageStart" filled in the way StageStartPanel, BallInfo,
// mooDeathballChangeBall, PlayerStageSetup and ScoreDisplay do.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
std::string WithCommas(int v) { // CurrencyDisplay's numberWithCommas
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}

std::string UpperAscii(std::string s) {
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    return s;
}

// MenuObjectLocker's locked look
uint32_t Dim(uint32_t c) { return (c & 0x00ffffffu) | 0x60000000u; }
} // namespace

bool App::DrawStageStartCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageStart");
    if (root < 0 || !run_ || !st_.game) return false;
    Run& run = *run_;
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage - StageStart");
    c.SetActive(c.Find("PlayerIntroductions", root), false);
    int page = c.Find("MenuPage - StageStart", pages);
    int area = c.Find("PageArea", page), body = c.Find("Body", area);

    // header (SimpleStageNumberDisplay / SimpleGameModeDisplay)
    c.SetText(c.Find("BG/HeadedPanel/Header/Text_L", area), UpperAscii(StageName()));
    c.SetText(c.Find("BG/HeadedPanel/Header/Text_L (Mode)", area), ModeName(rec_.mode));

    // StageStartPanel.SetParText
    c.SetText(c.Find("StageInfo/Par/Text_M (Par)", body), ui::Format(T("Gameplay/Turns_Par", "{value} turns"), {{"value", ui::Int(st_.game->TurnPar())}}));
    c.SetText(c.Find("StageInfo/SpeedBonus/Text_M (SpeedBonusTarget)", body),
              ui::Format(T("Gameplay/Turns_SpeedBonusTarget", "{value} below par"), {{"value", ui::Int(-st_.game->ParBonusRankTarget())}}));

    // the ball (hidden for teams that cannot change it)
    bool ballMods = !run.team->traitBlockBallMods();
    int ball = c.Find("Ball", body);
    c.SetActive(ball, ballMods);
    const BallModifierConfig* curCfg = run.BallModConfig();
    if (ballMods) {
        int used = c.Find("ButtonRowHeader_StageStartPanel/TextAndIcon/Text_S (ModsUsed)", ball);
        c.SetActive(used, run.currentStageNumberAsOverallStage() != 0);
        c.SetText(used, ui::Format(T("Gameplay/BallModsUsed", "USED: {0.easyValue} Easy, {1.hardValue} Hard"),
                                   {{"easyValue", ui::Int(run.easyBallModsUsed)}, {"hardValue", ui::Int(run.hardBallModsUsed)}}));
        static const DifficultyEffect order[3] = {DifficultyEffect::Easier, DifficultyEffect::Neutral, DifficultyEffect::Harder};
        static const char* const diff[3] = {"Gameplay/BallMod_Diff_Easy", "Gameplay/BallMod_Diff_Normal", "Gameplay/BallMod_Diff_Hard"};
        static const char* const diffFb[3] = {"Easy", "Normal", "Hard"};
        for (int b = 0; b < 3; b++) {
            std::string n = std::to_string(b);
            int btn = c.Find("Area/Buttons/Ball (" + n + ")/Button_ChangeBall (" + n + ")", ball);
            const BallModifierConfig* cfg = run.BallModConfigFor(order[b]);
            if (btn < 0 || !cfg) continue;
            int anim = c.Find("AnimRoot", btn);
            for (int k : c.node(anim).kids) // MenuObject.SetLabel: its labels
                if (c.node(k).name == "Label") c.SetText(k, cfg->friendlyName);
            c.SetSprite(c.Find("Icon", anim), cfg->ballSprite);
            c.SetText(c.Find("Difficulty/Text_S", anim), UpperAscii(T(diff[b], diffFb[b])));
            c.SetActive(c.Find("SelectedBall", anim), cfg == curCfg);
            c.SetActive(c.Find("SelectedOutline", btn), sel_ == 3 + b);
        }
        // BallInfo.ShowBallInfo
        if (curCfg) {
            int cb = c.Find("Area/CurrentBall", ball);
            c.SetText(c.Find("TextArea (info)/Text_S - Ball Name", cb), ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", curCfg->friendlyName}}));
            std::string desc = curCfg->description;
            if (desc.find('{') != std::string::npos) desc = ui::Format(desc, {{"value", ui::Int(curCfg->value)}});
            c.SetText(c.Find("TextArea (info)/Text_S - BallDescription", cb), desc);
            c.SetSprite(c.Find("BallSprite", cb), curCfg->ballSprite);
            // GetFriendlyScoreMultiplier(1) on GetScoreMultiplierBackgroundColour
            float mult = 1;
            if (curCfg->difficultyEffect == DifficultyEffect::Easier)
                mult = 1.0f - data_.session.ballMultiplierPerStageEasy * static_cast<float>(run.easyBallModsUsed + 1);
            else if (curCfg->difficultyEffect == DifficultyEffect::Harder)
                mult = 1.0f + data_.session.ballMultiplierPerStageHard * static_cast<float>(run.hardBallModsUsed + 1);
            int pct = static_cast<int>(std::lround(mult * 100.0f)) - 100;
            std::string pctText = (pct < 0 ? std::string() : std::string("+")) + std::to_string(pct) + "%";
            c.SetText(c.Find("TextArea (multiplier)/Text_S - ScoreMultiplier", cb), pctText);
            uint32_t bg = curCfg->difficultyEffect == DifficultyEffect::Easier   ? col::Rgb(115, 41, 48)
                        : curCfg->difficultyEffect == DifficultyEffect::Harder ? col::Blue
                                                                               : col::DarkGrey;
            c.SetImageColor(c.Find("TextArea (multiplier)/SectionBG_White", cb), bg);
        }
    }

    // the team
    int team = c.Find("Team", body);
    c.SetText(c.Find("ButtonRowHeader_StageStartPanel/TextAndIcon/Text_S", team),
              ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(rec_.team)}}));
    int players = c.Find("Area/Players", team);
    std::vector<int> setups = c.node(players).kids;
    std::sort(setups.begin(), setups.end()); // PlayerStageSetup (0), (1)
    for (size_t i = 0; i < setups.size(); i++) {
        int s = setups[i];
        if (i >= run.activePlayers.size()) {
            c.SetActive(s, false);
            continue;
        }
        c.SetActive(s, true);
        const HumanPlayer& hp = *run.activePlayers[i];
        const gfx::RuntimeSprite& face = Portrait(hp);
        c.SetRuntimeSprite(c.Find("Sprite", s), &face.tex, face.def);
        int carrierBall = c.Find("Sprite/Ball", s);
        c.SetActive(carrierBall, static_cast<int>(i) == run.ballCarrierIndex);
        if (curCfg) c.SetSprite(carrierBall, curCfg->ballSprite);
        c.SetText(c.Find("Right/Text_M (Name)", s), hp.playerName);
        c.SetSprite(c.Find("Right/ClassIcon", s), std::string("Icons_Class_") + ToString(hp.playerClassConfig->playerClass));
        int perk = c.Find("Right/PerkIcon", s);
        bool unlocked = hp.perkUnlocked() && hp.perkChain;
        c.SetSprite(perk, unlocked ? hp.perkChain->icon : std::string("Perks_Locked"));
        c.SetActive(c.Find("PerkLevel", perk), unlocked);
        if (unlocked) c.SetSprite(c.Find("PerkLevel", perk), "Perks_Fill_3-" + std::to_string(std::clamp(hp.perkLevel, 1, 3)));
    }
    // StageStartPanel.SwapPlayerPositions: the first setup goes last
    if (setups.size() == 2) {
        bool swapped = run.swapPositions && run.activePlayers.size() == 2;
        c.node(players).kids = swapped ? std::vector<int>{setups[1], setups[0]} : setups;
    }
    int buttons = c.Find("Area/Buttons", team);
    auto button = [&](const char* path, int index, bool locked) {
        int b = c.Find(path, buttons);
        if (b < 0) return;
        c.SetActive(c.Find("SelectedOutline", b), sel_ == index);
        int label = c.Find("AnimRoot/Label", b);
        if (label >= 0) c.SetTextColor(label, locked ? Dim(c.node(label).baseTextColor) : c.node(label).baseTextColor);
    };
    button("Button (1) SwapPlayers/Button", 1, false);
    button("Button (2) SwapBall/Button", 2, run.activePlayers.size() < 2);
    button("TeamInfo/Button", -1, false);
    c.SetActive(c.Find("Start/Button/SelectedOutline", body), sel_ == 0);

    // ScoreDisplay: the run's score so far
    c.SetText(c.Find("ScoreDisplay/Parent/Text_L (Number)", page), WithCommas(run.GetStageScores()));
    c.Draw(ui_, root, loc_);
    return true;
}

} // namespace dd::app
