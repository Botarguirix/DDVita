// Session-level state: HumanPlayer stats, HumanTeam, Gold, Score, BallMods, LeagueManager.
#include <cmath>
#include <cstdio>

#include "sim/sim.h"

namespace dd::sim {

float Distance(V2 a, V2 b) {
    float dx = static_cast<float>(a.x - b.x);
    float dy = static_cast<float>(a.y - b.y);
    return static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
}

int RoundToInt(float f) { return static_cast<int>(std::nearbyint(static_cast<double>(f))); }

// ---------------------------------------------------------------------------------------------
// HumanPlayer
// ---------------------------------------------------------------------------------------------
HumanPlayer::HumanPlayer(Run* run, const HumanPlayerSeed& s)
    : run(run), seed(s), playerNumber(s.playerNumber), playerName(s.name), playerClassConfig(s.playerClassConfig),
      perkChain(s.perkChain) {
    CachePerkConfig();
}

void HumanPlayer::CachePerkConfig() { perkConfig = run->data.GetPerkFromPerkChain(perkChain, perkLevel); }

int HumanPlayer::GetMaxStamina() const {
    const LevelUpConfig& l = run->data.levelUp;
    int num = l.staminaBase;
    for (int i = 0; i < staminaLevel; i++) num += l.staminaUpgrades[i].statBuff;
    num += run->team->traitStamina();
    if (num < 1) num = 1;
    return num;
}

int HumanPlayer::GetMaxAP() const {
    const LevelUpConfig& l = run->data.levelUp;
    int num = l.actionPointBase;
    if (run->isActionMode()) num++;
    for (int i = 0; i < apLevel; i++) num += l.actionPointUpgrades[i].statBuff;
    num += run->team->traitAP();
    if (num < 1) num = 1;
    return num;
}

int HumanPlayer::GetRestStaminaReplenishment() const { return run->data.levelUp.restBase + PerkValue(Perk::RestBoost); }

int HumanPlayer::GetAttackStrength() const {
    const LevelUpConfig& l = run->data.levelUp;
    int num = l.strengthBase;
    for (int i = 0; i < strengthLevel; i++) num += l.strengthUpgrades[i].statBuff;
    num += PerkValue(Perk::DamageBonus);
    bool playerHasBall = playerReference && playerReference->isHoldingBall();
    if (playerHasBall) num += PerkValue(Perk::DamageBoostWithBall);
    if (HasPerk(Perk::DamageBoostSurrounded)) {
        int adjacent = playerReference ? static_cast<int>(playerReference->adjacentMonsters.size()) : 0;
        int cap = PerkValue(Perk::DamageBoostSurrounded);
        if (adjacent > cap) adjacent = cap;
        num += adjacent;
    }
    if (HasPerk(Perk::BloodBoostsDamage) && playerReference && playerReference->currentFieldTile()->hasBlood())
        num += PerkValue(Perk::BloodBoostsDamage);
    if (HasPerk(Perk::KillsBoostDamage)) {
        int kills = vampireKills;
        int cap = PerkValue(Perk::KillsBoostDamage);
        if (vampireKills > cap) kills = cap;
        num += kills;
    }
    num += run->team->traitStrength();
    if (num < 0) num = 0;
    return num;
}

int HumanPlayer::GetAttackKnockback() const {
    int k = run->data.levelUp.knockbackBase + PerkValue(Perk::Knockback) + run->team->traitKnockback();
    return k < 0 ? 0 : k;
}

int HumanPlayer::GetAttackStun() const {
    return run->data.levelUp.stunBase + PerkValue(Perk::Stun) + run->team->traitStun();
}

int HumanPlayer::GetAttackRange() const {
    if (HasPerk(Perk::PierceAttack)) return PerkValue(Perk::PierceAttack);
    if (HasPerk(Perk::ChargeAttack)) return PerkValue(Perk::ChargeAttack);
    return 0;
}

int HumanPlayer::GetBallDamage() const {
    int d = run->data.levelUp.ballDamageBase + PerkValue(Perk::BallDamage);
    return d < 0 ? 0 : d;
}

int HumanPlayer::GetBallKnockback() const {
    int k = run->data.levelUp.ballKnockbackBase + PerkValue(Perk::BallKnockback);
    return k < 0 ? 0 : k;
}

int HumanPlayer::GetBallStun() const { return run->data.levelUp.ballStunBase; }

// ---------------------------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------------------------
Run::Run(const GameData& d, Session s, const TeamConfig* t, GameMode m)
    : data(d), session(std::move(s)), team(t), mode(m), halloweenRng(session.seed) {
    // HumanTeam.NewGame: the first two recruits form the team, the rest are reserves.
    for (const HumanPlayerSeed& hs : session.players) players.push_back(std::make_unique<HumanPlayer>(this, hs));
    for (size_t i = 0; i < players.size(); i++) {
        HumanPlayer* hp = players[i].get();
        if (i < 2) {
            hp->active = true;
            activePlayers.push_back(hp);
        } else {
            hp->active = false;
            hp->dead = false;
            reservePlayers.push_back(hp);
        }
    }
}

const BallModifierConfig* Run::BallModConfig() const {
    const SessionConfig& c = data.session;
    if (team->traitBlockBallMods()) return c.ballModifierDefault;
    int stage = currentStageNumberAsOverallStage();
    switch (ballEffect) {
    case DifficultyEffect::Easier:
        return stage < static_cast<int>(c.ballModsEasier.size()) ? c.ballModsEasier[stage] : c.ballModifierDefault;
    case DifficultyEffect::Harder:
        return stage < static_cast<int>(c.ballModsHarder.size()) ? c.ballModsHarder[stage] : c.ballModifierDefault;
    default:
        return c.ballModifierDefault;
    }
}

void Run::PlayerDied(int number) {
    HumanPlayer* hp = players[number].get();
    hp->dead = true;
    hp->active = false;
    hp->stageDiedIndex = currentStageNumberAsOverallStage();
    deadPlayers.push_back(hp);
    for (auto it = activePlayers.begin(); it != activePlayers.end(); ++it) {
        if (*it == hp) {
            activePlayers.erase(it);
            break;
        }
    }
}

void Run::ResetStagePlayerReferences() {
    for (HumanPlayer* hp : activePlayers) hp->playerReference = nullptr;
    for (HumanPlayer* hp : deadPlayers) hp->playerReference = nullptr;
}

int Run::GetStageScores() const {
    int total = 0;
    for (int i = 0; i < stagesPlayed && i < 12; i++) total += stageResults[i].score;
    return total;
}

int Run::GetFinalScoreTotal() const {
    int bonus = 0;
    if (currentStageIsFinalStageInFinalTrial()) {
        for (const HumanPlayer* hp : activePlayers) {
            if (hp->dead) continue;
            bonus += (12 - hp->stageStartedIndex) * data.scoring.finalSurvivalBonusPerStage;
        }
    }
    return GetStageScores() + bonus;
}

bool Run::AdvanceStage() {
    if (currentStage < 2) {
        currentStage++;
        return true;
    }
    if (currentTrial < 3) {
        currentTrial++;
        currentStage = 0;
        return true;
    }
    return false;
}

} // namespace dd::sim
