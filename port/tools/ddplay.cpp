// Console player for the headless simulation (placeholder render).
//
//   ddplay <gamedata.json> <seed> [mode] [team]
//   ddplay <gamedata.json> daily [team]      (today's daily challenge, UTC)
//
// Each turn prints the field and the numbered commands of the current player; type a number
// (or 'q'). Commands can also be piped through stdin.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/session_generator.h"
#include "sim/sim.h"

using namespace dd;
using namespace dd::sim;

static char Glyph(Game& g, V2 p) {
    FieldTile* t = g.GetTile(p);
    if (Actor* a = t->occupant) {
        if (a->isPlayer()) {
            Player* pl = static_cast<Player*>(a);
            char c = static_cast<char>('A' + pl->humanPlayer->playerNumber % 26); // uppercase = has the ball
            return pl->isHoldingBall() ? c : static_cast<char>(c + 32);
        }
        if (a->isEnemy()) {
            Enemy* e = static_cast<Enemy*>(a);
            char c = e->config->actorName.empty() ? 'E' : e->config->actorName[0];
            return e->isHoldingBall() ? '@' : (e->stunned() ? '*' : c);
        }
        if (a->isObstacle()) return '#';
        return '?';
    }
    if (t->containsBall()) return 'o';
    if (t->containsPickups()) return '$';
    if (t->hazard) return t->hazard->isLightningHazard() ? '%' : '^';
    if (t->isDesiredByEnemy()) return '!';
    return t->hasBlood() ? ',' : '.';
}

static void Render(Game& g) {
    std::printf("\n   ");
    for (int x = 0; x < g.fieldX; x++) std::printf("%d", x % 10);
    std::printf("   turn %d/%d par\n", g.Turn(), g.TurnPar());
    for (int y = g.fieldY - 1; y >= 0; y--) {
        std::printf("%2d ", y);
        for (int x = 0; x < g.fieldX; x++) std::putchar(Glyph(g, {x, y}));
        std::putchar('\n');
    }
    for (Player* p : g.players)
        std::printf("  %s%s #%d pos(%d,%d) HP %d ST %d/%d AP %d%s\n", p == g.currentPlayer ? ">" : " ", p->Name().c_str(),
                    p->humanPlayer->playerNumber, p->gridPos.x, p->gridPos.y, p->health, p->stamina, p->maxStamina(),
                    p->actionPoints, p->isHoldingBall() ? " [ball]" : "");
    for (Enemy* e : g.enemies)
        std::printf("   %s (%d,%d) HP %d -> %s %s\n", e->Name().c_str(), e->gridPos.x, e->gridPos.y, e->health,
                    e->hasIntention ? ToString(e->nextAction.action) : "-",
                    e->hasIntention ? (std::to_string(e->nextAction.direction.x) + "," + std::to_string(e->nextAction.direction.y)).c_str() : "");
}

static void Flush(Game& g, size_t& shown) {
    for (; shown < g.events.size(); shown++) {
        const Event& e = g.events[shown];
        if (e.type == Ev::PhaseChanged || e.type == Ev::Intention) continue;
        std::printf("  . %s\n", DescribeEvent(g, e).c_str());
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ddplay <gamedata.json> <seed> [mode] [team]\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.Load(argv[1], &err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    SessionRequest req;
    req.buildVersion = data.buildVersion;
    req.date = Date::TodayUtc();
    if (std::string(argv[2]) == "daily") { // today's daily challenge: same field as the Steam game today
        req.mode = GameMode::DailyChallenge;
        req.seed = DailyChallengeSeed(data.platform, req.date);
    } else {
        req.seed = std::atoi(argv[2]);
        if (argc > 3) req.mode = static_cast<GameMode>(std::atoi(argv[3]));
    }
    int teamArg = req.mode == GameMode::DailyChallenge && std::string(argv[2]) == "daily" ? 3 : 4;
    int team = argc > teamArg ? std::atoi(argv[teamArg]) : 0;
    req.team = TeamForMode(data, req.mode, team);
    Run run(data, GenerateSession(data, req), req.team, req.mode);

    for (;;) {
        Game g(run);
        g.Start();
        size_t shown = 0;
        std::printf("\n=== Trial %d Stage %d ===\n", run.currentTrial + 1, run.currentStage + 1);
        while (g.phase() != Phase::StageEnded) {
            Flush(g, shown);
            if (g.phase() == Phase::SuggestUndo) {
                std::printf("A player is in danger. Undo? [y/n] ");
                std::string a;
                if (!(std::cin >> a)) return 0;
                g.RespondToUndoSuggestion(a == "y");
                continue;
            }
            Render(g);
            std::vector<Command> cmds = g.GetCommands();
            for (size_t i = 0; i < cmds.size(); i++) {
                const Command& c = cmds[i];
                std::string name = c.actionConfig->name;
                if (name.rfind("PlayerActionConfig_", 0) == 0) name = name.substr(19);
                std::printf("  [%zu] %-14s", i, name.c_str());
                if (c.onTile) std::printf(" tile(%d,%d)", c.tile.x, c.tile.y);
                std::printf(" cost %d%s%s\n", c.staminaCost, c.interactable ? "" : " (locked)",
                            c.staminaCost > g.currentPlayer->stamina ? " (no stamina)" : "");
            }
            std::printf("> ");
            std::string in;
            if (!(std::cin >> in) || in == "q") return 0;
            size_t idx = static_cast<size_t>(std::atoi(in.c_str()));
            if (idx >= cmds.size() || !g.Select(cmds[idx])) std::printf("  (not allowed)\n");
        }
        Flush(g, shown);
        const StageResults& r = g.results();
        std::printf("Stage %s: score %d rank %d par diff %d gold +%d (total %d)\n", r.complete ? "complete" : "failed", r.score,
                    r.rank, r.parDifference, g.goldEarned(), run.gold);
        if (g.gameEndedInDefeat || !run.AdvanceStage()) break;
    }
    std::printf("Final score %d\n", run.GetFinalScoreTotal());
    return 0;
}
