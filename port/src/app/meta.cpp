// Persistent progress (the original's SaveManager "meta" data): GlobalStatTracker, the achievements
// (AchievementManagerDeathball, kept locally: homebrew cannot award trophies), Skull Keys, Teams and
// Unlocks. Saved in meta.json.
//
// The stage being played reports through sim::MetaSink; replays of saved stages have no sink, so
// nothing is counted twice.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

#include "app/app.h"

namespace dd::app {

using json = nlohmann::json;
using namespace dd::sim;

namespace {
constexpr int kVersion = 1;
constexpr int kStages = 12; // LeagueManager.totalNumberOfStages

const Enemy* AsEnemy(const Game& g, int uid) {
    const Entity* e = uid >= 0 ? g.FindEntity(uid) : nullptr;
    return e && e->entityType == EntityType::Enemy ? static_cast<const Enemy*>(e) : nullptr;
}
EnemyType TypeOf(const Enemy* e) { return e && e->config ? e->config->enemyType : EnemyType::UNDEFINED; }
bool IsMaw(EnemyType t) { return t == EnemyType::MawPotted || t == EnemyType::MawRooted; }
} // namespace

Meta::Meta(const GameData& data) : data_(data) {
    teams.resize(data.teams.size());
    if (!teams.empty()) teams[0].unlocked = true;
}

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------
bool Meta::LoadDefs(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return false;
    auto str = [](const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    for (const json& a : j.value("achievements", json::array())) {
        AchievementDef d;
        d.id = static_cast<Achievement>(a.value("id", 0));
        d.name = str(a, "name");
        d.nameFallback = str(a, "nameText");
        d.desc = str(a, "desc");
        d.descFallback = str(a, "descText");
        d.icon = str(a, "icon");
        d.iconLocked = str(a, "iconLocked");
        d.hidden = a.value("hidden", false);
        d.targetValue = a.value("targetValue", 0);
        d.progress = a.value("statType", 0) == 2; // AchievementConfig.StatType.Progression
        defs.push_back(std::move(d));
    }
    lockedIcon = str(j, "lockedIcon");
    return !defs.empty();
}

bool Meta::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object() || j.value("version", 0) != kVersion) return false;
    for (const json& a : j.value("achievements", json::array()))
        if (a.is_number_integer() && a.get<int>() > 0 && a.get<int>() < static_cast<int>(Achievement::Count)) unlocked[a.get<int>()] = true;
    const json st = j.value("stats", json::array());
    for (size_t i = 0; i < st.size() && i < static_cast<size_t>(Stat::Count); i++) stats[i] = st[i].get<long long>();
    const json tm = j.value("teams", json::array());
    for (size_t i = 0; i < tm.size() && i < teams.size(); i++) {
        TeamProgress& t = teams[i];
        t.unlocked = i == 0 || tm[i].value("unlocked", false);
        t.stagesCompleted = tm[i].value("stagesCompleted", 0);
        t.keysAcquired = tm[i].value("keysAcquired", 0);
        const json b = tm[i].value("best", json::array());
        for (size_t m = 0; m < b.size() && m < 4; m++) t.best[m] = b[m];
    }
    keys = j.value("keys", 0);
    lastDailyChallengeSeed = j.value("lastDailyChallengeSeed", 0);
    highScoreDaily = j.value("highScoreDaily", 0);
    const json cm = j.value("completedMode", json::array());
    for (size_t i = 0; i < cm.size() && i < 4; i++) completedMode[i] = cm[i];
    completedHalloween = j.value("completedHalloween", false);
    const json sm = j.value("seenModeUnlock", json::array());
    for (size_t i = 0; i < sm.size() && i < 4; i++) seenModeUnlock[i] = sm[i];
    const json sp = j.value("seenMusicPack", json::array());
    for (size_t i = 0; i < sp.size() && i < 4; i++) seenMusicPack[i] = sp[i];
    dirty = false;
    return true;
}

