// Headless gameplay simulation: a class-by-class translation of the original C# gameplay
// (GameController, EnemyTurn, FieldManager, Actor/Character/Enemy/Player, Attack*, Ball,
// EnemyActionPlanner, IntentionMarker, HazardManager, UndoManager, StageScore, Gold...).
//
// Coroutines run synchronously: game state changes happen in the original order and every visible
// step is reported as an Event for the presentation layer. Where the original runs coroutines
// concurrently (hits of one attack, the ball rebound) the state changes are split at the yield
// points and replayed in the order Unity resumes them (see Actor::HitBegin / HitEnd).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/gamedata.h"
#include "core/mono_random.h"
#include "core/session.h"

namespace dd::sim {

struct V2 {
    int x = 0, y = 0;
    friend bool operator==(V2 a, V2 b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(V2 a, V2 b) { return !(a == b); }
    friend V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
    friend V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
    friend V2 operator-(V2 a) { return {-a.x, -a.y}; }
    friend V2 operator*(V2 a, int k) { return {a.x * k, a.y * k}; }
    V2& operator+=(V2 b) { x += b.x; y += b.y; return *this; }
    V2& operator-=(V2 b) { x -= b.x; y -= b.y; return *this; }
};
// EggTools.Directions
inline constexpr V2 kUp{0, 1}, kDown{0, -1}, kLeft{-1, 0}, kRight{1, 0}, kNone{0, 0};
inline constexpr V2 kCardinal[4] = {kUp, kDown, kLeft, kRight};
inline constexpr V2 kTowardsGoal = kRight; // DirectionsDeathball
// Vector2Int.Distance (float, as in Unity)
float Distance(V2 a, V2 b);
int RoundToInt(float f); // Mathf.RoundToInt (banker's rounding)

enum class EntityType : int { UNDEFINED, Player, Enemy, Ball, Obstacle, Hazard, Pickup };
enum class Phase : int { Starting, PlayerInput, PlayerActing, EnemiesActing, StageEnded, HazardsActing, SuggestUndo };
enum class HalloweenTrickOrTreat : int {
    StaminaDrained, StaminaDrainedAll, EnemiesHealedSmall, EnemiesHealedFully, StaminaReplenished,
    StaminaReplenishedAll, EnemiesHurtSmall, EnemiesStunned, Count
};
enum class Rank : int { NONE, F, C, B, A, S };

class Game;
class Run;
class Actor;
class Character;
class Enemy;
class Player;
class Ball;
class Treasure;
class Pickup;
class Hazard;
class Attack;
struct FieldTile;
struct Command;

// ---------------------------------------------------------------------------------------------
// Presentation events
// ---------------------------------------------------------------------------------------------
enum class Ev : int {
    TurnStarted,      // value = turn
    PhaseChanged,     // value = Phase
    CurrentPlayer,    // actor = player
    Move,             // actor, from -> to (step, knockback, charge)
    Attack,           // actor, to = direction (or target tile for lightning/spawn-near)
    Hit,              // actor = target, value = damage, extra = attacker uid
    Died,             // actor
    Stunned,          // actor, value = duration (-1 = indefinite)
    StunEnded,        // actor
    Healed,           // actor, value = new health
    Stamina,          // actor = player, value = new stamina
    BallShot,         // actor = shooter, from -> to
    BallPass,         // actor = passer, from -> to
    BallRebound,      // from -> to
    BallDropped,      // to
    BallAcquired,     // actor
    TreasureTaken,    // actor = collector, to = tile
    TrickOrTreat,     // value = HalloweenTrickOrTreat
    Spawned,          // actor = new enemy, to
    SpawnBlocked,     // to
    HazardTriggered,  // to = tile, value = HazardKind
    LightningRow,     // value = column
    Intention,        // actor = enemy (intention markers refreshed)
    Undo,
    StageWon,
    StageLost,
    Log,              // text
};
struct Event {
    Ev type = Ev::Log;
    int actor = -1;
    V2 from, to;
    int value = 0;
    int extra = 0;
    std::string text;
};

// The game's PlayerStat (AP has no upgrade screen entry; PlayerClass is not a stat to level)
enum class PlayerStat : int { Stamina, AP, Strength, Perk, PlayerClass };

// What the stage being played reports to the persistent meta layer (stats and achievements):
// the app's Meta implements it. Only the live stage has one, so replays never count twice.
class MetaSink {
public:
    virtual ~MetaSink() = default;
    virtual void OnEvent(const Game& g, const Event& e) = 0;   // every presentation event, as emitted
    virtual void OnStageEnded(const Game& g) = 0;              // PlayerWon / PlayerLost
    virtual void OnPlayerAction(const Game& g, const Command& c) = 0; // a command starts executing
};

// ---------------------------------------------------------------------------------------------
// Session-level state (HumanPlayer, HumanTeam, Gold, SessionStatTracker, LeagueManager, BallMods)
// ---------------------------------------------------------------------------------------------
class HumanPlayer {
public:
    HumanPlayer(Run* run, const HumanPlayerSeed& seed);

