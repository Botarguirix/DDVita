// Meta test: plays runs with a seeded random player and the persistent meta layer attached (stats,
// achievements, Skull Keys, team progress), the way the game reports the stage being played. Then
// checks meta.json round-trips and prints what was earned.
//
//   metatest <data dir> <firstSeed> <count> [mode]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/app.h"
#include "core/session_generator.h"

using namespace dd;
using namespace dd::sim;
using dd::app::Meta;
using dd::app::RunRecord;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: metatest <data dir> <firstSeed> <count> [mode]\n");
        return 2;
    }
    std::string dir = argv[1];
    GameData data;
    std::string err;
    if (!data.Load(dir + "/gamedata.json", &err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    Meta meta(data);
    if (!meta.LoadDefs(dir + "/achievements.json")) std::fprintf(stderr, "no achievements.json\n");
    int first = std::atoi(argv[2]), count = std::atoi(argv[3]);
    GameMode mode = argc > 4 ? static_cast<GameMode>(std::atoi(argv[4])) : GameMode::Normal;
    int stagesWon = 0, runsWon = 0;
    for (int seed = first; seed < first + count; seed++) {
        RunRecord rec;
        rec.seed = seed;
        rec.mode = mode;
        rec.date = {2025, 6, 1};
        SessionRequest req = app::RequestFor(data, rec);
        Run run(data, GenerateSession(data, req), req.team, req.mode);
        MonoRandom chooser(seed ^ 0x5eed);
        meta.teamIndex = 0;
        meta.AddStat(app::Stat::GamesStarted, 1);
        for (;;) {
            Game g(run);
            g.meta = &meta;
            g.Start();
            int guard = 0;
            while (g.phase() != Phase::StageEnded && guard++ < 3000) {
                if (g.phase() == Phase::SuggestUndo) {
                    g.RespondToUndoSuggestion(chooser.Next(0, 2) == 0);
                    continue;
                }
                std::vector<Command> cmds = g.GetCommands();
                std::vector<int> ok;
                for (size_t i = 0; i < cmds.size(); i++)
                    if (cmds[i].interactable && cmds[i].staminaCost <= g.currentPlayer->stamina) ok.push_back(static_cast<int>(i));
                if (ok.empty()) break;
                int pick = ok[static_cast<size_t>(chooser.Next(0, static_cast<int>(ok.size())))];
                // lean towards the goal so some runs get far
                for (int i : ok)
                    if (chooser.Next(0, 2) == 0 && cmds[static_cast<size_t>(i)].onTile && cmds[static_cast<size_t>(i)].direction == kRight) pick = i;
                g.Select(cmds[static_cast<size_t>(pick)]);
            }
            if (g.phase() != Phase::StageEnded || g.gameEndedInDefeat) break;
            stagesWon++;
            for (int tries = 0; tries < 4; tries++)
                app::ApplyMeta(run, RunRecord::Upgrade(chooser.Next(0, 2), static_cast<PlayerStat>(chooser.Next(0, 2) == 0 ? 0 : 2)));
            if (run.currentStageIsFinalStageInFinalTrial()) {
                runsWon++;
                break;
            }
            if (!run.AdvanceStage()) break;
        }
        meta.RunEnded(run, 0);
    }
    // round trip
    const std::string path = "/tmp/metatest.json";
    if (!meta.Save(path)) {
        std::printf("save failed\n");
        return 1;
    }
    Meta back(data);
    back.LoadDefs(dir + "/achievements.json");
    if (!back.Load(path)) {
        std::printf("load failed\n");
        return 1;
    }
    int diffs = 0;
    for (int i = 0; i < static_cast<int>(app::Achievement::Count); i++) diffs += back.unlocked[i] != meta.unlocked[i];
    for (int i = 0; i < static_cast<int>(app::Stat::Count); i++) diffs += back.stats[i] != meta.stats[i];
    diffs += back.keys != meta.keys;
    for (size_t t = 0; t < meta.teams.size(); t++) diffs += back.teams[t].stagesCompleted != meta.teams[t].stagesCompleted;
    std::printf("runs %d stages won %d runs won %d achievements %d/%d keys %d kills %lld moves %lld passes %lld rests %lld gold %lld round-trip diffs %d\n",
                count, stagesWon, runsWon, meta.UnlockedCount(), static_cast<int>(meta.defs.size()), meta.keys,
                meta.StatValue(app::Stat::Kills), meta.StatValue(app::Stat::TilesMoved), meta.StatValue(app::Stat::Passes),
                meta.StatValue(app::Stat::Rests), meta.StatValue(app::Stat::GoldAcquired), diffs);
    std::printf("unlocked:");
    for (const app::AchievementDef& d : meta.defs)
        if (meta.Unlocked(d.id)) std::printf(" %s", d.nameFallback.c_str());
    std::printf("\n");
    return diffs ? 1 : 0;
}