bool Meta::Save(const std::string& path) {
    json j;
    j["version"] = kVersion;
    json a = json::array();
    for (int i = 1; i < static_cast<int>(Achievement::Count); i++)
        if (unlocked[i]) a.push_back(i);
    j["achievements"] = std::move(a);
    j["stats"] = std::vector<long long>(stats, stats + static_cast<int>(Stat::Count));
    json tm = json::array();
    for (const TeamProgress& t : teams)
        tm.push_back({{"unlocked", t.unlocked},
                      {"stagesCompleted", t.stagesCompleted},
                      {"keysAcquired", t.keysAcquired},
                      {"best", {t.best[0], t.best[1], t.best[2], t.best[3]}}});
    j["teams"] = std::move(tm);
    j["keys"] = keys;
    j["lastDailyChallengeSeed"] = lastDailyChallengeSeed;
    j["highScoreDaily"] = highScoreDaily;
    j["completedMode"] = {completedMode[0], completedMode[1], completedMode[2], completedMode[3]};
    j["completedHalloween"] = completedHalloween;
    j["seenModeUnlock"] = {seenModeUnlock[0], seenModeUnlock[1], seenModeUnlock[2], seenModeUnlock[3]};
    j["seenMusicPack"] = {seenMusicPack[0], seenMusicPack[1], seenMusicPack[2], seenMusicPack[3]};
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump();
        if (!f) return false;
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    dirty = false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Achievements and stats
// ---------------------------------------------------------------------------------------------
const AchievementDef* Meta::Def(Achievement a) const {
    for (const AchievementDef& d : defs)
        if (d.id == a) return &d;
    return nullptr;
}

void Meta::Unlock(Achievement a) {
    int i = static_cast<int>(a);
    if (i <= 0 || i >= static_cast<int>(Achievement::Count) || unlocked[i]) return;
    unlocked[i] = true;
    dirty = true;
    newlyUnlocked.push_back(a);
}

void Meta::Progress(Achievement a, long long value) {
    const AchievementDef* d = Def(a);
    if (d && value >= d->targetValue) Unlock(a);
}

int Meta::UnlockedCount() const {
    int n = 0;
    for (int i = 1; i < static_cast<int>(Achievement::Count); i++) n += unlocked[i];
    return n;
}

// AchievementManagerDeathball.GetProgressValue (the counters; per-run ones show 0 outside a run)
long long Meta::ProgressValue(const AchievementDef& d) const {
    switch (d.id) {
    case Achievement::StatBlockSpawns: return StatValue(Stat::SpawnsBlocked);
    case Achievement::StatEnemyKillEnemies: return StatValue(Stat::FriendlyFire);
    case Achievement::StatEscapees: return StatValue(Stat::Escapees);
    case Achievement::StatGameWins: return StatValue(Stat::GameWins);
    case Achievement::StatGems: return StatValue(Stat::GemsAcquired);
    case Achievement::StatGold: return StatValue(Stat::GoldAcquired);
    case Achievement::StatKills: return StatValue(Stat::Kills);
    case Achievement::StatMove1:
    case Achievement::StatMove2: return StatValue(Stat::TilesMoved);
    case Achievement::StatPass: return StatValue(Stat::Passes);
    case Achievement::StatPushRocks: return StatValue(Stat::RocksPushed);
    case Achievement::StatRankA: return StatValue(Stat::StageWinsRankA);
    case Achievement::StatRest: return StatValue(Stat::Rests);
    case Achievement::StatStageWins: return StatValue(Stat::StageWins);
    case Achievement::StatDailyChallenges: return StatValue(Stat::DailyChallenges);
    case Achievement::StatTouchdowns: return StatValue(Stat::StageWinsTouchdowns);
    case Achievement::CompleteWithEveryTeam: return StatValue(Stat::TeamsCompleted);
    default: return 0;
    }
}

// GlobalStatTracker.CheckAchievements
void Meta::AddStat(Stat s, long long n) {
    stats[static_cast<int>(s)] += n;
    dirty = true;
    Progress(Achievement::StatPass, StatValue(Stat::Passes));
    Progress(Achievement::StatRest, StatValue(Stat::Rests));
    Progress(Achievement::StatKills, StatValue(Stat::Kills));
    Progress(Achievement::StatPushRocks, StatValue(Stat::RocksPushed));
    Progress(Achievement::StatBlockSpawns, StatValue(Stat::SpawnsBlocked));
    Progress(Achievement::StatGold, StatValue(Stat::GoldAcquired));
    Progress(Achievement::StatGems, StatValue(Stat::GemsAcquired));
    Progress(Achievement::StatRankA, StatValue(Stat::StageWinsRankA));
    Progress(Achievement::StatMove1, StatValue(Stat::TilesMoved));
    Progress(Achievement::StatMove2, StatValue(Stat::TilesMoved));
    Progress(Achievement::StatEscapees, StatValue(Stat::Escapees));
    Progress(Achievement::StatStageWins, StatValue(Stat::StageWins));
    Progress(Achievement::StatGameWins, StatValue(Stat::GameWins));
    Progress(Achievement::StatEnemyKillEnemies, StatValue(Stat::FriendlyFire));
    Progress(Achievement::StatTouchdowns, StatValue(Stat::StageWinsTouchdowns));
    Progress(Achievement::StatDailyChallenges, StatValue(Stat::DailyChallenges));
    Progress(Achievement::CompleteWithEveryTeam, StatValue(Stat::TeamsCompleted));
}

// ---------------------------------------------------------------------------------------------
// Unlocks
// ---------------------------------------------------------------------------------------------
bool Meta::ModeUnlocked(const GameModeConfig* m) const {
    if (!m) return false;
    if (UnlockedCount() < m->unlockCriteriaAchievementCount) return false;
    for (int a : m->unlockCriteriaAchievements)
        if (a > 0 && a < static_cast<int>(Achievement::Count) && !unlocked[a]) return false;
    return true;
}

bool Meta::MusicPackUnlocked(int pack) const {
    switch (pack) {
    case 0: return true;
    case 1: return ModeComplete(GameMode::Normal) || ModeComplete(GameMode::DailyChallenge);
    case 2: return ModeComplete(GameMode::Boss);
    case 3: return ModeComplete(GameMode::Action);
    default: return false;
    }
}

int Meta::MusicPacksUnlocked() const {
    int n = 0;
    for (int p = 0; p < 4; p++) n += MusicPackUnlocked(p);
    return n;
}

bool Meta::TeamSelectionLocked() const {
    for (size_t i = 1; i < teams.size(); i++)
        if (teams[i].unlocked) return false;
    return keys < 1;
}

bool Meta::TeamUnlockable(int team) const {
    if (team < 0 || team >= static_cast<int>(data_.teams.size())) return false;
    int a = data_.teams[static_cast<size_t>(team)]->requiredAchievement;
    return a <= 0 || a >= static_cast<int>(Achievement::Count) || unlocked[a];
}

bool Meta::TryUnlockTeam(int team) {
    if (team < 0 || team >= static_cast<int>(teams.size()) || teams[static_cast<size_t>(team)].unlocked || !TeamUnlockable(team)) return false;
    int cost = data_.teams[static_cast<size_t>(team)]->unlockCost;
    if (cost > keys) return false;
    keys -= cost;
    teams[static_cast<size_t>(team)].unlocked = true;
    dirty = true;
    Unlock(Achievement::UnlockTeam);
    return true;
}

void Meta::StartedNewDailyChallenge(int seed) {
    if (lastDailyChallengeSeed != seed) AddStat(Stat::DailyChallenges, 1);
    lastDailyChallengeSeed = seed;
    dirty = true;
}

int Meta::TeamsCompleted() const {
    int n = 0;
    for (const TeamProgress& t : teams) n += t.stagesCompleted >= kStages;
    return n;
}

int Meta::BestScore(int team, GameMode mode) const {
    if (mode == GameMode::DailyChallenge) return highScoreDaily;
    if (team < 0 || team >= static_cast<int>(teams.size())) return 0;
    return teams[static_cast<size_t>(team)].best[static_cast<int>(mode) & 3];
}

// Score.TryHighScore / Teams.TryTeamHighScore at the end of a run
void Meta::RunEnded(const Run& run, int team) {
    int score = run.GetFinalScoreTotal();
    newHighScore = false;
    if (run.mode == GameMode::DailyChallenge) {
        const SeasonalEventConfig* sc = data_.SeasonalConfig(run.session.seasonalEvent);
        if (sc && sc->preventHighScoreSaving) return;
        if (score > highScoreDaily) highScoreDaily = score, newHighScore = true;
    } else if (team >= 0 && team < static_cast<int>(teams.size())) {
        int& best = teams[static_cast<size_t>(team)].best[static_cast<int>(run.mode) & 3];
        if (score > best) best = score, newHighScore = true;
    }
    dirty = true;
}

// ---------------------------------------------------------------------------------------------
// The stage being played
// ---------------------------------------------------------------------------------------------
void Meta::NewStage(const Game& g) {
    stage_ = &g;
    enemiesHit_ = enemiesKilled_ = spikeKills_ = 0;
    turnsWithPasses_.clear();
    lastAttacker_.clear();
    enemyHitsThisTurn_.clear();
    tilesMovedThisTurn_.clear();
    surrounded_.clear();
    attacker_ = shooter_ = ballKillShooter_ = -1;
    attackKills_ = 0;
    shotToFinishLine_ = enemyHadBall_ = ballRecovered_ = false;
    lastPhase_ = Phase::Starting;
    const Run& run = g.run;
    if (run.currentStageNumberAsOverallStage() == 0) goaliesLeftGame_ = false; // AchievementManagerDeathball.NewGame
    if (run.currentStage == 0) goaliesLeftTrial_ = false;                     // NewTrial
    finalStageOnePlayer_ = run.currentStageIsFinalStageInFinalTrial() && g.players.size() == 1;
}

void Meta::OnEvent(const Game& g, const Event& e) {
    if (stage_ != &g) NewStage(g);
    switch (e.type) {
    case Ev::TurnStarted:
        turnsWithPasses_.push_back(false); // AchievementManagerDeathball.NewTurn
        enemyHitsThisTurn_.clear();
        tilesMovedThisTurn_.clear();
        CheckSurrounded(g);
        break;
    case Ev::PhaseChanged:
        if (static_cast<Phase>(e.value) != Phase::StageEnded) lastPhase_ = static_cast<Phase>(e.value);
        if (static_cast<Phase>(e.value) == Phase::PlayerInput) CheckSurrounded(g);
        break;
    case Ev::Attack: // a new attack: its kills count together (KillMultipleWithOneAttack / KappaTwoKills)
        attacker_ = e.actor;
        attackKills_ = 0;
        break;
    case Ev::BallShot:
        shooter_ = e.actor;
        ballKillShooter_ = -1;
        reboundDist_ = -1;
        if (e.to.x >= g.fieldX - 1) shotToFinishLine_ = true; // Ball.shotToFinishLine
        break;
    case Ev::BallPass:
        // Ball.GA_Ball_Pass: from one corner of the first column to the opposite one by the goal
        if (e.from.x == 0 && e.to.x == g.fieldX - 2 &&
            ((e.from.y == 0 && e.to.y == g.fieldY - 1) || (e.from.y == g.fieldY - 1 && e.to.y == 0)))
            Unlock(Achievement::PassOppositeCorners);
        break;
    case Ev::BallRebound:
        // Ball.GA_Ball_Shoot: the shooter catches the rebound of a killing shot
        reboundTo_ = e.to;
        reboundDist_ = e.from.x - e.to.x;
        if (ballKillShooter_ >= 0 && g.PositionIsWithinField(e.to)) {
            const FieldTile* t = const_cast<Game&>(g).GetTile(e.to);
            if (t->occupant && t->occupant->uid == ballKillShooter_) Progress(Achievement::KillThenRebound, e.from.x - e.to.x);
        }
        break;
    case Ev::BallAcquired: {
        const Entity* a = g.FindEntity(e.actor);
        if (a && a->entityType == EntityType::Enemy) enemyHadBall_ = true;
        else if (a && a->entityType == EntityType::Player && enemyHadBall_) ballRecovered_ = true; // Ball.ballWasRecoveredFromEnemies
        break;
    }
    case Ev::Hit: {
        // AchievementManagerDeathball.ActorWasHit
        lastAttacker_[e.actor] = e.extra;
        const Entity* by = e.extra > 0 ? g.FindEntity(e.extra) : nullptr;
        if (by && by->entityType == EntityType::Ball) Progress(Achievement::DealFiveDamageWithBall, e.value); // Ball shot damage
        const Enemy* victim = AsEnemy(g, e.actor);
        if (!victim || e.extra <= 0) break; // Hit: extra 0 = no attacker
        enemiesHit_++;
        if (const Enemy* attacker = AsEnemy(g, e.extra)) {
            Progress(Achievement::EnemyHitByTwoEnemies, ++enemyHitsThisTurn_[e.actor]); // Enemy.OnHitStart
            Unlock(Achievement::EnemyHitOtherEnemy);
            if (TypeOf(victim) == EnemyType::Kappa && TypeOf(attacker) == EnemyType::Kappa) Unlock(Achievement::KappaHitKappa);
        }
        break;
    }
    case Ev::Died: {
        const Entity* dead = g.FindEntity(e.actor);
        if (!dead) break;
        CheckBlood(g);
        if (dead->entityType == EntityType::Player) {
            AddStat(Stat::Deaths, 1);
            break;
        }
        if (AsEnemy(g, e.actor)) AddStat(Stat::Kills, 1); // Enemy.GA_Actor_Die
        // AchievementManagerDeathball.ActorDied
        int attackerUid = e.extra;
        if (attackerUid < 0) break;
        const Entity* attacker = g.FindEntity(attackerUid);
        if (!attacker) break;
        if (const Enemy* victim = AsEnemy(g, e.actor)) {
            enemiesKilled_++;
            Unlock(Achievement::KillAnEnemy);
            // kills of one attack (Attack.Attack_Coroutine)
            if (attackerUid == attacker_) {
                attackKills_++;
                if (attacker->entityType == EntityType::Player) Progress(Achievement::KillMultipleWithOneAttack, attackKills_);
                else if (TypeOf(AsEnemy(g, attackerUid)) == EnemyType::Kappa) Progress(Achievement::KappaTwoKills, attackKills_);
            }
            if (attacker->entityType == EntityType::Ball) {
                ballKillShooter_ = shooter_;
                if (reboundDist_ > 0 && g.PositionIsWithinField(reboundTo_)) {
                    const FieldTile* t = const_cast<Game&>(g).GetTile(reboundTo_);
                    if (t->occupant && t->occupant->uid == shooter_) Progress(Achievement::KillThenRebound, reboundDist_);
                }
            }
            if (TypeOf(victim) == EnemyType::Hatchery && victim->cSpawning && !victim->cSpawning->spawnedAnyChildren)
                Unlock(Achievement::StopHatcherySpawning);
            if (victim->isGoalieEnemy()) {
                Unlock(Achievement::KillGoalie);
                // a ball shot from the first column killing a goalie on the last
                if (e.value && attacker->entityType == EntityType::Player &&
                    static_cast<const Actor*>(attacker)->gridPos.x == 0 && e.to.x == g.fieldX - 1)
                    Unlock(Achievement::KillGoalieFromOppositeEnd);
            }
            EnemyType t = TypeOf(victim);
            if (t == EnemyType::Brutolith) Unlock(Achievement::KillBrutolith);
            else if (t == EnemyType::Judgment) Unlock(Achievement::KillFinalBoss);
            if (const Enemy* ea = AsEnemy(g, attackerUid)) {
                AddStat(Stat::FriendlyFire, 1);
                if (TypeOf(ea) == EnemyType::Redhorn) Unlock(Achievement::DodgeCharger);
                else if (IsMaw(TypeOf(ea))) Unlock(Achievement::MawKillOtherEnemy);
            } else if (attacker->entityType == EntityType::Hazard) { // the spike trap
                spikeKills_++;
                Progress(Achievement::KillMultipleWithSpikes, spikeKills_);
                if (t == EnemyType::Snaptor || t == EnemyType::Redhorn) Unlock(Achievement::KillDinoWithTrap);
            }
        } else if (dead->entityType == EntityType::Obstacle && AsEnemy(g, attackerUid)) {
            Unlock(Achievement::EnemyBreakRock);
        }
        break;
    }
    case Ev::Move: {
        const Entity* a = g.FindEntity(e.actor);
        if (a && a->entityType == EntityType::Obstacle) {
            AddStat(Stat::RocksPushed, 1); // BreakableWall pushed
            CheckTrappedEnemies(g);
        }
        // Actor.GA_Actor_Hit: a Rooted Maw knocked back
        if (const Enemy* en = AsEnemy(g, e.actor); en && en->isRootedMaw() && !(e.from == e.to)) Unlock(Achievement::PushMaw);
        break;
    }
    case Ev::SpawnBlocked: AddStat(Stat::SpawnsBlocked, 1); break;
    case Ev::TreasureTaken: AddStat(Stat::GemsAcquired, 1); break;
    default: break;
    }
}

void Meta::OnPlayerAction(const Game& g, const Command& c) {
    if (stage_ != &g) NewStage(g);
    switch (c.actionConfig->action) {
    case ActorAction::Char_Wait: AddStat(Stat::Rests, 1); break;
    case ActorAction::Char_Step: {
        int n = std::max(1, c.distance);
        AddStat(Stat::TilesMoved, n);
        int& moved = tilesMovedThisTurn_[c.player ? c.player->uid : 0];
        moved += n;
        if (moved > 2) Progress(Achievement::MoveManyTilesInOneTurn, moved); // Player.CheckMoveManyTilesAchievement
        break;
    }
    case ActorAction::Player_Pass: {
        AddStat(Stat::Passes, 1);
        int turn = g.Turn() - 1; // AchievementManagerDeathball.PlayerPassed
        if (turn >= 0 && turn < static_cast<int>(turnsWithPasses_.size())) turnsWithPasses_[static_cast<size_t>(turn)] = true;
        break;
    }
    default: break;
    }
}

void Meta::OnStageEnded(const Game& g) {
    if (stage_ != &g) NewStage(g);
    AddStat(Stat::GoldAcquired, g.goldEarned());
    CheckBlood(g);
    if (g.gameEndedInDefeat) return;
    // GameController.PlayerWon
    if (lastPhase_ == Phase::EnemiesActing && g.BallReachedOtherSide() && g.ball->isHeldByPlayer()) Unlock(Achievement::WinStageDuringEnemyTurn);
    if (g.lightningHazardRowsSpawned >= g.fieldX - 1) Unlock(Achievement::FullLightningWin); // HazardManager.FieldIsFullOfLightning
    const StageResults& res = g.results();
    // StageScore: the stat counters of a won stage
    if (res.touchdown) AddStat(Stat::StageWinsTouchdowns, 1);
    if (res.withinPar()) AddStat(Stat::StageWinsWithinPar, 1);
    if (res.rank >= static_cast<int>(Rank::A)) AddStat(Stat::StageWinsRankA, 1);
    // Teams.StageComplete: progress and the Skull Keys of a completed trial
    const Run& run = g.run;
    if (teamIndex >= 0 && teamIndex < static_cast<int>(teams.size()) && run.mode != GameMode::DailyChallenge) {
        TeamProgress& t = teams[static_cast<size_t>(teamIndex)];
        int done = run.currentStageNumberAsOverallStage() + 1;
        if (done > t.stagesCompleted) t.stagesCompleted = done;
        if (run.currentStageIsLastInCurrentTrial()) {
            int target = (run.currentTrial + 1) * data_.teams[static_cast<size_t>(teamIndex)]->keysAwardedPerTrial;
            if (t.keysAcquired < target) {
                int add = target - t.keysAcquired;
                t.keysAcquired += add;
                keys += add;
                unreportedKeys += add;
            }
        }
    }
    // Unlocks.StageCompleteCheck
    if (run.currentStageIsFinalStageInFinalTrial()) {
        completedMode[static_cast<int>(run.mode) & 3] = true;
        if (run.mode == GameMode::DailyChallenge && run.IsHalloween()) completedHalloween = true;
    }
    dirty = true;
    StageWon(g);
}

// Player.CheckSurrounded: monsters on all four sides
void Meta::CheckSurrounded(const Game& g) {
    static const V2 dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    Game& gm = const_cast<Game&>(g);
    for (const Player* p : g.players) {
        if (!p->isAlive) continue;
        bool all = true;
        for (V2 d : dirs) {
            V2 q{p->gridPos.x + d.x, p->gridPos.y + d.y};
            if (!g.PositionIsWithinField(q) || !gm.GetTile(q)->containsEnemy()) {
                all = false;
                break;
            }
        }
        if (all && std::find(surrounded_.begin(), surrounded_.end(), p->uid) == surrounded_.end()) surrounded_.push_back(p->uid);
    }
}

// BreakableWall: an enemy boxed in by rocks (or the field's edge) on all four sides
void Meta::CheckTrappedEnemies(const Game& g) {
    static const V2 dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    Game& gm = const_cast<Game&>(g);
    for (const Enemy* e : g.enemies) {
        if (!e->isAlive) continue;
        int n = 0;
        for (V2 d : dirs) {
            V2 q{e->gridPos.x + d.x, e->gridPos.y + d.y};
            if (!g.PositionIsWithinField(q)) n++;
            else if (const Actor* o = gm.GetTile(q)->occupant; o && o->entityType == EntityType::Obstacle) n++;
        }
        if (n == 4) {
            Unlock(Achievement::TrapEnemyWithWalls);
            return;
        }
    }
}

// FieldManager: blood on three quarters of the field
void Meta::CheckBlood(const Game& g) {
    if (Unlocked(Achievement::LotsOfBlood)) return;
    int n = 0, need = static_cast<int>(std::nearbyint(g.fieldX * g.fieldY * 0.75));
    for (const FieldTile& t : g.grid) n += t.hasBlood();
    if (n >= need) Unlock(Achievement::LotsOfBlood);
}

// AchievementManagerDeathball.StageWon
void Meta::StageWon(const Game& g) {
    bool goaliesAlive = false, anyEnemy = false;
    for (const Enemy* e : g.enemies)
        if (e->isAlive) {
            anyEnemy = true;
            goaliesAlive = goaliesAlive || e->isGoalieEnemy();
        }
    if (goaliesAlive) goaliesLeftGame_ = goaliesLeftTrial_ = true;
    AddStat(Stat::StageWins, 1);
    Unlock(Achievement::WinAStage);
    if (!anyEnemy) Unlock(Achievement::KillAllEnemiesThenWin);
    if (enemiesKilled_ == 0) Unlock(Achievement::WinWithoutKilling);
    if (enemiesHit_ == 0) Unlock(Achievement::WinWithoutHitting);
    const Run& run = g.run;
    const StageResults& res = g.results();
    if (shotToFinishLine_ && res.touchdown) Unlock(Achievement::LongShotThenTouchdown);
    if (ballRecovered_ && res.touchdown) Unlock(Achievement::ReclaimBallThenTouchdown);
    for (int uid : surrounded_)
        if (const Entity* p = g.FindEntity(uid); p && static_cast<const Actor*>(p)->isAlive) {
            Unlock(Achievement::SurviveSurrounded);
            break;
        }
    if (res.rank == static_cast<int>(Rank::S)) Unlock(Achievement::RankS);
    // GetLongestConsecutiveSRankWins
    int run_ = 0, best = 0;
    for (int i = 0; i <= run.currentStageNumberAsOverallStage() && i < kStages; i++) {
        bool s = i == run.currentStageNumberAsOverallStage() ? res.rank == static_cast<int>(Rank::S)
                                                                : run.stageResults[i].rank == static_cast<int>(Rank::S);
        run_ = s ? run_ + 1 : 0;
        best = std::max(best, run_);
    }
    Progress(Achievement::RankSConsecutively, best);
    if (std::find(turnsWithPasses_.begin(), turnsWithPasses_.end(), false) == turnsWithPasses_.end())
        Unlock(Achievement::PassBallEveryTurn);
    if (finalStageOnePlayer_) Unlock(Achievement::FinalStageOnePlayer);
    if (run.currentStageIsLastInCurrentTrial()) {
        // TrialComplete
        switch (run.currentTrial) {
        case 0: Unlock(Achievement::CompleteTrial1); break;
        case 1: Unlock(Achievement::CompleteTrial2); break;
        case 2: Unlock(Achievement::CompleteTrial3); break;
        default: GameComplete(g); break;
        }
        if (!goaliesLeftTrial_) Unlock(Achievement::KillAllGoaliesInTrial);
    }
}

// AchievementManagerDeathball.GameComplete
void Meta::GameComplete(const Game& g) {
    const Run& run = g.run;
    int live = g.GetNumberOfLivePlayers();
    AddStat(Stat::GameWins, 1);
    AddStat(Stat::Escapees, live);
    Unlock(Achievement::CompleteGame);
    switch (run.mode) {
    case GameMode::DailyChallenge:
        Unlock(Achievement::CompleteDailyChallenge);
        if (run.session.seasonalEvent != SeasonalEvent::None) Unlock(Achievement::CompleteSeasonalEvent);
        break;
    case GameMode::Boss: Unlock(Achievement::CompleteBossMode); break;
    case GameMode::Action: Unlock(Achievement::CompleteActionMode); break;
    default: break;
    }
    if (!goaliesLeftGame_) Unlock(Achievement::KillAllGoaliesInGame);
    if (run.deadPlayers.empty()) Unlock(Achievement::CompleteGameNoDeaths);
    // SessionStatTracker over the twelve stages (this one's results are already in run.stageResults)
    bool hard = true, rankS = true, td = true, par = true, speed = true, gems = true;
    for (int i = 0; i < kStages; i++) {
        const StageResults& r = run.stageResults[i];
        hard = hard && r.ballModifier == static_cast<int>(DifficultyEffect::Harder);
        rankS = rankS && r.rank == static_cast<int>(Rank::S);
        td = td && r.touchdown;
        par = par && r.withinPar();
        speed = speed && r.speedTarget;
        gems = gems && r.gemTarget;
    }
    if (hard) Unlock(Achievement::HardBallsOnly);
    if (rankS) Unlock(Achievement::RankSEveryStage);
    if (td) Unlock(Achievement::EveryStageTouchdown);
    if (par) Unlock(Achievement::EveryStageWithinPar);
    if (speed) Unlock(Achievement::EveryStageSpeed);
    if (gems) Unlock(Achievement::EveryStageGems);
    int score = run.GetFinalScoreTotal();
    Progress(Achievement::FinalScore, score);
    Progress(Achievement::FinalScore2, score);
    if (g.players.size() == 2) {
        int a = g.players[0]->humanPlayer->playerNumber, b = g.players[1]->humanPlayer->playerNumber;
        if (a > 1 && b > 1) Unlock(Achievement::ReplaceFullTeam);
    }
    // CheckAllUpgradesInCompleteRun
    bool stamina = false, strength = false, ap = false, perk = false;
    auto check = [&](const std::vector<HumanPlayer*>& list) {
        for (const HumanPlayer* hp : list) {
            stamina = stamina || hp->staminaLevel > 0;
            strength = strength || hp->strengthLevel > 0;
            ap = ap || hp->apLevel > 0;
            perk = perk || hp->perkLevel > 0;
        }
    };
    check(run.activePlayers);
    check(run.deadPlayers);
    if (!stamina) Unlock(Achievement::NoStaminaUpgrades);
    if (!strength) Unlock(Achievement::NoStrengthUpgrades);
    if (!stamina && !strength && !ap && !perk) Unlock(Achievement::NoUpgrades);
    if (teamIndex != 0 && run.mode != GameMode::DailyChallenge) Unlock(Achievement::CompleteWithUnlockedTeam);
    long long done = TeamsCompleted();
    if (done > StatValue(Stat::TeamsCompleted)) stats[static_cast<int>(Stat::TeamsCompleted)] = done;
    Progress(Achievement::CompleteWithEveryTeam, StatValue(Stat::TeamsCompleted));
}

} // namespace dd::app
