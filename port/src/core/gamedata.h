// Typed game configuration loaded from gamedata.json (produced by tools/export_gamedata.py).
// Mirrors the ScriptableObjects / scene singletons of the original; only fields the port uses.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dd {

// Enum values match the original C# declarations exactly (RNG results are cast to them).
enum class PlayerClass : int { Carrier = 0, Runner = 1, Brawler = 2, Count = 3 };
enum class GameMode : int { Normal = 0, DailyChallenge = 1, Boss = 2, Action = 3, NOT_SET = 4 };
enum class FieldSize : int { Small = 0, Medium = 1, Large = 2 };
enum class SkinTone : int { Light = 0, Mid = 1, Dark = 2 };
enum class SeasonalEvent : int { None = 0, Halloween = 1, Christmas = 2, Easter = 3 };

const char* ToString(PlayerClass c);
const char* ToString(GameMode m);
const char* ToString(FieldSize s);
const char* ToString(SeasonalEvent e);

struct BuildVersion {
    int major = 0, minor = 0, patch = 0;
    bool IsOlderThan(const BuildVersion& v) const;
};

// Gameplay enums (same declaration order as the C# originals).
enum class AttackBehaviour : int { AdjacentTile, Charge, SpawnAdjacent, SpawnNearFurthestPlayer, LightningBolt, None };
enum class EnemyMoveBehaviour : int { Random, ChaseBall, BlockBall, Static, ChasePlayers, AvoidPlayers };
enum class EnemyType : int {
    UNDEFINED, MadLad, SlamLad, Brutolith, Sascatch, Redhorn, MawPotted, MawRooted, Kappa, Judgment, Snaptor, Tick,
    Hatchery, LilLil
};
enum class ActorAction : int {
    NULL_ACTION, Actor_Die, Actor_Hit, Char_Attack, Char_Step, Char_ChargeImpact, Char_Wait, Enemy_StunEnter,
    Enemy_StunCountdown, Enemy_StunExit, Player_Shoot, Player_Pass, Player_AcquireBall, Ball_MoveForwards,
    Ball_MoveToTile, Ball_Stop, Enemy_HurtSelf, Explosion, Player_TakeBall
};
enum class Perk : int {
    NONE, Stun, DamageBonus, RemovedHealthBonus, StaminaReduction, RemovedRestMidTurn, Knockback, BallDamage, RestBoost,
    StaminaAutoRecovery, PierceAttack, ChargeAttack, RemovedMoveRange, BallKnockback, BallSnatcher, BallRebound,
    SpikeImmunity, BloodBoostsDamage, KillsBoostDamage, BloodRestoresStamina, RemovedDamageBoostAdjacent,
    RemovedDamageBoostLowStamina, DamageBoostSurrounded, DamageBoostWithBall, RemovedMoreGoldFromGems,
    RemovedRockSmasher, StaminaBoostSurrounded, TransferStaminaOnAdjacent, TransferStaminaOnRest,
    StaminaFromBallAcquire, PassGivesStamina
};
enum class BallModifier : int { NONE, Damage, Knockback, Rebound, StaminaPenalty, Stun, NoAttack, NoMove, NoPass, NoShoot };
enum class DifficultyEffect : int { Neutral, Harder, Easier };
enum class TeamTrait : int {
    NONE, BaseAP, BaseKnockback, BasePerkLevel, BaseStamina, BaseStrength, BaseStun, BlockAttack, BlockBallModifiers,
    BlockPerksDEPRECATED, MultiplyGold, MultiplyScore, MultiplyScoreBonusPar, MultiplyScoreBonusTouchdown,
    MultiplyScoreBonusTreasure, PerkMaxLevelReduced, RarePerk, StaminaAutoRestore, StaminaCosts, COUNT
};
enum class TreasureType : int { CoinPile, Gem, GoldSkull };
enum class HazardKind : int { FloorSpikes, Lightning }; // which prefab HazardConfig.prefab points at

const char* ToString(ActorAction a);

struct EnemyConfig;

struct AttackConfig {
    std::string id, name;
    AttackBehaviour attackBehaviour = AttackBehaviour::AdjacentTile;
    int attackDamage = 0;
    bool rangeEnabled = false;
    int range = 0; // Optional<int>.Value (read regardless of Enabled, like the original)
    int stunDuration = 0;
    int knockbackDistance = 1;
    bool explodeOnDeath = false, spawnOnMove = false, pierce = false, wind = false;
    const EnemyConfig* enemyToSpawn = nullptr;
    int maxLiveChildren = 0;
    int additionalStaminaCost = 0;
    bool spawner() const {
        return attackBehaviour == AttackBehaviour::SpawnAdjacent || attackBehaviour == AttackBehaviour::SpawnNearFurthestPlayer;
    }
};