    Run* run;
    HumanPlayerSeed seed;
    int playerNumber;
    std::string playerName;
    const PlayerClassConfig* playerClassConfig;
    const PerkChain* perkChain;
    const PerkConfig* perkConfig = nullptr;
    bool dead = false, active = false;
    int stageStartedIndex = 0, stageDiedIndex = 0;
    int staminaLevel = 0, strengthLevel = 0, apLevel = 0, perkLevel = 0;
    int vampireKills = 0;
    Player* playerReference = nullptr;

    void CachePerkConfig();
    bool perkUnlocked() const { return perkLevel > 0; }
    bool HasPerk(Perk p) const { return perkUnlocked() && perkChain->perk == p; }
    int PerkValue(Perk p) const { return HasPerk(p) ? perkConfig->perkStatValue : 0; } // GetPerkValue(hasXPerk)

    int GetMaxStamina() const;
    int GetMaxAP() const;
    int GetRestStaminaReplenishment() const;
    int GetAttackStrength() const;
    int GetAttackKnockback() const;
    int GetAttackStun() const;
    int GetAttackRange() const;
    int GetBallDamage() const;
    int GetBallKnockback() const;
    int GetBallStun() const;
    int GetPerkStatValue() const { return perkConfig ? perkConfig->perkStatValue : 0; }

    // ---- level up (HumanPlayer / PlayerStatUpgrader) ----
    int GetStatLevel(PlayerStat s) const;
    int GetStatLevelCap(PlayerStat s) const;
    bool StatIsMaxLevel(PlayerStat s) const { return GetStatLevel(s) >= GetStatLevelCap(s); }
    int GetNextUpgradePrice(PlayerStat s) const;
    int GetStatValue(PlayerStat s) const;
    bool AffordableUpgradeAvailable(PlayerStat s) const;
    void LevelUpPlayerStat(PlayerStat s);
};

struct StageResults {
    bool complete = false, touchdown = false;
    int parDifference = 0;
    bool speedTarget = false, gemTarget = false;
    int ballModifier = 0;
    int rank = 0;
    int score = 0;
    bool withinPar() const { return parDifference <= 0; }
};

class Run {
public:
    Run(const GameData& data, Session session, const TeamConfig* team, GameMode mode);

    const GameData& data;
    Session session;
    const TeamConfig* team;
    GameMode mode;
    std::vector<std::unique_ptr<HumanPlayer>> players; // session.players order
    std::vector<HumanPlayer*> activePlayers, deadPlayers, reservePlayers;
    int gold = 0;
    int ballCarrierIndex = 0;        // SessionStatTracker.ballCarrierIndex
    bool swapPositions = false;      // StageStartPanel "Swap Positions" (this stage only)
    int easyBallModsUsed = 0, hardBallModsUsed = 0;
    DifficultyEffect ballEffect = DifficultyEffect::Neutral; // BallMods.currentEffect
    int currentTrial = 0, currentStage = 0;                  // LeagueManager
    StageResults stageResults[12];
    int stagesPlayed = 0;
    bool blockUndoOnNextAction = false; // UndoManager statics survive between stages
    MonoRandom halloweenRng;            // Treasure.halloweenRNG (session seed)

    bool isActionMode() const { return mode == GameMode::Action; }
    bool IsHalloween() const { return session.seasonalEvent == SeasonalEvent::Halloween; }
    bool IsEaster() const { return session.seasonalEvent == SeasonalEvent::Easter; }
    int currentStageNumberAsOverallStage() const { return currentTrial * 3 + currentStage; }
    const Stage& CurrentStage() const { return session.trials[currentTrial].stages[currentStage]; }
    const BallModifierConfig* BallModConfig() const; // BallMods.currentConfig
    const BallModifierConfig* BallModConfigFor(DifficultyEffect e) const; // BallMods.GetConfigForCurrentStage

