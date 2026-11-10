// Canvas_TitleScreen's Stats group: MenuPage_Stats&Achievements (the hub), MenuPage_Stats (StatsScreen
// with StatDisplay rows), MenuPage_Achievements (AchievementGrid, AchievementInfo) and
// MenuPage_HallOfFame (HallOfFameEntryDisplay). metaTab_: 0 hub, 1 stats, 2 achievements, 3 hall of fame.
#include <algorithm>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

namespace col = ui::col;

namespace {
std::string WithCommas(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}
} // namespace

bool App::DrawStatsCanvas() {
    ui::Canvas& c = titleUi_;
    static const char* const pages[4] = {"MenuPage_Stats&Achievements", "MenuPage_Stats", "MenuPage_Achievements", "MenuPage_HallOfFame"};
    int page = TitlePage(pages[metaTab_]);
    if (page < 0) return false;
    int base = c.Mark();
    auto outline = [&](int n, bool on) {
        if (n >= 0) c.SetActive(c.FindDeep("SelectedOutline", n), on);
    };
    for (int i = 0; i < c.size(); i++)
        if (c.node(i).name.rfind("Button_Simple", 0) == 0) outline(i, false); // Back

    if (metaTab_ == 0) {
        int buttons = c.Find("PageArea/Buttons", page);
        const auto& kids = c.node(buttons).kids;
        for (size_t i = 0; i < kids.size(); i++) outline(kids[i], sel_ == static_cast<int>(i));
    } else if (metaTab_ == 1 && optionsUi_.loaded()) {
        // StatsScreen.DisplayStats: sub headers on arneBlue, the rows alternating a background
        int list = c.Find("PageArea/Stats", page), prefab = optionsUi_.Find("StatDisplay");
        bool bg = false;
        auto header = [&](const char* key, const char* fb) {
            int n = c.Instantiate(optionsUi_, prefab, list);
            c.SetText(c.Find("Text_M (Label)", n), T(key, fb));
            c.SetActive(c.Find("Text_M (Value)", n), false);
            c.SetImageColor(c.Find("Background", n), col::Blue);
            bg = false;
        };
        auto row = [&](const char* key, const char* fb, long long v, int max = 0) {
            int n = c.Instantiate(optionsUi_, prefab, list);
            c.SetText(c.Find("Text_M (Label)", n), T(key, fb));
            c.SetText(c.Find("Text_M (Value)", n),
                      max > 0 ? ui::Format(T("FrontEnd/Stat_ValueCounter", "{0.value} of {1.maxValue}"), {{"value", std::to_string(v)}, {"maxValue", std::to_string(max)}})
                              : WithCommas(v));
            c.SetActive(c.Find("Background", n), bg);
            bg = !bg;
        };
        auto v = [&](Stat s) { return meta_.StatValue(s); };
        header("FrontEnd/Stats_SubHdr_Career", "Career");
        row("FrontEnd/Stats_GamesStarted", "Games Started", v(Stat::GamesStarted));
        row("FrontEnd/Stat_GamesWon", "Games Won", v(Stat::GameWins));
        row("FrontEnd/Stat_Escapees", "Escapees", v(Stat::Escapees));
        row("FrontEnd/Stat_Deaths", "Player Deaths", v(Stat::Deaths));
        row("FrontEnd/Stat_DailyChallenges", "Daily Challenges", v(Stat::DailyChallenges));
        row("FrontEnd/Stat_Teams", "Teams Completed", meta_.TeamsCompleted(), static_cast<int>(data_.teams.size()));
        row("FrontEnd/Stat_MusicPacks", "Music Packs Unlocked", meta_.MusicPacksUnlocked(), 4);
        header("FrontEnd/Stats_SubHdr_Stages", "Stages");
        row("FrontEnd/Stat_StagesWon", "Stages Won", v(Stat::StageWins));
        row("FrontEnd/Stat_Touchdowns", "Touchdowns", v(Stat::StageWinsTouchdowns));
        row("FrontEnd/Stat_StagesWithinPar", "Stages Within Par", v(Stat::StageWinsWithinPar));
        row("FrontEnd/Stat_StagesRankA", "Stages Rank A or higher", v(Stat::StageWinsRankA));
        row("FrontEnd/Stat_Restarts", "Stages Restarted", v(Stat::StagesRestarted));
        header("FrontEnd/Stats_SubHdr_Actions", "Actions");
        row("FrontEnd/Stat_TilesMoved", "Tiles Moved", v(Stat::TilesMoved));
        row("FrontEnd/Stat_Rests", "Rests", v(Stat::Rests));
        row("FrontEnd/Stat_Passes", "Passes", v(Stat::Passes));
        row("FrontEnd/Stat_MonsterHits", "Monster vs Monster Hits", v(Stat::FriendlyFire));
        row("FrontEnd/Stat_Kills", "Kills", v(Stat::Kills));
        row("FrontEnd/Stat_RocksPushed", "Rocks Pushed", v(Stat::RocksPushed));
        row("FrontEnd/Stat_SpawnBlocks", "Spawns Blocked", v(Stat::SpawnsBlocked));
        row("FrontEnd/Stat_GoldAcquired", "Gold Acquired", v(Stat::GoldAcquired));
        row("FrontEnd/Stat_GemsAcquired", "Gems Acquired", v(Stat::GemsAcquired));
        c.SetActive(c.Find("InfoPopup_S", page), false);
    } else if (metaTab_ == 2) {
        int body = c.Find("PageArea/HeadedPanel/Body", page);
        int n = static_cast<int>(meta_.defs.size());
        // OverallProgress
        int done = meta_.UnlockedCount();
        // AchievementManager.percentCompleteString
        int pct = n > 0 ? std::clamp(static_cast<int>(static_cast<float>(done) * 100.0f / n + 0.5f), 0, 100) : 0;
        c.SetText(c.Find("OverallProgress/Label/Text_M", body), std::to_string(pct) + "%");
        if (int fill = c.Find("OverallProgress/ProgressBar/FillMask/Fill", body); fill >= 0)
            c.node(fill).amax[0] = n > 0 ? static_cast<float>(done) / n : 0.0f;
        // AchievementGrid: a cell per achievement, in the defs' order
        const auto& cells = c.node(c.Find("Buttons", body)).kids;
        for (size_t i = 0; i < cells.size(); i++) {
            int cell = cells[i];
            bool has = static_cast<int>(i) < n;
            c.SetActive(cell, has);
            if (!has) continue;
            const AchievementDef& d = meta_.defs[i];
            bool got = meta_.Unlocked(d.id);
            c.SetSprite(c.Find("IconMask/Icon", cell), got ? d.icon : (d.hidden ? meta_.lockedIcon : d.iconLocked));
            bool bar = d.progress && d.targetValue > 0 && !got;
            c.SetActive(c.Find("ProgressBar", cell), bar);
            if (bar)
                if (int fill = c.Find("ProgressBar/Fill", cell); fill >= 0)
                    c.node(fill).amax[0] = std::min(1.0f, static_cast<float>(meta_.ProgressValue(d)) / d.targetValue);
            outline(cell, sel_ == static_cast<int>(i));
        }
        // AchievementInfo: the selected one
        int info = c.Find("AchievementInfo", body);
        if (sel_ >= 0 && sel_ < n) {
            const AchievementDef& d = meta_.defs[static_cast<size_t>(sel_)];
            bool got = meta_.Unlocked(d.id), secret = d.hidden && !got;
            c.SetSprite(c.Find("Icon", info), got ? d.icon : (d.hidden ? meta_.lockedIcon : d.iconLocked));
            c.SetText(c.Find("Left (info)/Top/Text_M (name)", info), secret ? "???" : T(d.name, d.nameFallback));
            c.SetText(c.Find("Left (info)/Bottom/Text_M (description)", info), secret ? std::string() : T(d.desc, d.descFallback));
            bool stat = d.progress && d.targetValue > 0;
            int right = c.Find("Right (progress)", info);
            c.SetActive(c.Find("Simple", right), !stat);
            c.SetActive(c.Find("Stat", right), stat);
            c.SetText(c.Find("Simple/Text_M", right), got ? T("FrontEnd/Unlocked", "Unlocked!") : T("General/Locked", "Locked"));
            c.SetTextColor(c.Find("Simple/Text_M", right), got ? col::Green : col::Grey);
            if (stat) {
                long long v = std::min<long long>(meta_.ProgressValue(d), d.targetValue);
                if (got) v = d.targetValue;
                c.SetText(c.Find("Stat/Text_M (progress)", right), WithCommas(v) + " / " + WithCommas(d.targetValue));
                if (int fill = c.Find("Stat/ProgressBar/Mask/Fill", right); fill >= 0) c.node(fill).amax[0] = static_cast<float>(v) / d.targetValue;
            }
        }
    } else if (metaTab_ == 3 && optionsUi_.loaded()) {
        int area = c.Find("PageArea/BG", page), items = c.Find("Items", area);
        c.SetActive(c.Find("NoEntries", area), meta_.hallOfFame.empty());
        int prefab = optionsUi_.Find("HallOfFameEntryDisplay");
        for (const auto& [name, date] : meta_.hallOfFame) {
            int e = c.Instantiate(optionsUi_, prefab, items);
            c.SetText(c.Find("Text_S (Name)", e), name);
            c.SetText(c.Find("Text_S (Date)", e), date);
        }
    }
    c.Draw(ui_, c.Find("Canvas_TitleScreen"), loc_);
    c.Truncate(base);
    return true;
}

} // namespace dd::app
