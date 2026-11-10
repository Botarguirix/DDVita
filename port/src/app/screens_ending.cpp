// The ending scene's MenuPage_EndingResults (mpaEndingResults.AppearanceSequence): the title strip
// ("X and Y survived / and won their freedom!" or the game over lines), then the FinalTimeline
// revealing a stage at a time while the total counts up, the survival bonuses, the final score,
// the high scores (FinalHighScoreDisplay for the mode and the team) and the misc stats. Confirm
// skips to the end, then goes back to the title.
#include <algorithm>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;

namespace {
// mpaEndingResults' timing
constexpr float kStrip = 3.0f, kBeforeTimeline = 1.0f, kBeforeReveal = 1.0f, kBetweenNodes = 0.3f, kBeforeBonus = 1.0f,
                kBeforePulse = 1.0f, kBeforeHighScores = 2.0f, kBeforeMisc = 1.0f, kBeforeButtons = 1.0f;
constexpr float kPageAt = kStrip + kBeforeTimeline, kFirstStage = kPageAt + kBeforeReveal;
constexpr int kStages = 12;
std::string Commas(int v) {
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}
} // namespace

float App::EndingDoneTime() const {
    int bonuses = defeat_ || !run_ ? 0 : static_cast<int>(run_->activePlayers.size());
    float bonusEnd = kFirstStage + kStages * 2 * kBetweenNodes + (bonuses ? kBeforeBonus + bonuses * kBetweenNodes : 0);
    return bonusEnd + kBeforePulse + kBeforeHighScores + kBeforeMisc + kBeforeButtons;
}