    void PlayerDied(int playerNumber);     // HumanTeam.PlayerDied
    void ResetStagePlayerReferences();
    void AddGold(int g) { gold += g; }
    int GetStageScores() const;             // Score.GetStageScores
    int GetFinalScoreTotal() const;         // stage scores + survival bonus (final stage only)
    bool AdvanceStage();                    // LeagueManager: next stage; false at the end of the run
    bool currentStageIsFinalStageInFinalTrial() const { return currentTrial >= 3 && currentStage >= 2; }
    bool currentStageIsLastInCurrentTrial() const { return currentStage >= 2; }

    // ---- team management (HumanTeam, StageEndUI, PlayerStatUpgrader, NewRecruitButton) ----
    static constexpr int kMaxTeamSize = 2;
    bool teamIsAtMaximumCapacity() const { return static_cast<int>(activePlayers.size()) >= kMaxTeamSize; }
    bool AffordableUpgradesOrRecruitsAvailable() const;
    bool TryUpgrade(int activeIndex, PlayerStat s); // pays and levels up; false if not allowed
    bool TryRecruit(int reserveIndex);              // pays and sends the reserve to the team
};

// ---------------------------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------------------------
struct FieldTile {
    V2 gridPos;
    Actor* occupant = nullptr;
    Hazard* hazard = nullptr;
    std::vector<Enemy*> desiredBy;
    std::vector<Pickup*> pickups;
    int bloodSplats = 0;
    static constexpr int maxBloodSplats = 5;

    bool isOccupiedByActor() const { return occupant != nullptr; }
    bool isVacant() const { return !isOccupiedByActor(); }
    bool containsPlayer() const;
    bool containsEnemy() const;
    bool containsObstacle() const;
    bool containsHazard() const { return hazard != nullptr; }
    bool containsBall() const;
    bool containsPickups() const { return !pickups.empty(); }
    bool containsGem() const;
    bool isDesiredByEnemy() const { return !desiredBy.empty(); }
    bool isVacantSafeAndUndesired() const { return isVacant() && !isDesiredByEnemy() && !containsHazard(); }
    bool isVacantAndUndesiredButMayBeDangerous() const { return isVacant() && !isDesiredByEnemy(); }
    bool hasBlood() const { return bloodSplats > 0; }
    bool ContainsPlayerInDanger() const; // the danger stripes the game draws (FieldTile.ShowIndicators)
    void AddBloodSplat() { if (bloodSplats != maxBloodSplats) bloodSplats++; }
    void CleanBloodSplat() { if (bloodSplats > 0) bloodSplats--; }
    // UndoFieldTile cache
    int cachedBloodSplats = 0;
};

// ---------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------
class Entity {
public:
    Entity(Game* g, EntityType t);
    virtual ~Entity() = default;
    Game* g;
    EntityType entityType;
    int uid;

    virtual bool isActor() const { return false; }
    virtual bool isCharacter() const { return false; }
    virtual bool isEnemy() const { return false; }
    virtual bool isPlayer() const { return false; }
    virtual bool isBall() const { return false; }
    virtual bool isPickup() const { return false; }
    virtual bool isObstacle() const { return false; }
    virtual bool isHazard() const { return false; }
    virtual bool isRock() const { return false; }
    virtual bool isSpikeTrap() const { return false; }
    virtual bool isLightningHazard() const { return false; }
    virtual bool tileOccupier() const { return false; }
    virtual std::string Name() const { return "?"; }
};

struct AttackData {
    V2 direction;
    Entity* attacker = nullptr;
    int damage = 0, knockback = 0, stun = 0;
    bool wind = false, ballShot = false;
    bool CausesBallDrop(const Actor* target) const;
};

// The part of GA_Actor_Hit that runs after the knockback movement (MoveToPosition's yield).
struct PendingHit {
    Actor* actor = nullptr;
    AttackData data;
    bool moving = false;
    int moveDistance = 0;
    bool done = false;
};

class Actor : public Entity {
public:
    Actor(Game* g, EntityType t, const ActorPrefabFlags& flags);
    V2 gridPos;
    bool isAlive = true;
    int health = 1;
    bool obstructPlayer, obstructEnemy, splatBloodOnTile;
    bool active = true; // gameObject.activeSelf

    bool isActor() const override { return true; }
    virtual int maxHealth() const { return 1; }
    virtual int knockbackResistance() const { return 0; }
    virtual bool isHoldingBall() const { return false; }
    FieldTile* currentFieldTile() const;

