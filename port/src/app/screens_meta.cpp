// Stats & Achievements, the Skull Keys popup and the title's unlock popups.
#include <algorithm>
#include <cmath>

#include "app/app.h"

namespace dd::app {

using namespace dd::gfx;
using ui::Align;
namespace col = ui::col;

void App::DrawAchievements() {
    const float W = ui_.W(), H = ui_.H();
    ui_.Text(ui::Large, W * 0.5f, 8, col::White, T("FrontEnd/StatsAndAchievements", "Stats & Achievements"), Align::Center);
    // tabs (Switch flips them)
    std::string tabs[2] = {T("FrontEnd/Achievements", "Achievements"), T("FrontEnd/Stats", "Stats")};
    for (int i = 0; i < 2; i++)
        ui_.Button(W * 0.5f - 150 + i * 152, 36, 148, 20, tabs[i], metaTab_ == i, true, ui::Small);
    if (metaTab_ == 0) {
        int n = static_cast<int>(meta_.defs.size());
        ui_.Text(ui::Small, W - 14, 40, col::Yellow, std::to_string(meta_.UnlockedCount()) + "/" + std::to_string(n), Align::Right);
        float cell = std::min(36.0f, (W - 40) / kAchievementCols);
        float gx = std::floor((W - cell * kAchievementCols) * 0.5f), gy = 64;
        for (int i = 0; i < n; i++) {
            const AchievementDef& d = meta_.defs[static_cast<size_t>(i)];
            float x = gx + (i % kAchievementCols) * cell, y = gy + (i / kAchievementCols) * cell;
            bool got = meta_.Unlocked(d.id);
            bool on = i == sel_;
            ui_.Fill(x + 1, y + 1, cell - 2, cell - 2, on ? col::Rgb(190, 38, 51, 0.9f) : col::Rgb(27, 38, 50, 0.85f));
            if (on) ui_.Frame(x, y, cell, cell, col::White);
            std::string icon = got ? d.icon : (d.hidden ? meta_.lockedIcon : d.iconLocked);
            if (!ui_.Sprite(icon, x + 2, y + 2, cell - 4, cell - 4)) ui_.Sprite("Achievements_Locked", x + 2, y + 2, cell - 4, cell - 4);
        }
        // the selected one
        if (sel_ >= 0 && sel_ < n) {
            const AchievementDef& d = meta_.defs[static_cast<size_t>(sel_)];
            bool got = meta_.Unlocked(d.id);
            float rows = std::ceil(static_cast<float>(n) / kAchievementCols);
            float py = gy + rows * cell + 8, pw = W - 60, px = 30;
            float ph = std::max(40.0f, H - py - 36);
            ui_.Panel(px, py, pw, ph);
            bool secret = d.hidden && !got;
            ui_.Text(ui::Medium, px + 10, py + 6, got ? col::Yellow : col::White, secret ? "???" : T(d.name, d.nameFallback));
            if (!secret) ui_.Paragraph(ui::Small, px + 10, py + 24, pw - 120, col::Grey, T(d.desc, d.descFallback));
            if (d.progress && d.targetValue > 0 && !got) {
                long long v = std::min<long long>(meta_.ProgressValue(d), d.targetValue);
                ui_.Text(ui::Small, px + pw - 10, py + 8, col::Grey,
                         ui::Format(T("FrontEnd/Stat_ValueCounter", "{0.value} of {1.maxValue}"),
                                    {{"value", std::to_string(v)}, {"maxValue", std::to_string(d.targetValue)}}),
                         Align::Right);
            }
            if (got) ui_.Text(ui::Small, px + pw - 10, py + 8, col::Green, T("FrontEnd/Unlocked", "Unlocked!"), Align::Right);
        }
    } else {
        // GlobalStatTracker, grouped like the original's stats page
        struct Row {
            const char* key;
            const char* fb;
            long long value;
        };
        auto v = [&](Stat s) { return meta_.StatValue(s); };
        std::vector<std::pair<std::string, std::vector<Row>>> groups = {
            {T("FrontEnd/Stats_SubHdr_Career", "Career"),
             {{"FrontEnd/Stats_GamesStarted", "Games Started", v(Stat::GamesStarted)},
              {"FrontEnd/Stat_GamesWon", "Games Won", v(Stat::GameWins)},
              {"FrontEnd/Stat_Escapees", "Escapees", v(Stat::Escapees)},
              {"FrontEnd/Stat_Deaths", "Player Deaths", v(Stat::Deaths)},
              {"FrontEnd/Stat_DailyChallenges", "Daily Challenges", v(Stat::DailyChallenges)},
              {"FrontEnd/Stat_Teams", "Teams Completed", meta_.TeamsCompleted()},
              {"FrontEnd/Stat_MusicPacks", "Music Packs Unlocked", meta_.MusicPacksUnlocked()},
              {"General/SkullKeys", "Skull Keys", meta_.keys}}},
            {T("FrontEnd/Stats_SubHdr_Stages", "Stages"),
             {{"FrontEnd/Stat_StagesWon", "Stages Won", v(Stat::StageWins)},
              {"FrontEnd/Stat_Touchdowns", "Touchdowns", v(Stat::StageWinsTouchdowns)},
              {"FrontEnd/Stat_StagesWithinPar", "Stages Within Par", v(Stat::StageWinsWithinPar)},
              {"FrontEnd/Stat_StagesRankA", "Stages Rank A or higher", v(Stat::StageWinsRankA)},
              {"FrontEnd/Stat_Restarts", "Stages Restarted", v(Stat::StagesRestarted)},
              {"FrontEnd/Stat_GoldAcquired", "Gold Acquired", v(Stat::GoldAcquired)},
              {"FrontEnd/Stat_GemsAcquired", "Gems Acquired", v(Stat::GemsAcquired)}}},
            {T("FrontEnd/Stats_SubHdr_Actions", "Actions"),
             {{"FrontEnd/Stat_Kills", "Kills", v(Stat::Kills)},
              {"FrontEnd/Stat_MonsterHits", "Monster vs Monster Hits", v(Stat::FriendlyFire)},
              {"FrontEnd/Stat_TilesMoved", "Tiles Moved", v(Stat::TilesMoved)},
              {"FrontEnd/Stat_Passes", "Passes", v(Stat::Passes)},
              {"FrontEnd/Stat_Rests", "Rests", v(Stat::Rests)},
              {"FrontEnd/Stat_RocksPushed", "Rocks Pushed", v(Stat::RocksPushed)},
              {"FrontEnd/Stat_SpawnBlocks", "Spawns Blocked", v(Stat::SpawnsBlocked)}}},
        };
        float colW = std::floor((W - 40) / 3), y0 = 66;
        for (size_t gi = 0; gi < groups.size(); gi++) {
            float x = 20 + static_cast<float>(gi) * colW, y = y0;
            ui_.Panel(x, y, colW - 8, H - y0 - 40);
            ui_.Text(ui::Medium, x + 8, y + 6, col::Yellow, groups[gi].first);
            y += 28;
            for (const Row& r : groups[gi].second) {
                ui_.Text(ui::Small, x + 8, y, col::Grey, T(r.key, r.fb));
                ui_.Text(ui::Small, x + colW - 18, y, col::White, std::to_string(r.value), Align::Right);
                y += 18;
            }
        }
    }
    float lh = ui_.LineHeight(ui::Small), hy = H - lh - 8;
    ui_.Fill(0, hy - 5, W, lh + 10, PackColor(0, 0, 0, 0.6f));
    float hx = W * 0.5f - 110;
    hx += ui_.Prompt(hx, hy, platform::ButtonName(Input::Switch), platform::ButtonSprite(Input::Switch)) + 2;
    hx += ui_.Text(ui::Small, hx, hy, col::White, tabs[metaTab_ ^ 1]) + 14;
    hx += ui_.Prompt(hx, hy, platform::ButtonName(Input::Back), platform::ButtonSprite(Input::Back)) + 2;
    ui_.Text(ui::Small, hx, hy, col::White, T("General/MenuNav_Back", "Back"));
}

// UnlockPopupSkullKeys
void App::DrawSkullKeys() {
    if (st_.view) DrawStage(), ui_.Begin();
    ui_.Shade(0.6f);
    const float W = ui_.W(), H = ui_.H();
    int n = meta_.unreportedKeys;
    float w = 300, h = 150, x = std::floor((W - w) * 0.5f), y = std::floor((H - h) * 0.5f);
    ui_.Panel(x, y, w, h, col::Rgb(20, 12, 28, 0.95f), col::Yellow);
    std::string hdr = n == 1 ? T("FrontEnd/Unlocks_SkullKeys_Hdr_Sngl", "Skull Key Acquired!") : T("FrontEnd/Unlocks_SkullKeys_Hdr_Plrl", "Skull Keys Acquired!");
    ui_.Text(ui::Medium, W * 0.5f, y + 10, col::Yellow, hdr, Align::Center);
    float kx = W * 0.5f - n * 14;
    for (int i = 0; i < n; i++) ui_.Sprite("Icons_SkullKey_wOutline", kx + i * 28, y + 38, 26, 26);
    ui_.Text(ui::Small, W * 0.5f, y + 72, col::White, "x" + std::to_string(meta_.keys), Align::Center);
    ui_.Paragraph(ui::Small, x + 16, y + 94, w - 32, col::Grey, T("FrontEnd/Unlocks_SkullKeys_Body", "Use Skull Keys to unlock new Teams."), Align::Center);
}

// the title's popups: a mode unlocked, or new music
void App::DrawUnlocks() {
    if (unlockPopups_.empty()) return;
    ui_.Shade(0.6f);
    int u = unlockPopups_.front();
    std::string hdr, body;
    if (u == kMusicPopup) {
        hdr = T("FrontEnd/Unlocks_Music_Hdr", "Music Unlocked");
        body = T("FrontEnd/Unlocks_Music_Body", "New music tracks have been added to the mix!");
    } else {
        hdr = ModeName(static_cast<GameMode>(u)) + " " + T("FrontEnd/Unlocked", "Unlocked!");
        body = ui::Format(T("FrontEnd/GameMode_Unlocked", "{value} is now available when starting a new game."),
                          {{"value", ModeName(static_cast<GameMode>(u))}});
    }
    DrawConfirm(hdr, body, {T("General/MenuNav_Continue", "Continue")}, 0);
}

} // namespace dd::app