struct ActorConfig {
    std::string id, name, actorName;
    int budget = 5, minimumPerPool = 1, maximumPerPool = 4;
};
struct EnemyConfig : ActorConfig {
    int maximumHealth = 1, knockbackResistance = 0;
    EnemyType enemyType = EnemyType::UNDEFINED;
    std::string sprite;
    bool boss = false, goalie = false, ballCatcher = false;
    int healthLossPerTurn = 0;
    EnemyMoveBehaviour moveBehaviour = EnemyMoveBehaviour::Random;
    int moveRange = 1;
    bool stayBehindGoal = false;
    const AttackConfig* attackConfig = nullptr;
};
struct ObstacleConfig : ActorConfig {
    int maximumHealth = 1, knockbackResistance = 0;
};
struct HazardConfig : ActorConfig {
    int damage = 0;
    HazardKind kind = HazardKind::FloorSpikes;
};
struct TreasureConfig : ActorConfig {
    TreasureType treasureType = TreasureType::Gem;
    int scoreValue = 0;
    std::string sprite;
    SeasonalEvent seasonalVariant = SeasonalEvent::None;
};

struct PerkConfig {
    std::string id, name;
    int perkAdditionalCost = 0, perkStatValue = 0;
};

struct PerkChain {
    std::string id, name, chainName;
    Perk perk = Perk::NONE;
    std::vector<const PerkConfig*> perks;
    bool allowCarriers = false, allowRunners = false, allowBrawlers = false;
    BuildVersion minimumVersion;
};

struct PlayerClassConfig {
    std::string id, name, className;
    PlayerClass playerClass = PlayerClass::Carrier;
    int maxLevelStamina = 0, maxLevelStrength = 0, maxLevelActionPoints = 0;
};

struct PlayerActionConfig {
    std::string id, name;
    ActorAction action = ActorAction::NULL_ACTION;
    int actionPoints = 1;
    bool noStaminaCost = false;
    int staminaCost = 1;
    bool switchControl = false, undo = false;
    bool lockWithoutBall = false, lockWithoutAnotherPlayer = false, lockIfBallOutOfReach = false, lockIfUndoInvalid = false;
};

struct BallModifierConfig {
    std::string id, name, friendlyName, description, ballSprite;
    BallModifier modifier = BallModifier::NONE;
    DifficultyEffect difficultyEffect = DifficultyEffect::Neutral;
    int value = 0;
};

struct TeamTraitConfig {
    TeamTrait trait = TeamTrait::NONE;
    int valueInt = 0;
    float valueFloat = 0.0f;
    std::string description;
};

struct LevelUpConfig {
    struct Upgrade { int statBuff = 0, price = 0; };
    int perkCostMultiplier = 1, newRecruitPrice = 0;
    int staminaBase = 0, actionPointBase = 0, restBase = 0, strengthBase = 0, knockbackBase = 0, stunBase = 0;
    int ballDamageBase = 0, ballKnockbackBase = 0, ballStunBase = 0;
    std::vector<Upgrade> staminaUpgrades, strengthUpgrades, actionPointUpgrades;
};

struct ScoringConfig {
    int pointsStageComplete = 0, pointsTouchdown = 0, pointsWithinPar = 0, pointsPerPar = 0;
    int finalSurvivalBonusPerStage = 0;
    int parBonusRankTargetSmall = 0, parBonusRankTargetMedium = 0, parBonusRankTargetLarge = 0;
    float goldScoreBonusMultiplier = 0.0f;
    int goldBonusPerStage = 0;
    float goldDifficultyBonusMultiplier = 0.0f;
};

// Serialized fields of the single-instance gameplay prefabs.
struct ActorPrefabFlags {
    bool obstructPlayer = true, obstructEnemy = true, splatBloodOnTile = false;
};
struct Prefabs {
    ActorPrefabFlags player, enemy, wall, ball, treasure;
    const AttackConfig* playerDefaultAttack = nullptr;
    const AttackConfig* playerPierceAttack = nullptr;
    const AttackConfig* playerChargeAttack = nullptr;
    const PlayerActionConfig* configStep = nullptr;
    const PlayerActionConfig* configAttack = nullptr;
    const PlayerActionConfig* configWait = nullptr;
    const PlayerActionConfig* configThrow = nullptr;
    const PlayerActionConfig* configPass = nullptr;
    const PlayerActionConfig* configSwitchControl = nullptr;
    const PlayerActionConfig* configTake = nullptr;
    const PlayerActionConfig* configUndo = nullptr;
    const AttackConfig* ballAttackConfig = nullptr;
    const AttackConfig* lightningAttackConfig = nullptr;
    const EnemyConfig* easterEnemyToSpawn = nullptr;
};