    virtual void InitHealth() { health = maxHealth(); }
    void SetGridPosition(V2 p) { gridPos = p; }
    // MoveToPosition, split at its yield.
    void MoveBegin(V2 target);
    void MoveEnd() { OnMoveToPositionFinished(); }
    void MoveToPosition(V2 target) { MoveBegin(target); MoveEnd(); }
    virtual void OnMoveToPositionStarted() {}
    virtual void OnMoveToPositionFinished() {}

    PendingHit HitBegin(const AttackData& data);
    void HitEnd(PendingHit& p);
    void Hit(const AttackData& data) { PendingHit p = HitBegin(data); HitEnd(p); } // GA_Actor_Hit run to completion
    virtual void OnHitStart(const AttackData&) {}
    virtual void OnHealthReduced() {}
    virtual void OnHitEnd() {}
    void ReduceHealth(int dmg);
    virtual void GA_Actor_Die(const AttackData& data);
};

class Stun {
public:
    explicit Stun(Character* c) : character(c) {}
    Character* character;
    bool stunned = false;
    int stunTurnDuration = 0;
    int turnStunned = 0;
    bool stunnedIndefinitely() const { return stunTurnDuration == -1; }
    void StunEnter(int duration);
    void StunExit();
    bool GetStunExpired() const;
};

class SpawnEnemy {
public:
    explicit SpawnEnemy(Game* g) : g(g) {}
    Game* g;
    const EnemyConfig* enemyToSpawn = nullptr;
    int currentLiveChildren = 0;
    bool spawnedAnyChildren = false;
    Enemy* Spawn(V2 pos);
    void SpawnChildDied() { currentLiveChildren--; }
    static bool SpawnBlocked(Game* g, V2 pos);
};

class Character : public Actor {
public:
    Character(Game* g, EntityType t, const ActorPrefabFlags& flags);
    ~Character() override;
    std::unique_ptr<Attack> cAttack;
    std::unique_ptr<Stun> cStun;
    std::unique_ptr<SpawnEnemy> cSpawning;
    bool holdingBall = false;

    bool isCharacter() const override { return true; }
    bool tileOccupier() const override { return true; }
    virtual bool stunned() const { return false; }
    virtual int moveRange() const { return 1; }
    bool isHoldingBall() const override { return holdingBall; }

    void InitialiseAttack(const AttackConfig* config);
    void AddSpawnComponent(const AttackConfig* config);
    void OnHitStart(const AttackData& data) override;
    void GA_Char_Step(V2 direction);
    void OnMoveToPositionFinished() override { CheckTileForBall(); }
    virtual void StepStarted() {}
    virtual void StepComplete(V2) {}
    void CheckTileForBall();
    virtual void AcquiredBall();
    virtual void SetHasBall(bool has);
    bool BallModifierBlocksAttack() const;
    bool BallModifierBlocksMove() const;
    bool BallModifierBlocksPass() const;
    bool BallModifierBlocksShoot() const;
};

struct EnemyAction {
    ActorAction action = ActorAction::Char_Wait;
    V2 direction;
    bool isWait() const { return action == ActorAction::Char_Wait; }
    bool isStep() const { return action == ActorAction::Char_Step; }
    bool isAttack() const { return action == ActorAction::Char_Attack; }
};

class Enemy : public Character {
public:
    Enemy(Game* g);
    const EnemyConfig* config = nullptr;
    SpawnEnemy* spawnParent = nullptr;
    int minimumFieldX = 0, maximumFieldX = 0;
    int actOrder = 0;
    // EnemyIntention
    bool hasIntention = false;
    EnemyAction nextAction;
    // IntentionMarker(Enemy)
    std::vector<V2> targetTiles;
    // EnemyActionPlanner
    std::vector<V2> possibleMovements;
    std::vector<V2> possibleAttacks;
    bool possibleAttacksNull = false;
    bool checkedMovementsThisTurn = false;
    V2 chaseTargetPos;

    std::string Name() const override { return config ? config->actorName : "Enemy"; }
    bool isEnemy() const override { return true; }
    int maxHealth() const override { return config->maximumHealth; }
    int knockbackResistance() const override { return config->knockbackResistance; }
    bool stunned() const override { return cStun->stunned; }
    int moveRange() const override { return config->moveRange; }
    bool spawnOnMove() const;
    bool explodeOnDeath() const;
    EnemyMoveBehaviour moveBehaviour() const { return config->moveBehaviour; }
    bool isGoalieEnemy() const { return config->goalie; }
    bool isRootedMaw() const { return config->enemyType == EnemyType::MawRooted; }

