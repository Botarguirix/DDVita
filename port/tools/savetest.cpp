// Save/continue test: plays runs with a seeded random player, recording the inputs the way the game
// does. After every stage cleared it writes the record, reads it back, replays it on a fresh Run and
// checks the replayed run is in the same state (stage, score, and the next stage starts the same).
//
//   savetest <gamedata.json> <firstSeed> <count> [mode] [save path]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/save.h"
#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/session_generator.h"
#include "sim/sim.h"

using namespace dd;
using namespace dd::sim;
using dd::app::RunRecord;

namespace {
int failures = 0, purchases = 0;

uint64_t StartHash(Run& run) {
    Game g(run);
    g.Start();
    uint64_t h = 1469598103934665603ull;
    for (const Event& e : g.events)
        for (int v : {static_cast<int>(e.type), e.actor, e.from.x * 64 + e.from.y, e.to.x * 64 + e.to.y, e.value})
            for (int i = 0; i < 4; i++) {
                h ^= static_cast<uint8_t>(v >> (8 * i));
                h *= 1099511628211ull;
            }
    return h;
}

// returns stages cleared
int PlayRun(const GameData& data, int seed, GameMode mode, const std::string& path) {
    RunRecord rec;
    rec.seed = seed;
    rec.mode = mode;
    rec.team = 0;
    rec.date = {2025, 6, 1};
    SessionRequest req = app::RequestFor(data, rec);
    Run run(data, GenerateSession(data, req), req.team, req.mode);
    MonoRandom chooser(seed ^ 0x5eed);
    int cleared = 0;
    for (;;) {
        rec.current = app::StageRecord{};
        rec.current.ball = chooser.Next(0, 3); // Neutral / Harder / Easier
        run.ballEffect = static_cast<DifficultyEffect>(rec.current.ball);
        rec.current.swap = run.swapPositions = chooser.Next(0, 2) == 0;
        if (run.activePlayers.size() == 2 && chooser.Next(0, 2) == 0) run.ballCarrierIndex ^= 1;
        rec.current.carrier = run.ballCarrierIndex;
        Game g(run);
        g.Start();
        int guard = 0;
        while (g.phase() != Phase::StageEnded && guard++ < 3000) {
            if (g.phase() == Phase::SuggestUndo) {
                bool yes = chooser.Next(0, 2) == 0;
                rec.current.inputs.push_back(yes ? RunRecord::kUndoYes : RunRecord::kUndoNo);
                g.RespondToUndoSuggestion(yes);
                continue;
            }
            std::vector<Command> cmds = g.GetCommands();
            std::vector<int> ok;
            for (size_t i = 0; i < cmds.size(); i++)
                if (cmds[i].interactable && cmds[i].staminaCost <= g.currentPlayer->stamina) ok.push_back(static_cast<int>(i));
            if (ok.empty()) break;
            int pick = ok[static_cast<size_t>(chooser.Next(0, static_cast<int>(ok.size())))];
            if (chooser.Next(0, 3) == 0)
                for (int i : ok)
                    if (cmds[static_cast<size_t>(i)].onTile && cmds[static_cast<size_t>(i)].direction == kRight) pick = i;
            if (g.Select(cmds[static_cast<size_t>(pick)])) rec.current.inputs.push_back(pick);
        }
        if (g.phase() != Phase::StageEnded || g.gameEndedInDefeat) break;
        // team management: random purchases (some refused, which are not recorded)
        for (int tries = chooser.Next(0, 6); tries > 0; tries--) {
            int code = chooser.Next(0, 4) == 0 ? RunRecord::Recruit(chooser.Next(0, 3))
                                               : RunRecord::Upgrade(chooser.Next(0, 2), static_cast<PlayerStat>(chooser.Next(0, 2) == 0 ? 0 : chooser.Next(2, 4)));
            if (app::ApplyMeta(run, code)) rec.current.meta.push_back(code), purchases++;
        }
        if (!run.AdvanceStage()) break;
        run.swapPositions = false;
        rec.stages.push_back(rec.current);
        cleared++;

        RunRecord loaded;
        if (!app::SaveRecord(path, rec) || !app::LoadRecord(path, &loaded)) {
            std::printf("seed %d: save/load failed\n", seed);
            failures++;
            break;
        }
        Run replay(data, GenerateSession(data, app::RequestFor(data, loaded)), req.team, req.mode);
        if (!app::ReplayStages(replay, loaded)) {
            std::printf("seed %d stage %d: replay refused\n", seed, cleared);
            failures++;
            break;
        }
        replay.ballEffect = run.ballEffect;
        if (replay.gold != run.gold || replay.activePlayers.size() != run.activePlayers.size() ||
            replay.currentTrial != run.currentTrial || replay.currentStage != run.currentStage ||
            replay.GetFinalScoreTotal() != run.GetFinalScoreTotal() || StartHash(replay) != StartHash(run)) {
            std::printf("seed %d stage %d: replayed run differs\n", seed, cleared);
            failures++;
            break;
        }
    }
    app::DeleteRecord(path);
    return cleared;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: savetest <gamedata.json> <firstSeed> <count> [mode] [save path]\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.Load(argv[1], &err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    int first = std::atoi(argv[2]), count = std::atoi(argv[3]);
    GameMode mode = argc > 4 ? static_cast<GameMode>(std::atoi(argv[4])) : GameMode::Normal;
    std::string path = argc > 5 ? argv[5] : "savetest.json";
    int checked = 0;
    for (int s = first; s < first + count; s++) checked += PlayRun(data, s, mode, path);
    std::printf("runs %d saves checked %d purchases %d failures %d\n", count, checked, purchases, failures);
    return failures ? 1 : 0;
}