struct HumanPlayerTexture {
    std::string texture;
    int instancesInArray = 1;
    bool useWithLightSkin = true, useWithDarkSkin = true, useWithMidSkin = true;
};

struct HumanPlayerConfig {
    std::string id, name;
    std::vector<std::string> playerNames;
    std::vector<HumanPlayerTexture> hairTextures;
    // HumanPlayerConfig.CreateHairsArrayForSkinTone: texture list with per-texture repetitions.
    std::vector<std::string> HairsForSkinTone(SkinTone tone) const;
};

struct HumanPlayerGeneralConfig {
    std::string id, name;
    std::vector<std::string> bodyTextures;
    std::vector<std::string> bios;
};

struct TrialData {
    std::string trialName;
    const EnemyConfig* spareEnemy = nullptr;
    std::vector<const EnemyConfig*> specialEnemies;
    const HazardConfig* hazard = nullptr;
    const EnemyConfig* goalieOverride = nullptr; // null when Optional<> disabled
    bool HasSpecialEnemies() const { return !specialEnemies.empty(); }
    bool HasHazards() const { return hazard != nullptr; }
};

struct FieldSizeData {
    FieldSize fieldSize = FieldSize::Small;
    int gridX = 0, gridY = 0;
    std::string backgroundScene;
    int par = 0;
    int maxObstacles = 0, minWalls = 0, maxWalls = 0, minHazards = 0, maxHazards = 0;
    int minWanderers = 0, maxWanderers = 0, minAggressives = 0, maxAggressives = 0, goalies = 0;
    int gems = 0, gemsDuringEasterEvent = 0;
    int specialEnemyTypesMin = 0, specialEnemyTypesMax = 0, enemyBudget = 0;
    int GetGemCount(SeasonalEvent e) const { return e == SeasonalEvent::Easter ? gemsDuringEasterEvent : gems; }
};

struct SessionConfig {
    int enemySpawnMinX = 0, wallSpawnMinX = 0, hazardSpawnMinX = 0, treasureSpawnMinX = 0;
    int enemySpawnMaxX = 0, wallSpawnMaxX = 0, hazardSpawnMaxX = 0, treasureSpawnMaxX = 0, bossSpawnMaxX = 0;
    int parIncreasePerTrial = 0, enemyBudgetIncreasePerTrial = 0;
    std::vector<TrialData> trials;
    std::vector<FieldSizeData> fieldSizeData;
    const EnemyConfig* midGameBoss = nullptr;
    const EnemyConfig* endGameBoss = nullptr;
    const ObstacleConfig* wallConfig = nullptr;
    const TreasureConfig* gemConfig = nullptr;
    const HumanPlayerConfig* playerMaleConfig = nullptr;
    const HumanPlayerConfig* playerFemaleConfig = nullptr;
    const HumanPlayerGeneralConfig* playerGeneralConfig = nullptr;
    std::vector<const PerkChain*> perkChains;
    const HazardConfig* lightningHazardConfig = nullptr;
    const BallModifierConfig* ballModifierDefault = nullptr;
    std::vector<const BallModifierConfig*> ballModsEasier, ballModsHarder;
    float ballMultiplierPerStageEasy = 0.0f, ballMultiplierPerStageHard = 0.0f;
};

struct GameModeConfig {
    std::string id, name, friendlyName;
    GameMode mode = GameMode::Normal;
    float enemyBudgetMultiplier = 1.0f, turnParMultiplier = 1.0f;
    int stagesPerTrial = 3;
    const EnemyConfig* basicWanderer = nullptr;
    const EnemyConfig* basicAggressive = nullptr;
    const EnemyConfig* basicGoalie = nullptr;
    bool allowRestartStage = true, allowSaveAndQuit = true, allowTeamSelection = true;
    int unlockCriteriaAchievementCount = 0;
    std::vector<int> unlockCriteriaAchievements; // Achievement ids
    std::string unlockPopupText;
    bool isDailyChallengeMode() const { return mode == GameMode::DailyChallenge; }
};

struct TeamConfig {
    struct PlayerSetup {
        const PlayerClassConfig* playerClassConfig = nullptr; // null = random
        const PerkChain* playerPerk = nullptr;                // null = random
    };
    std::string id, name, teamName, description;
    int unlockCost = 4;
    int requiredAchievement = 0; // Achievement id (0 = none)
    int keysAwardedPerTrial = 1;
    std::vector<PlayerSetup> players;
    std::vector<const PerkChain*> blockedPerks;
    const TeamTraitConfig* positiveTrait = nullptr;
    const TeamTraitConfig* negativeTrait = nullptr;
    const PlayerSetup& GetPlayerSetupAtIndex(int index) const { return players[index % players.size()]; }
    bool PerkIsBlocked(const PerkChain* perk) const;