    void InitHealth() override { if (config) Actor::InitHealth(); }
    void Heal(int amount);
    void ManualStart(V2 pos, const EnemyConfig* c, SpawnEnemy* parent);
    void ApplyEnemyConfig(const EnemyConfig* c);
    void GA_Actor_Die(const AttackData& data) override;
    void StepComplete(V2 direction) override;
    void OnMoveToPositionFinished() override;
    void OnHitStart(const AttackData& data) override;
    void OnHitEnd() override;
    bool LiveChildrenAtMaxCapacity() const;

    // EnemyIntention
    void IntentionSet(const EnemyAction& a) { hasIntention = true; nextAction = a; }
    void IntentionClear();
    bool IntentionStillValid() const { return hasIntention && !stunned(); }
    // IntentionMarker / IntentionMarkerEnemy
    bool MarkerShow(ActorAction action, V2 direction, bool fullDetails);
    void ShowEnemyIntention(bool fullDetails);
    void MarkerClear(bool alsoClearDesiredTiles);
    void RefreshIntentionMarker();
    void TargetTilesInDirection(V2 direction, int distance, bool stopAtFirstOccupiedTile);
    bool StepWillResultInAttack() const;
    // EnemyActionPlanner
    void DecideNextAction();
    EnemyAction GetNextAction();
    void CachePossibleMovements();
    bool LongMoveIsPossible(V2 direction) const;
    EnemyAction GetBestPossibleAttack();
    EnemyAction GetBestPossibleMovement(bool tryingToEscapeHazard);
    V2 GetChaseTargetPosition() const;
};

class Player : public Character {
public:
    Player(Game* g);
    HumanPlayer* humanPlayer = nullptr;
    int stamina = 0;
    int actionPoints = 0;
    int tilesMovedThisTurn = 0;
    std::vector<Enemy*> adjacentMonsters;
    std::vector<Player*> adjacentPlayers;
    V2 positionOnMoveStarted;
    static constexpr int lowStaminaThreshold = 4;
    // UndoPlayer cache
    struct { int stamina = 0, actionPoints = 0; V2 gridPos; bool isHoldingBall = false; int tilesMovedThisTurn = 0; } undo;

    std::string Name() const override { return humanPlayer ? humanPlayer->playerName : "Player"; }
    bool isPlayer() const override { return true; }
    int maxStamina() const { return humanPlayer->GetMaxStamina(); }
    int maxAP() const { return humanPlayer->GetMaxAP(); }
    int moveRange() const override { return 1 + humanPlayer->PerkValue(Perk::RemovedMoveRange); }
    bool staminaIsLow() const { return stamina <= lowStaminaThreshold; }

    void ManualStart(bool hasBall, HumanPlayer* hp);
    void SpendStamina(int cost);
    void DrainStamina() { SpendStamina(stamina); }
    void ReplenishStamina(int value);
    void SpendActionPoints(int points);
    void ReplenishActionPoints() { actionPoints = maxAP(); }
    void StepStarted() override;
    void GA_Actor_Die(const AttackData& data) override;
    void GA_Char_Wait();
    void StartAttack();
    void GA_Player_Shoot();
    void GA_Player_Pass();
    void GA_Player_Take();
    void OnMoveToPositionStarted() override { positionOnMoveStarted = gridPos; }
    void OnMoveToPositionFinished() override;
    void AcquiredBall() override;
    bool SwitchToPlayerIsAllowed() const;
    void CheckAdjacentActors();
    void NewTurn();
    void RestoreUndo();
    void CacheUndo();
};

class Obstacle : public Actor { // BreakableWall
public:
    Obstacle(Game* g, const ObstacleConfig* c);
    const ObstacleConfig* config;
    std::string Name() const override { return config->actorName; }
    bool isObstacle() const override { return true; }
    bool isRock() const override { return true; }
    bool tileOccupier() const override { return true; }
    int maxHealth() const override { return config->maximumHealth; }
    int knockbackResistance() const override { return config->knockbackResistance; }
};

class Pickup : public Actor {
public:
    Pickup(Game* g, EntityType t, const ActorPrefabFlags& flags) : Actor(g, t, flags) {}
    bool acquired = false;
    bool isPickup() const override { return true; }
    virtual bool isGem() const { return false; }
    virtual void CollectPickup(Character* c);
    virtual void SetPickupActive(bool a) { active = a; }
};

class Treasure : public Pickup {
public:
    Treasure(Game* g, const TreasureConfig* c);
    const TreasureConfig* config;
    HalloweenTrickOrTreat halloweenEvent = HalloweenTrickOrTreat::StaminaDrained;
    bool halloweenIsTrick = false;
    bool hiddenForEaster = false;
    int easterEggCountdown = 3;
    bool proximityCheckEnabled = false;
    std::unique_ptr<SpawnEnemy> spawnEnemy;
    // UndoTreasure cache
    bool cachedAcquired = false, cachedHiddenForEaster = false;

