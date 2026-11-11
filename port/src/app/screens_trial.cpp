// Canvas_StageEnd's MenuPage_3_TrialComplete (TrialCompleteTimeline) with the FinalTimeline built
// from the prefabs (TimelineElementLabel_*, TimelineElement_Ranks / _Scores / _Player and their
// nodes): the run's stages so far, a rank and a score per stage, a row per player with the stages
// they played.
#include <algorithm>
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
const char* const kRankIcons[6] = {"Icons_RankF", "Icons_RankF", "Icons_RankC", "Icons_RankB", "Icons_RankA", "Icons_RankS"};
constexpr int kStages = 12; // LeagueManager.totalNumberOfStages
} // namespace

// FinalTimeline.Initialise into `dataRoot` of `c`; the lines need the nodes laid out, so `root` is
// laid out once here
void App::BuildTimeline(ui::Canvas& c, int root, int dataRoot, int reveal, bool survivalBonus) {
    const Run& run = *run_;
    ui::Canvas& p = prefabUi_;
    int played = run.stagesPlayed;
    auto label = [&](const char* prefab, const char* key, const char* fb) {
        int n = c.Instantiate(p, p.Find(prefab), dataRoot);
        c.SetText(c.Find("Label/Text_M", n), T(key, fb));
    };
    label("TimelineElementLabel_Scores", "Gameplay/Ending_Performance", "Performance");
    // ranks and scores
    int ranks = c.Instantiate(p, p.Find("TimelineElement_Ranks"), dataRoot);
    c.SetText(c.Find("LeftArea/Text_M", ranks), T("Gameplay/Ending_Ranks", "Ranks"));
    c.SetActive(c.Find("Line", ranks), false);
    int scores = c.Instantiate(p, p.Find("TimelineElement_Scores"), dataRoot);
    c.SetText(c.Find("LeftArea/Text_M", scores), T("General/Scores", "Scores"));
    c.SetActive(c.Find("Line", scores), false);
    for (int s = 0; s < kStages; s++) {
        bool none = s >= played;
        int r = c.Instantiate(p, p.Find("TimelineNode_Image_Player"), c.Find("Nodes", ranks));
        c.SetSprite(c.Find("HideGroup", r), none ? "GameInfoIcons_StageNode_HollowFully" : kRankIcons[std::clamp(run.stageResults[s].rank, 0, 5)]);
        c.SetActive(c.Find("HideGroup", r), s < reveal); // TimelineNode.ShowNode
        int t = c.Instantiate(p, p.Find("TimelineNode_Text_Score"), c.Find("Nodes", scores));
        c.SetText(c.Find("HideGroup/Text_S", t), none ? "-" : WithCommas(run.stageResults[s].score));
        c.SetActive(c.Find("HideGroup", t), s < reveal);
    }
    label("TimelineElementLabel_Players", "Gameplay/Ending_Players", "Players");
    // a row per player (the team, then the dead), six at most
    std::vector<const HumanPlayer*> people(run.activePlayers.begin(), run.activePlayers.end());
    people.insert(people.end(), run.deadPlayers.begin(), run.deadPlayers.end());
    if (people.size() > 6) people.resize(6);
    struct Line {
        int element, line, start, end;
    };
    std::vector<Line> lines;
    int current = std::min(run.currentStageNumberAsOverallStage(), kStages - 1);
    for (const HumanPlayer* hp : people) {
        int e = c.Instantiate(p, p.Find("TimelineElement_Player"), dataRoot);
        c.SetText(c.Find("LeftArea/Text_M", e), hp->playerName);
        const gfx::RuntimeSprite& face = Portrait(*hp);
        c.SetRuntimeSprite(c.Find("LeftArea/Portrait/Sprite", e), &face.tex, face.def);
        c.SetImageColor(c.Find("LeftArea/Portrait/BG", e), hp->dead ? col::Rgb(115, 41, 48) : col::Rgb(157, 157, 157)); // arneRedDark / arneGreyLight
        // TimelineElement.ShowPlayerSurvivalBonus (Score.GetPlayerSurvivalBonus)
        c.SetActive(c.Find("SurvivalBonus", e), survivalBonus && !hp->dead);
        c.SetText(c.Find("SurvivalBonus/Text_S (Label)", e), T("Gameplay/Ending_Survival", "Survival"));
        c.SetText(c.Find("SurvivalBonus/Text_S (Score)", e), WithCommas((kStages - hp->stageStartedIndex) * data_.scoring.finalSurvivalBonusPerStage));
        for (int s = 0; s < kStages; s++) {
            int n = c.Instantiate(p, p.Find("TimelineNode_Image_Player"), c.Find("Nodes", e));
            // TimelineNodePlayer.GetStageIcon
            const char* icon = "GameInfoIcons_StageNode_Complete";
            if (s >= played) icon = "GameInfoIcons_StageNode_HollowFully";
            else if (hp->dead && hp->stageDiedIndex == s) icon = "GameInfoIcons_PlayerDied";
            else if ((hp->dead && s > hp->stageDiedIndex) || s < hp->stageStartedIndex) icon = "GameInfoIcons_StageNode_HollowFully";
            else if (!hp->dead && s + 1 == kStages) icon = "GameInfoIcons_PlayerSurvived";
            c.SetSprite(c.Find("HideGroup", n), icon);
            c.SetActive(c.Find("HideGroup", n), s < reveal);
        }
        int end = std::min({hp->dead ? hp->stageDiedIndex : current, kStages - 1, std::max(reveal - 1, hp->stageStartedIndex)});
        lines.push_back({e, c.Find("Line", e), hp->stageStartedIndex, end});
    }
    // TimelineElement.InitLine: from the first stage played to the last, behind the nodes
    c.Layout(ui_, root, loc_);
    for (const Line& l : lines) {
        c.SetActive(l.line, l.start != l.end);
        if (l.start == l.end) continue;
        const auto& nodes = c.node(c.Find("Nodes", l.element)).kids;
        if (l.end >= static_cast<int>(nodes.size())) continue;
        const ui::CNode& el = c.node(l.element);
        float k = el.worldScale > 0 ? el.worldScale : 1;
        auto centre = [&](int i) { const ui::CRect& r = c.node(nodes[static_cast<size_t>(i)]).rect; return (r.x + r.w * 0.5f - el.rect.x) / k; };
        ui::CNode& line = c.node(l.line);
        line.pos[0] = centre(l.start);
        line.size[0] = centre(l.end) - centre(l.start);
        // the line goes under the nodes
        auto& kids = c.node(l.element).kids;
        kids.erase(std::remove(kids.begin(), kids.end(), l.line), kids.end());
        kids.insert(kids.begin(), l.line);
    }
}

bool App::DrawTrialCompleteCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0 || !run_ || !prefabUi_.loaded()) return false;
    int base = c.Mark();
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage_3_TrialComplete");
    c.SetActive(c.Find("StageEndFlash", root), false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    int page = c.Find("MenuPage_3_TrialComplete", pages);
    c.SetText(c.Find("PageArea/BG/HeadedPanel/Header/Text_L", page),
              ui::Format(T("Gameplay/Results_TrialComplete", "Trial {0.value} of {1.maxValue} Complete!"),
                         {{"value", ui::Int(run_->currentTrial + 1)}, {"maxValue", "4"}}));
    int button = c.Find("PageArea/Buttons/Button_StageEnd", page);
    c.SetText(c.Find("AnimRoot/Label", button), T("General/MenuNav_Continue", "Continue"));
    c.SetActive(c.Find("SelectedOutline", button), true);
    c.SetText(c.Find("ScoreDisplay/Parent/Text_L (Number)", page), WithCommas(run_->GetStageScores()));
    BuildTimeline(c, root, c.Find("PageArea/Timeline/DataRoot", page));
    c.Draw(ui_, root, loc_);
    c.Truncate(base);
    return true;
}

} // namespace dd::app
