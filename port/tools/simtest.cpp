// Simulation soak test: plays whole runs with a seeded random "player", checks invariants after
// every command and prints one hash of the event stream per run (two runs of the same seed must match).
//
//   simtest <gamedata.json> <firstSeed> <count> [mode] [verbose] [startTrial] [YYYY-MM-DD] [rest]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/session_generator.h"
#include "sim/sim.h"

using namespace dd;
using namespace dd::sim;

namespace {
int failures = 0;

void Fail(int seed, const Game& g, const char* what) {
    if (failures++ < 30) std::printf("seed %d turn %d: %s\n", seed, g.Turn(), what);
}

void CheckInvariants(int seed, Game& g) {
    for (int x = 0; x < g.fieldX; x++)
        for (int y = 0; y < g.fieldY; y++) {
            FieldTile* t = g.GetTile({x, y});
            if (t->occupant && t->occupant->gridPos != V2{x, y}) Fail(seed, g, "tile occupant position mismatch");
            if (t->occupant && !t->occupant->isAlive) Fail(seed, g, "dead occupant on tile");
        }
    for (Player* p : g.players) {
        if (!p->isAlive) continue;
        if (g.GetTile(p->gridPos)->occupant != p) Fail(seed, g, "player not on its tile");
        if (p->stamina < 0) Fail(seed, g, "negative stamina");
    }
    for (Enemy* e : g.enemies)
        if (e->isAlive && g.GetTile(e->gridPos)->occupant != e) Fail(seed, g, "enemy not on its tile");
    int holders = 0;
    for (const auto& ent : g.entities)
        if (ent->isCharacter() && static_cast<Character*>(ent.get())->isHoldingBall() && static_cast<Character*>(ent.get())->isAlive) holders++;
    if (holders > 1) Fail(seed, g, "more than one ball holder");
    if (g.ball->carrier && !g.ball->carrier->isHoldingBall()) Fail(seed, g, "ball carrier without the ball");
}

struct Outcome {
    uint64_t hash = 1469598103934665603ull;
    int stages = 0, score = 0, commands = 0;
    bool won = false;
};

void Mix(uint64_t& h, int v) {
    for (int i = 0; i < 4; i++) {
        h ^= static_cast<uint8_t>(v >> (8 * i));
        h *= 1099511628211ull;
    }
}

int startTrial = 0;
bool restOnly = false; // only rest: reaches the lightning hazard rows and the enemy endgame
Date runDate{2025, 6, 1};

Outcome PlayRun(const GameData& data, int seed, GameMode mode, bool verbose) {
    SessionRequest req;
    req.buildVersion = data.buildVersion;
    req.date = runDate;
    req.seed = seed;
    req.mode = mode;
    req.team = TeamForMode(data, mode, 0);
    Run run(data, GenerateSession(data, req), req.team, mode);
    run.currentTrial = startTrial;
    MonoRandom chooser(seed ^ 0x5eed);
    Outcome o;
    for (;;) {
        Game g(run);
        g.Start();
        int guard = 0;
        while (g.phase() != Phase::StageEnded && guard++ < 3000) {
            if (g.phase() == Phase::SuggestUndo) {
                g.RespondToUndoSuggestion(chooser.Next(0, 2) == 0);
                continue;
            }
            std::vector<Command> cmds = g.GetCommands();
            std::vector<Command> ok;
            for (const Command& c : cmds)
                if (c.interactable && c.staminaCost <= g.currentPlayer->stamina) ok.push_back(c);
            if (ok.empty()) {
                Fail(seed, g, "no usable command");
                break;
            }
            // Prefer moving right a bit so stages actually end.
            const Command* pick = &ok[static_cast<size_t>(chooser.Next(0, static_cast<int>(ok.size())))];
            if (restOnly)
                for (const Command& c : ok)
                    if (c.actionConfig->action == ActorAction::Char_Wait) pick = &c;
            if (!restOnly && chooser.Next(0, 3) == 0)
                for (const Command& c : ok)
                    if (c.onTile && c.direction == kRight) pick = &c;
            if (!g.Select(*pick)) Fail(seed, g, "Select refused an offered command");
            o.commands++;
            CheckInvariants(seed, g);
        }
        if (g.phase() != Phase::StageEnded) Fail(seed, g, "stage did not end");
        for (const Event& e : g.events) {
            Mix(o.hash, static_cast<int>(e.type));
            Mix(o.hash, e.actor);
            Mix(o.hash, e.from.x * 64 + e.from.y);
            Mix(o.hash, e.to.x * 64 + e.to.y);
            Mix(o.hash, e.value);
            if (verbose) std::printf("    %s\n", DescribeEvent(g, e).c_str());
        }
        o.stages++;
        if (verbose)
            std::printf("  stage T%dS%d %s score %d\n", run.currentTrial, run.currentStage,
                        g.gameEndedInDefeat ? "lost" : "won", g.results().score);
        if (g.gameEndedInDefeat || g.phase() != Phase::StageEnded) break;
        if (!run.AdvanceStage()) {
            o.won = true;
            break;
        }
    }
    o.score = run.GetFinalScoreTotal();
    return o;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: simtest <gamedata.json> <firstSeed> <count> [mode] [verbose]\n");
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
    bool verbose = argc > 5 && std::atoi(argv[5]) != 0;
    if (argc > 6) startTrial = std::atoi(argv[6]);
    restOnly = argc > 8 && std::string(argv[8]) == "rest";
    if (argc > 7) std::sscanf(argv[7], "%d-%d-%d", &runDate.year, &runDate.month, &runDate.day);
    int totalStages = 0, wins = 0;
    for (int s = first; s < first + count; s++) {
        Outcome a = PlayRun(data, s, mode, verbose);
        Outcome b = PlayRun(data, s, mode, false);
        if (a.hash != b.hash) {
            std::printf("seed %d: NON-DETERMINISTIC\n", s);
            failures++;
        }
        std::printf("seed %d stages %d commands %d score %d %s hash %016llx\n", s, a.stages, a.commands, a.score,
                    a.won ? "WON" : "lost", static_cast<unsigned long long>(a.hash));
        totalStages += a.stages;
        wins += a.won;
    }
    std::printf("runs %d stages %d wins %d failures %d\n", count, totalStages, wins, failures);
    return failures ? 1 : 0;
}