    std::string Name() const override { return config->actorName; }
    bool isGem() const override { return config->treasureType == TreasureType::Gem; }
    void InitialiseTreasure();
    void CollectPickup(Character* c) override;
    int GetScoreValue() const;
    void HideForEaster();
    void RevealForEaster();
    void OnNewTurn();
    bool CheckProximity(V2 pos);
};

class Ball : public Pickup {
public:
    Ball(Game* g);
    Character* carrier = nullptr;
    EntityType teamCarrying = EntityType::Player;
    Player* shooterPlayer = nullptr;
    bool ballWasRecoveredFromEnemies = false;
    bool shotToFinishLine = false;
    // UndoBall cache
    V2 cachedGridPos;
    Character* cachedCarrier = nullptr;

    std::string Name() const override { return "Ball"; }
    bool isBall() const override { return true; }
    bool isHeldBySomeone() const { return carrier != nullptr; }
    bool isHeldByPlayer() const;
    void CollectPickup(Character* c) override;
    void GA_Ball_Shoot(Player* p);
    void GA_Ball_Pass(Player* p);
    void Moving(V2 start, V2 end, bool isPass);
    void BallAcquired(Character* c);
    void BallDropped(V2 pos);
    V2 GetBallPosition() const { return carrier ? carrier->gridPos : gridPos; }
    int GetDamageTotal(Player* p = nullptr) const;
    int GetKnockbackTotal(Player* p = nullptr) const;
    int GetReboundTotal(Player* p = nullptr) const;
    int GetStunDuration(Player* p = nullptr) const;
    int GetStaminaPenalty() const;
    bool ShotTargetShouldCatchBall(Actor* a) const;
};

class Hazard : public Entity {
public:
    Hazard(Game* g, const HazardConfig* c, V2 pos);
    const HazardConfig* config;
    V2 gridPos;
    std::string Name() const override { return config->actorName; }
    bool isHazard() const override { return true; }
    bool isSpikeTrap() const override { return config->kind == HazardKind::FloorSpikes; }
    bool isLightningHazard() const override { return config->kind == HazardKind::Lightning; }
    void PerformAction();
};

// ---------------------------------------------------------------------------------------------
// Attacks
// ---------------------------------------------------------------------------------------------
class Attack {
public:
    Attack(Character* owner, const AttackConfig* config) : character(owner), config(config) {}
    virtual ~Attack() = default;
    Character* character;
    const AttackConfig* config;
    V2 cachedDirection;
    std::vector<Actor*> cachedRecipients;

    AttackBehaviour attackBehaviour() const { return config->attackBehaviour; }
    virtual int attackDamage() const { return config->attackDamage; }
    virtual int knockbackDistance() const { return config->knockbackDistance; }
    virtual int stunValue() const { return config->stunDuration; }
    virtual int range() const { return config->range; }
    bool pierce() const { return config->pierce; }
    bool wind() const { return config->wind; }
    int additionalStaminaCost() const { return config->additionalStaminaCost; }

