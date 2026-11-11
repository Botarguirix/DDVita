// Canvas_StageEnd's MenuPage_1_StageResults filled in the way mpaResultsScreen and StageResultsStat
// do, revealed step by step (revealed_, advanced by the results screen's timer).
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
std::string WithCommas(int v) { // StringHelper.GetNumberWithCommas
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}

const char* RankName(int rank) { // Rank: F, C, B, A, S
    static const char* const n[] = {"F", "F", "C", "B", "A", "S"};
    return rank >= 0 && rank <= 5 ? n[rank] : "F";
}
} // namespace

bool App::DrawResultsCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0 || !run_ || !st_.game) return false;
    Game& g = *st_.game;
    Run& run = *run_;
    const StageResults& res = g.results();
    const StageScoreParts& parts = g.scoreParts();
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage_1_StageResults");
    c.SetActive(c.Find("StageEndFlash", root), false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    int page = c.Find("MenuPage_1_StageResults", pages);
    int step = revealed_;

    // the tall strip with the mode and the team
    c.SetActive(c.Find("TallStrip", page), step >= 1);
    c.SetText(c.Find("TallStrip/Text_S (GameMode)", page), ModeName(rec_.mode));
    c.SetText(c.Find("TallStrip/Text_S (TeamName)", page), ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(rec_.team)}}));
    c.SetActive(c.Find("TallStrip/Text_S (GameMode)", page), step >= 2);
    c.SetActive(c.Find("TallStrip/Text_S (TeamName)", page), step >= 2);

    int info = c.Find("StageInfo", page), stats = c.Find("Stats/StageStats", info);
    c.SetActive(c.Find("Stats", info), step >= 2);
    c.SetText(c.Find("AnimRoot/HeadedPanel/Header/Text_L", stats),
              ui::Format(T("Gameplay/Results_Header", "Stage {0.trial}-{1.stage} Results"),
                         {{"trial", ui::Int(run.currentTrial + 1)}, {"stage", ui::Int(run.currentStage + 1)}}));

    // StageResultsStat.Show for each of the six
    const BallModifierConfig* ball = run.BallModConfig();
    float mult = 1;
    if (ball && ball->difficultyEffect == DifficultyEffect::Easier)
        mult = 1.0f - data_.session.ballMultiplierPerStageEasy * static_cast<float>(run.easyBallModsUsed);
    else if (ball && ball->difficultyEffect == DifficultyEffect::Harder)
        mult = 1.0f + data_.session.ballMultiplierPerStageHard * static_cast<float>(run.hardBallModsUsed);
    int pct = static_cast<int>(std::lround(mult * 100.0f)) - 100;
    std::string multText = (pct < 0 ? std::string() : std::string("+")) + std::to_string(pct) + "%"; // GetFriendlyScoreMultiplier(0)
    // GetScoreMultiplierColour: orange for the easier balls, blue for the harder
    uint32_t ballColour = !ball || ball->difficultyEffect == DifficultyEffect::Neutral ? col::Grey
                          : ball->difficultyEffect == DifficultyEffect::Easier       ? col::Orange
                                                                                     : col::Blue;
    const SeasonalEventConfig* season = data_.SeasonalConfig(run.session.seasonalEvent);
    struct Stat {
        const char* item;
        std::string label;
        int score;
        bool achieved, tick, isBall;
        std::string completion;
    };
    int below = res.parDifference < 0 ? -res.parDifference : 0;
    Stat list[6] = {
        {"StageResultsItem (1)", T("Gameplay/Results_Complete", "Complete"), parts.complete, res.complete, true, false, ""},
        {"StageResultsItem (2)", T("Gameplay/Results_Touchdown", "Touchdown"), parts.touchdown, res.touchdown, true, false, ""},
        {"StageResultsItem (3)", T("Gameplay/Results_WithinPar", "Within Par"), parts.withinPar, res.complete && res.withinPar(), true, false, ""},
        {"StageResultsItem (4)", "", parts.parBonus, res.speedTarget, false, false,
         ui::Format(T("Gameplay/Results_SpeedBonusTarget", "{0.value} Below Par (target {1.value})"),
                    {{"value", ui::Int(below)}, {"value2", ui::Int(-g.ParBonusRankTarget())}})},
        {"StageResultsItem (5)", season ? season->pluralLabel : std::string("Gems"), parts.treasure, res.gemTarget, false, false,
         std::to_string(g.gems) + "/" + std::to_string(parts.gemTarget)},
        {"StageResultsItem (Ball)", ball ? ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", ball->friendlyName}}) : "", 1,
         ball && ball->modifier != BallModifier::NONE, false, true, multText},
    };
    int items = c.Find("AnimRoot/Items", stats);
    for (int s = 0; s < 6; s++) {
        const Stat& st = list[s];
        int it = c.Find(st.item, items);
        if (it < 0) continue;
        bool shown = step >= 2 + s;
        int icon = c.Find("Icon", it), label = c.Find("Text_M - Label", it), score = c.Find("Text_M - Score", it),
            completion = c.Find("Text_M - Completion", it);
        for (int k : {icon, label, score, completion}) c.SetActive(k, false);
        if (!shown) continue;
        uint32_t byAchieved = st.isBall ? (st.achieved ? ballColour : col::Grey) : st.achieved ? col::Green : col::Grey;
        if (st.tick) {
            c.SetSprite(icon, st.achieved ? "Icons_Tick" : "Icons_Cross");
            c.SetImageColor(icon, st.achieved ? col::Green : col::Grey);
            c.SetActive(icon, true);
        } else if (st.isBall) {
            c.SetSprite(icon, ball ? ball->ballSprite : std::string());
            c.SetImageColor(icon, st.achieved ? ballColour : col::Grey);
            c.SetActive(icon, true);
        } else {
            c.SetText(completion, st.completion);
            c.SetTextColor(completion, st.achieved ? col::Green : col::Grey);
            c.SetActive(completion, true);
        }
        c.SetText(label, st.label);
        c.SetTextColor(label, byAchieved);
        c.SetActive(label, true);
        c.SetText(score, st.isBall ? multText : WithCommas(st.score));
        c.SetTextColor(score, st.isBall ? ballColour : st.score > 0 ? col::Green : st.score < 0 ? col::Orange : col::Grey);
        c.SetActive(score, true);
    }

    // the stage's total, its gold and the rank
    c.SetActive(c.Find("Stats/StageTotal", info), step >= 8);
    c.SetText(c.Find("Stats/StageTotal/AnimRoot/Text_L (Number)", info), WithCommas(res.score));
    c.SetActive(c.Find("Stats/StageGold", info), step >= 9);
    c.SetText(c.Find("Stats/StageGold/AnimRoot/Text_L (Number)", info), WithCommas(stageGold_));
    c.SetActive(c.Find("Rank", info), step >= 10);
    c.SetSprite(c.Find("Rank/AnimRoot/RankSprite", info), std::string("Ranks_") + RankName(res.rank));

    // the run's score and gold (counted up from before this stage in the original)
    c.SetActive(c.Find("ScoreDisplay", page), step >= 12);
    c.SetText(c.Find("ScoreDisplay/Parent/Text_L (Number)", page), WithCommas(run.GetStageScores()));
    c.SetActive(c.Find("GoldDisplay", page), step >= 12);
    c.SetText(c.Find("GoldDisplay/Parent/Text_L (Number)", page), WithCommas(run.gold));

    // the button (mooDeathballStageEnd: Continue), once everything is shown
    int button = c.Find("PageArea/Buttons/Button_StageEnd", page);
    c.SetActive(c.Find("PageArea/Buttons", page), step >= kResultSteps);
    c.SetText(c.Find("AnimRoot/Label", button), T("General/MenuNav_Continue", "Continue"));
    c.SetActive(c.Find("SelectedOutline", button), true);
    c.Draw(ui_, root, loc_);
    return true;
}

} // namespace dd::app
