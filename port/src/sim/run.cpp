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

int HumanPlayer::GetStatLevel(PlayerStat s) const {
    switch (s) {
    case PlayerStat::Stamina: return staminaLevel;
    case PlayerStat::AP: return apLevel;
    case PlayerStat::Strength: return strengthLevel;
    case PlayerStat::Perk: return perkLevel;
    default: return 0;
    }
}

// LevelUpConfig.GetStatLevelCap: the class limit, never beyond the upgrade table
int HumanPlayer::GetStatLevelCap(PlayerStat s) const {
    const LevelUpConfig& l = run->data.levelUp;
    auto cap = [](int table, int cls) { return cls > table ? table : cls; };
    switch (s) {
    case PlayerStat::Perk: return static_cast<int>(perkChain->perks.size());
    case PlayerStat::Stamina: return cap(static_cast<int>(l.staminaUpgrades.size()), playerClassConfig->maxLevelStamina);
    case PlayerStat::AP: return cap(static_cast<int>(l.actionPointUpgrades.size()), playerClassConfig->maxLevelActionPoints);
    case PlayerStat::Strength: return cap(static_cast<int>(l.strengthUpgrades.size()), playerClassConfig->maxLevelStrength);
    default: return 0;
    }
}

int HumanPlayer::GetNextUpgradePrice(PlayerStat s) const {
    const LevelUpConfig& l = run->data.levelUp;
    switch (s) {
    case PlayerStat::Perk: return perkChain->perks[static_cast<size_t>(perkLevel)]->perkAdditionalCost * l.perkCostMultiplier;
    case PlayerStat::Stamina: return l.staminaUpgrades[static_cast<size_t>(staminaLevel)].price;
    case PlayerStat::Strength: return l.strengthUpgrades[static_cast<size_t>(strengthLevel)].price;
    default: return 0;
    }
}

int HumanPlayer::GetStatValue(PlayerStat s) const {
    switch (s) {
    case PlayerStat::Stamina: return GetMaxStamina();
    case PlayerStat::Strength: return GetAttackStrength();
    case PlayerStat::Perk: return GetPerkStatValue();
    default: return 0;
    }
}

bool HumanPlayer::AffordableUpgradeAvailable(PlayerStat s) const {
    return !StatIsMaxLevel(s) && run->gold >= GetNextUpgradePrice(s);
}

void HumanPlayer::LevelUpPlayerStat(PlayerStat s) {
    if (StatIsMaxLevel(s)) return;
    switch (s) {
    case PlayerStat::Stamina: staminaLevel++; break;
    case PlayerStat::AP: apLevel++; break;
    case PlayerStat::Strength: strengthLevel++; break;
    case PlayerStat::Perk:
        perkLevel++;
        CachePerkConfig();
        break;
    default: break;
    }
}

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

// StageEndUI.AffordableUpgradesOrRecruitsAvailable
bool Run::AffordableUpgradesOrRecruitsAvailable() const {
    for (const HumanPlayer* hp : activePlayers)
        if (hp->AffordableUpgradeAvailable(PlayerStat::Stamina) || hp->AffordableUpgradeAvailable(PlayerStat::Strength) ||
            hp->AffordableUpgradeAvailable(PlayerStat::Perk))
            return true;
    return !teamIsAtMaximumCapacity() && !reservePlayers.empty() && gold >= data.levelUp.newRecruitPrice;
}

// PlayerStatUpgrader.TryUpgrade
bool Run::TryUpgrade(int activeIndex, PlayerStat s) {
    if (activeIndex < 0 || activeIndex >= static_cast<int>(activePlayers.size())) return false;
    HumanPlayer* hp = activePlayers[static_cast<size_t>(activeIndex)];
    if (hp->StatIsMaxLevel(s)) return false;
    int price = hp->GetNextUpgradePrice(s);
    if (gold < price) return false;
    hp->LevelUpPlayerStat(s);
    gold -= price;
    return true;
}

// NewRecruitButton.TryRecruit -> HumanTeam.SendReservePlayerToTeam
bool Run::TryRecruit(int reserveIndex) {
    if (teamIsAtMaximumCapacity() || reserveIndex < 0 || reserveIndex >= static_cast<int>(reservePlayers.size()) || reserveIndex > 2)
        return false;
    int price = data.levelUp.newRecruitPrice;
    if (gold < price) return false;
    HumanPlayer* hp = reservePlayers[static_cast<size_t>(reserveIndex)];
    hp->active = true;
    hp->stageStartedIndex = currentStageNumberAsOverallStage() + 1;
    activePlayers.push_back(hp);
    reservePlayers.erase(reservePlayers.begin() + reserveIndex);
    gold -= price;
    return true;
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