    void Attack_Coroutine(V2 direction);
    virtual void OnAttackStart() {}
    virtual void CacheAttackRecipients() {}
    virtual void OnAttackExecuted() {}
    // Returns false for the C# "return null".
    virtual bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out);
    AttackData CreateAttackData(V2 direction) const;
};

class AttackAdjacentTile : public Attack {
public:
    using Attack::Attack;
    void CacheAttackRecipients() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
class AttackCharge : public Attack {
public:
    using Attack::Attack;
    V2 cachedEndPos;
    Actor* cachedEndPosActor = nullptr;
    void OnAttackStart() override;
    void CacheAttackRecipients() override;
    void OnAttackExecuted() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
class AttackExplode : public Attack {
public:
    using Attack::Attack;
    void CacheAttackRecipients() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
class AttackSpawnAdjacent : public Attack {
public:
    using Attack::Attack;
    void OnAttackExecuted() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
class AttackSpawnNearPlayer : public Attack {
public:
    using Attack::Attack;
    void OnAttackExecuted() override;
    void CacheAttackRecipients() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
class AttackLightningBolt : public Attack {
public:
    using Attack::Attack;
    int nextPlayerTarget = 0;
    void CacheAttackRecipients() override;
    bool GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) override;
};
// PlayerAttackAdjacent / PlayerAttackCharge: stats come from the HumanPlayer.
template <typename Base>
class PlayerAttack : public Base {
public:
    PlayerAttack(Player* p, const AttackConfig* c) : Base(p, c), player(p) {}
    Player* player;
    int attackDamage() const override { return this->config->attackDamage + player->humanPlayer->GetAttackStrength(); }
    int knockbackDistance() const override { return this->config->knockbackDistance + player->humanPlayer->GetAttackKnockback(); }
    int stunValue() const override { return this->config->stunDuration + player->humanPlayer->GetAttackStun(); }
    int range() const override { return this->config->range + player->humanPlayer->GetAttackRange(); }
    void OnAttackStart() override { player->StartAttack(); Base::OnAttackStart(); }
};

// ---------------------------------------------------------------------------------------------
// Player commands (Command / CommandObject / SetTileCommands / CommandObjectButton)
// ---------------------------------------------------------------------------------------------
struct Command {
    const PlayerActionConfig* actionConfig = nullptr;
    Player* player = nullptr;
    V2 direction;
    int distance = 0;
    int staminaCost = 0;
    int staminaReplenish = 0;
    int actionPointCost = 1;
    bool onTile = false;  // field tile command (else UI button)
    V2 tile;              // tile holding the command
    bool interactable = true;
    bool tooExpensive = false; // CommandObject.commandTooExpensive
};

// ---------------------------------------------------------------------------------------------
// One stage (the Gameplay scene: GameController and the managers)
// ---------------------------------------------------------------------------------------------
class Game {
public:
    Game(Run& run);
    ~Game();

    Run& run;
    const GameData& data;
    std::vector<Event> events;
    bool fastMode = false;      // AppSettingsDeathball.pref_fastMode (cosmetic only)
    bool suggestUndo = true;    // AppSettingsDeathball.pref_suggestUndo
    MetaSink* meta = nullptr;   // stats and achievements of the stage being played (never replays)

    // ---- lifecycle (GameController) ----
    void Start();                                 // Start() + the stage start sequence
    Phase phase() const { return awaitingUndo_ ? Phase::SuggestUndo : phase_; }
    const Command& playerCommand() const { return command_; } // PlayerCommand.command
    int StaminaCost(const PlayerActionConfig* c, const Player* p, int distance = 0) const;
    int Turn() const { return turn_; }
    int TurnPar() const { return turnPar_; }
    int ParBonusRankTarget() const; // StageScore.GetParBonusRankTarget (turns under par, negative)
    int ParDifference() const { return turn_ - turnPar_; }
    bool anyPlayerPassedFinishLine() const { return playerOnFinishLine_ != nullptr; }
    bool gameEndedInDefeat = false;
    const StageResults& results() const { return results_; }
    int goldEarned() const { return goldEarned_; }

    // ---- input ----
    std::vector<Command> GetCommands();          // tile commands + buttons for the current player
    bool Select(const Command& c);               // CommandObject.Select
    void RespondToUndoSuggestion(bool acceptUndo);
    bool UndoValid() const { return undoValid_; }

    // ---- field (FieldManager) ----
    int fieldX = 0, fieldY = 0;
    std::vector<FieldTile> grid;
    bool PositionIsWithinField(V2 p) const { return p.x >= 0 && p.x <= fieldX - 1 && p.y >= 0 && p.y <= fieldY - 1; }
    FieldTile* GetTile(V2 p);
    bool PositionIsPastFinishLine(int x) const { return x >= fieldX - 1; }
    void SetTileOccupant(Actor* a, V2 p) { GetTile(p)->occupant = a; }
    void ClearTileOccupant(Actor* a, V2 p, bool ignoreIfNewOccupantExists);
    void MoveTileOccupant(Actor* a, V2 from, V2 to);
    void AddPickupToTile(Pickup* p, V2 pos);
    void RemovePickupFromTile(Pickup* p, V2 pos);
    void SetTileDesired(V2 p, Enemy* e);
    void SetTileUndesiredByEnemy(V2 p, Enemy* e);
    void SetAllTilesToUndesired();
    void ForceRefreshAllOccupants();
    FieldTile* GetObstacleFromStraightLine(V2 start, V2 dir, int maxDistance, bool returnPreviousTileIfOccupied);
    std::vector<V2> GetMoveAndAttackTiles(V2 start, V2 dir, int maxDistance);
    std::vector<V2> GetChargeTiles(V2 start, V2 dir, int maxDistance);
    std::vector<V2> GetPierceAttackTiles(V2 start, V2 dir, int maxDistance);
    bool StraightLineToOpponentIsClear(V2 start, V2 dir, int maxDistance, bool byPlayers, bool byEnemies, bool byDesire);
    bool PlayerIsWithinMoveRange(const Character* c, V2 dir);

