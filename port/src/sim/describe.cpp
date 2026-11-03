// Text form of presentation events (console player, logs, determinism tests).
#include <cstdio>

#include "sim/sim.h"

namespace dd::sim {

namespace {
std::string Pos(V2 p) {
    char b[24];
    std::snprintf(b, sizeof b, "(%d,%d)", p.x, p.y);
    return b;
}
std::string Who(const Game& g, int uid) {
    if (uid < 0) return "-";
    Entity* e = g.FindEntity(uid);
    if (!e) return "#" + std::to_string(uid);
    return e->Name() + "#" + std::to_string(uid);
}
} // namespace

std::string DescribeEvent(const Game& g, const Event& e) {
    std::string a = Who(g, e.actor);
    switch (e.type) {
    case Ev::TurnStarted: return "turn " + std::to_string(e.value);
    case Ev::PhaseChanged: return std::string("phase ") + PhaseName(static_cast<Phase>(e.value));
    case Ev::CurrentPlayer: return "current " + a;
    case Ev::Move: return a + " moves " + Pos(e.from) + "->" + Pos(e.to);
    case Ev::Attack: return a + " attacks " + Pos(e.to);
    case Ev::Hit: return a + " hit for " + std::to_string(e.value) + " by " + Who(g, e.extra);
    case Ev::Died: return a + " dies";
    case Ev::Stunned: return a + " stunned " + std::to_string(e.value);
    case Ev::StunEnded: return a + " recovers";
    case Ev::Healed: return a + " healed to " + std::to_string(e.value);
    case Ev::Stamina: return a + " stamina " + std::to_string(e.value);
    case Ev::BallShot: return a + " shoots " + Pos(e.from) + "->" + Pos(e.to);
    case Ev::BallPass: return a + " passes " + Pos(e.from) + "->" + Pos(e.to);
    case Ev::BallRebound: return "ball rebounds " + Pos(e.from) + "->" + Pos(e.to);
    case Ev::BallDropped: return "ball dropped at " + Pos(e.to);
    case Ev::BallAcquired: return a + " gets the ball";
    case Ev::TreasureTaken: return a + " takes treasure at " + Pos(e.to);
    case Ev::TrickOrTreat: return "trick-or-treat " + std::to_string(e.value);
    case Ev::Spawned: return a + " spawned at " + Pos(e.to);
    case Ev::SpawnBlocked: return "spawn blocked at " + Pos(e.to);
    case Ev::HazardTriggered: return "hazard at " + Pos(e.to);
    case Ev::LightningRow: return "lightning column " + std::to_string(e.value);
    case Ev::Intention: return a + " intends " + std::string(ToString(static_cast<ActorAction>(e.value))) + " " + Pos(e.to);
    case Ev::Undo: return "undo";
    case Ev::StageWon: return "STAGE WON";
    case Ev::StageLost: return "STAGE LOST";
    case Ev::Log: return e.text;
    }
    return "?";
}

} // namespace dd::sim