bool App::DrawEndingCanvas() {
    ui::Canvas& c = endingUi_;
    int root = c.Find("Canvas_EndingScreen");
    if (root < 0 || !run_ || !prefabUi_.loaded()) return false;
    const Run& run = *run_;
    bool victory = !defeat_;
    float t = timer_;
    for (int k : c.node(root).kids) c.SetActive(k, c.node(k).name == "MenuPage_EndingResults");
    int page = c.Find("MenuPage_EndingResults", root);
    int area = c.Find("PageArea", page), strip = c.Find("TitleStrip", page);

    // the title strip: in, held, out
    bool stripOn = t < kStrip + 0.3f;
    c.SetActive(strip, stripOn);
    if (stripOn) {
        float k = t < 0.3f ? t / 0.3f : t > kStrip ? 1 - (t - kStrip) / 0.3f : 1;
        c.node(strip).scale[1] = std::clamp(k, 0.0f, 1.0f);
        std::string a, b;
        if (victory) {
            std::vector<const HumanPlayer*> alive(run.activePlayers.begin(), run.activePlayers.end());
            if (alive.size() >= 2) {
                a = ui::Format(T("Gameplay/Ending_Survived_1_Plural", "{0.name} and {1.name} survived"), {{"name", alive[0]->playerName}, {"name2", alive[1]->playerName}});
                b = T("Gameplay/Ending_Survived_2_Plural", "and won their freedom!");
            } else if (!alive.empty()) {
                a = ui::Format(T("Gameplay/Ending_Survived_1_Singular", "{name} survived"), {{"name", alive[0]->playerName}});
                b = alive[0]->seed.isMale ? T("Gameplay/Ending_Survived_2_Male", "and won his freedom!") : T("Gameplay/Ending_Survived_2_Female", "and won her freedom!");
            }
        } else {
            a = T("Gameplay/Ending_GameOver1", "In Dungeon Deathball");
            b = T("Gameplay/Ending_GameOver2", "there are no winners");
        }
        int text = c.Find("AnimRoot/Text", strip);
        c.SetText(c.Find("Text_M (SurvivedText) (1)", text), a);
        c.SetText(c.Find("Text_M (SurvivedText) (2)", text), b);
    }
    c.SetActive(area, t >= kPageAt);
    c.SetActive(c.Find("SeedDisplay", page), t >= kPageAt);
    c.SetText(c.Find("SeedDisplay/Text_S (Seed)", page), std::to_string(run.session.seed));

    int base = c.Mark();
    if (t >= kPageAt) {
        // stages revealed so far (a node, then the line to the next)
        int reveal = t < kFirstStage ? 0 : std::min(kStages, 1 + static_cast<int>((t - kFirstStage) / (2 * kBetweenNodes)));
        float bonusAt = kFirstStage + kStages * 2 * kBetweenNodes + kBeforeBonus;
        int bonuses = victory ? static_cast<int>(run.activePlayers.size()) : 0;
        int bonusShown = !victory || t < bonusAt ? 0 : std::min(bonuses, 1 + static_cast<int>((t - bonusAt) / kBetweenNodes));
        float pulseAt = (bonuses ? bonusAt + bonuses * kBetweenNodes : bonusAt - kBeforeBonus) + kBeforePulse;
        float highAt = pulseAt + kBeforeHighScores, miscAt = highAt + kBeforeMisc, buttonsAt = miscAt + kBeforeButtons;

        // the sounds and jingles on the way (each once, as the time passes them)
        static float last = -1;
        if (last > t) last = -1;
        auto passed = [&](float at) { return last < at && t >= at; };
        for (int s = 0; s < kStages; s++)
            if (passed(kFirstStage + s * 2 * kBetweenNodes) && s < run.stagesPlayed) sound_.PlayCue("MenuMove");
        if (passed(pulseAt)) sound_.PlayJingle("jingle.victory");
        last = t;

        c.SetText(c.Find("BG/HeadedPanel/Header/Text_L", area), T("Gameplay/Ending_FinalResults", "FINAL RESULTS"));
        BuildTimeline(c, root, c.Find("Timeline/DataRoot", area), reveal, bonusShown > 0);
        // the total counting up
        int total = 0;
        for (int s = 0; s < std::min(reveal, run.stagesPlayed); s++) total += run.stageResults[s].score;
        if (bonusShown) total = run.GetFinalScoreTotal();
        int hs = c.Find("HighScoresStrip", area);
        c.SetText(c.Find("TotalScore/Text_M", hs), T("General/FinalScore", "Final Score"));
        c.SetText(c.Find("TotalScore/Text_L", hs), Commas(total));
        // the score's pulse
        float pk = t >= pulseAt && t < pulseAt + 0.4f ? 1 + 0.25f * std::sin((t - pulseAt) / 0.4f * 3.14159f) : 1;
        c.node(c.Find("TotalScore/Text_L", hs)).scale[0] = c.node(c.Find("TotalScore/Text_L", hs)).scale[1] = pk;
        // FinalHighScoreDisplay.Show
        auto high = [&](const char* name, bool on, bool isNew, int prev, const std::string& label) {
            int d = c.Find(name, hs);
            c.SetActive(d, on);
            if (!on) return;
            c.SetActive(c.Find("NewHighScore", d), isNew);
            c.SetActive(c.Find("PreviousHighScore", d), !isNew);
            c.SetText(c.Find("NewHighScore/Text_S (Label)", d), label);
            c.SetText(c.Find("NewHighScore/Text_M (new)", d), T("General/NewRecord", "NEW RECORD"));
            c.SetText(c.Find("PreviousHighScore/Text_S (Label)", d), label);
            c.SetText(c.Find("PreviousHighScore/Text_S (HighScoreLabel)", d), T("General/HighScore", "High Score"));
            c.SetText(c.Find("PreviousHighScore/Text_M (Points)", d), Commas(prev));
        };
        bool daily = run.mode == GameMode::DailyChallenge;
        high("FinalHighScoreDisplay_GameMode", t >= highAt, daily ? meta_.newHighScore : meta_.newModeHighScore, meta_.prevModeBest, ModeName(run.mode));
        high("FinalHighScoreDisplay_Team", t >= highAt && !daily, meta_.newHighScore, meta_.prevTeamBest, TeamName(meta_.teamIndex));
        // ShowMiscStats
        int misc = c.Find("MiscStats", area);
        c.SetActive(misc, t >= miscAt);
        int touchdowns = 0, withinPar = 0;
        for (int s = 0; s < run.stagesPlayed; s++) touchdowns += run.stageResults[s].touchdown, withinPar += run.stageResults[s].withinPar();
        auto stat = [&](const char* path, int v) { c.SetText(c.Find(std::string(path) + "/Text_M", misc), Commas(v)); };
        stat("Items (Left)/Touchdowns", touchdowns);
        stat("Items (Left)/WithinPars", withinPar);
        stat("Items (Left)/Deaths", static_cast<int>(run.deadPlayers.size()));
        stat("Items (Right)/Gold", run.gold);
        stat("Items (Right)/BallMods (Harder)", run.hardBallModsUsed);
        stat("Items (Right)/BallMods (Easier)", run.easyBallModsUsed);
        int button = c.Find("Buttons/Button_FinalResultsScreenHidden", area);
        c.SetActive(c.Find("Buttons", area), t >= buttonsAt);
        c.SetText(c.Find("AnimRoot/Label", button), T("General/MenuNav_Continue", "Continue"));
        c.SetActive(c.Find("SelectedOutline", button), true);
    }
    c.Draw(ui_, root, loc_);
    c.Truncate(base);
    return true;
}

} // namespace dd::app