    // TeamConfig trait accessors (GetTotalTraitValueInt / GetTraitMultiplierValue).
    int TraitInt(TeamTrait t) const;
    float TraitMultiplier(TeamTrait t) const;
    int traitAP() const { return TraitInt(TeamTrait::BaseAP); }
    int traitKnockback() const { return TraitInt(TeamTrait::BaseKnockback); }
    int traitStamina() const { return TraitInt(TeamTrait::BaseStamina); }
    int traitStrength() const { return TraitInt(TeamTrait::BaseStrength); }
    int traitStun() const { return TraitInt(TeamTrait::BaseStun); }
    int traitStaminaCosts() const { return TraitInt(TeamTrait::StaminaCosts); }
    int traitStaminaAutoRestore() const { return TraitInt(TeamTrait::StaminaAutoRestore); }
    float traitMultiplyGold() const { return TraitMultiplier(TeamTrait::MultiplyGold); }
    float traitMultiplyScore() const { return TraitMultiplier(TeamTrait::MultiplyScore); }
    float traitMultiplyScoreBonusPar() const { return TraitMultiplier(TeamTrait::MultiplyScoreBonusPar); }
    float traitMultiplyScoreBonusTouchdown() const { return TraitMultiplier(TeamTrait::MultiplyScoreBonusTouchdown); }
    float traitMultiplyScoreBonusTreasure() const { return TraitMultiplier(TeamTrait::MultiplyScoreBonusTreasure); }
    bool traitBlockAttack() const { return negativeTrait && negativeTrait->trait == TeamTrait::BlockAttack; }
    bool traitBlockBallMods() const { return negativeTrait && negativeTrait->trait == TeamTrait::BlockBallModifiers; }
};

struct SeasonalEventConfig {
    std::string id, name;
    SeasonalEvent seasonalEvent = SeasonalEvent::None;
    const TreasureConfig* treasureConfig = nullptr;
    bool allowSeason = true, preventHighScoreSaving = false;
    std::string pluralLabel = "Gems";
    bool titleScreenJingle = false, titleLogo = false;
    std::string titleLogoSprite;
};

class GameData {
public:
    bool Load(const std::string& path, std::string* error);

    SessionConfig session;
    std::vector<const TeamConfig*> teams;            // Teams.teamConfigs (index = team id)
    const PlayerClassConfig* classCarrier = nullptr; // PlayerClasses singleton
    const PlayerClassConfig* classRunner = nullptr;
    const PlayerClassConfig* classBrawler = nullptr;
    const GameModeConfig* modeNormal = nullptr;      // AppDeathball singleton
    const GameModeConfig* modeDaily = nullptr;
    const GameModeConfig* modeBoss = nullptr;
    const GameModeConfig* modeAction = nullptr;
    const SeasonalEventConfig* seasonal[4] = {};     // Configs singleton, indexed by SeasonalEvent
    BuildVersion buildVersion;
    int platform = 1;                                // BuildConfig.Platform (1 = PCSteam)
    LevelUpConfig levelUp;
    ScoringConfig scoring;
    Prefabs prefabs;
    const PerkConfig* noPerkConfig = nullptr;        // PerkManager.noPerkConfig

    const PlayerClassConfig* GetPlayerClassConfig(PlayerClass c) const;
    const GameModeConfig* GetGameModeConfig(GameMode m) const;
    const SeasonalEventConfig* SeasonalConfig(SeasonalEvent e) const { return seasonal[static_cast<int>(e)]; }
    // PerkManager.GetPerkFromPerkChain
    const PerkConfig* GetPerkFromPerkChain(const PerkChain* chain, int level) const;

private:
    template <typename T> using Store = std::map<std::string, std::unique_ptr<T>>;
    Store<AttackConfig> attacks_;
    Store<PerkConfig> perkConfigs_;
    Store<PlayerActionConfig> actionConfigs_;
    Store<BallModifierConfig> ballMods_;
    Store<TeamTraitConfig> traits_;
    Store<EnemyConfig> enemies_;
    Store<ObstacleConfig> obstacles_;
    Store<HazardConfig> hazards_;
    Store<TreasureConfig> treasures_;
    Store<PerkChain> perkChains_;
    Store<PlayerClassConfig> playerClasses_;
    Store<HumanPlayerConfig> humanPlayers_;
    Store<HumanPlayerGeneralConfig> humanGenerals_;
    Store<GameModeConfig> gameModes_;
    Store<TeamConfig> teams_;
    Store<SeasonalEventConfig> seasonalConfigs_;
};

} // namespace dd