    // ---- actors ----
    std::vector<std::unique_ptr<Entity>> entities; // ownership (instantiation order)
    std::vector<Enemy*> enemies;                    // EnemyManager.enemies
    std::vector<Player*> players;                   // PlayerManager.players
    std::vector<Player*> playersWaitingToAct;
    Player* currentPlayer = nullptr;
    std::vector<Hazard*> hazards;                   // HazardManager.hazards
    std::vector<Treasure*> treasures;               // instantiation order (event subscription order)
    Ball* ball = nullptr;
    int deathsThisStage = 0;
    int lightningHazardRowsSpawned = 0;
    MonoRandom enemyRng;                            // EnemyActionPlanner.rng
    std::vector<Treasure*> stageTreasures;          // Gold.stageTreasures
    int gems = 0;                                   // Gold.gems
    int nextUid = 1;

    Entity* FindEntity(int uid) const;
    Enemy* SpawnEnemyAt(V2 pos, const EnemyConfig* c, SpawnEnemy* parent); // EnemySpawner.SpawnEnemy
    void SpawnWall(V2 pos);
    void SpawnHazard(V2 pos, const HazardConfig* c);
    void SpawnTreasure(V2 pos, const TreasureConfig* c);

    // ---- managers ----
    void EnemyDied(Enemy* e);
    void PlayerDied(Player* p);
    void DecideNextActions();
    void RefreshIntentionMarkers();
    void ClearAndHideAllIntentions();
    void RefreshAllActOrders();
    void StunAllEnemies();
    void DamageAllEnemies(int damage);
    void HealAllEnemiesFully();
    void HealAllEnemies(int amount);
    Player* GetNextPlayerInFullList();
    Player* GetNextPlayerInWaitingList();
    void SwitchControl(Player* next);
    void SwitchControlToNextPlayer() { SwitchControl(GetNextPlayerInWaitingList()); }
    int GetNumberOfLivePlayers() const;
    void CheckAllPlayersAdjacentActors();
    bool AnyPlayersInDanger();
    void BroadcastPlayerMoved(V2 pos);
    void BlockUndoOnNextAction() { run.blockUndoOnNextAction = true; }
    void PlayerPassedFinishLine(Player* p) { playerOnFinishLine_ = p; }
    bool BallReachedOtherSide() const { return PositionIsPastFinishLine(ball->GetBallPosition().x); }
    const BallModifierConfig* modifierConfig() const { return modifier_; }
    Player* PlayerById(int uid) const;

    void Emit(Ev type, const Entity* actor = nullptr, V2 from = {}, V2 to = {}, int value = 0, int extra = 0);
    void Log(const std::string& text);

private:
    Phase phase_ = Phase::Starting;
    int turn_ = 0, turnPar_ = 0;
    Player* playerOnFinishLine_ = nullptr;
    bool undoValid_ = false;
    Command command_; // PlayerCommand.command
    StageResults results_;
    int goldEarned_ = 0;
    const BallModifierConfig* modifier_ = nullptr;
    bool awaitingUndo_ = false;          // GameController.SuggestUndo is waiting for an answer
    std::vector<Player*> allPlayers_;    // every Player spawned this stage (UndoPlayer subscribers)

    void ChangePhase(Phase p);
    void NextTurn();
    void StartPlayerInput();
    void ExecutePlayerAction();
    void GoToNextPhaseAfterPlayer();
    void FinishedPlayerTurn();
    void HazardsTurn();
    void TryLightningHazard();
    void EnemyTurn();
    void FinishedEnemyTurn();
    void PlayerWon();
    void PlayerLost();
    void StartNewTurnPlayers();   // PlayerManager.StartNewTurn
    void PlayerFinishedActing();  // PlayerManager.PlayerFinishedActing
    void SpawnPlayers();
    void PlayerActed(const PlayerActionConfig* c); // UndoManager.PlayerActed
    void CacheValues();                            // UndoManager.CacheValues
    void TriggerUndo();
    void CalculateStageScore();
    Command MakeCommand(const PlayerActionConfig* c, Player* p, V2 dir, int distance, bool onTile, V2 tile);
};

const char* PhaseName(Phase p);
std::string DescribeEvent(const Game& g, const Event& e);

} // namespace dd::sim
