// Entity / Actor / Character / Enemy / Player / BreakableWall / Pickup / Treasure / Ball / Hazard / Stun /
// SpawnEnemy. Enemy AI and intention markers live in ai.cpp, attacks in attack.cpp.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "sim/sim.h"

namespace dd::sim {

Entity::Entity(Game* game, EntityType t) : g(game), entityType(t), uid(game->nextUid++) {}

bool AttackData::CausesBallDrop(const Actor* target) const {
    if (!target->isHoldingBall()) return false;
    if (wind) return false;
    if (attacker == nullptr) return false;
    if (attacker->isSpikeTrap()) return true;
    if (attacker->isLightningHazard()) return true;
    if (direction == kNone) return false;
    if (ballShot && attacker->isPlayer() && target->isPlayer()) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------------------------
Actor::Actor(Game* game, EntityType t, const ActorPrefabFlags& f)
    : Entity(game, t), obstructPlayer(f.obstructPlayer), obstructEnemy(f.obstructEnemy), splatBloodOnTile(f.splatBloodOnTile) {}

FieldTile* Actor::currentFieldTile() const { return g->GetTile(gridPos); }

void Actor::MoveBegin(V2 target) {
    OnMoveToPositionStarted();
    V2 from = gridPos;
    g->MoveTileOccupant(this, gridPos, target);
    gridPos = target;
    g->Emit(Ev::Move, this, from, target);
}

// GA_Actor_Hit up to the knockback movement. Without knockback the whole hit completes here.
PendingHit Actor::HitBegin(const AttackData& data) {
    PendingHit p;
    p.actor = this;
    p.data = data;
    OnHitStart(data);
    if (data.damage > 0) {
        if (splatBloodOnTile) g->GetTile(gridPos)->AddBloodSplat();
        ReduceHealth(data.damage);
        OnHealthReduced();
    }
    g->Emit(Ev::Hit, this, {}, gridPos, data.damage, data.attacker ? data.attacker->uid : 0);
    if (health > 0) {
        if (data.stun != 0 && isEnemy()) {
            Enemy* e = static_cast<Enemy*>(this);
            e->cStun->StunEnter(data.stun);
            e->IntentionClear();
        }
        int kb = data.knockback - knockbackResistance();
        if (kb < 0) kb = 0;
        if (kb > 0) {
            V2 end = g->GetObstacleFromStraightLine(gridPos, data.direction, kb, true)->gridPos;
            p.moveDistance = std::abs(end.x - gridPos.x) + std::abs(end.y - gridPos.y);
            MoveBegin(end);
            p.moving = true;
            return p;
        }
    } else {
        GA_Actor_Die(data);
    }
    HitEnd(p);
    return p;
}

void Actor::HitEnd(PendingHit& p) {
    if (p.done) return;
    p.done = true;
    if (p.moving) MoveEnd();
    OnHitEnd();
}

void Actor::ReduceHealth(int dmg) {
    health -= dmg;
    if (health < 0) health = 0;
}

void Actor::GA_Actor_Die(const AttackData&) {
    g->ClearTileOccupant(this, gridPos, true);
    isAlive = false;
    active = false;
    g->Emit(Ev::Died, this, {}, gridPos);
}

// ---------------------------------------------------------------------------------------------
// Stun
// ---------------------------------------------------------------------------------------------
void Stun::StunEnter(int duration) {
    Game* g = character->g;
    if (!stunned || duration == -1) {
        stunTurnDuration = duration;
        turnStunned = g->Turn();
    } else {
        int end = turnStunned + stunTurnDuration;
        if (g->Turn() + duration >= end) {
            stunTurnDuration = duration;
            turnStunned = g->Turn();
        }
    }
    stunned = true;
    g->Emit(Ev::Stunned, character, {}, character->gridPos, stunTurnDuration);
}

void Stun::StunExit() {
    stunned = false;
    character->g->Emit(Ev::StunEnded, character, {}, character->gridPos);
}

bool Stun::GetStunExpired() const {
    if (stunnedIndefinitely()) return false;
    return character->g->Turn() >= turnStunned + stunTurnDuration;
}

// ---------------------------------------------------------------------------------------------
// SpawnEnemy
// ---------------------------------------------------------------------------------------------
bool SpawnEnemy::SpawnBlocked(Game* g, V2 pos) {
    if (!g->PositionIsWithinField(pos)) return true;
    return !g->GetTile(pos)->isVacant();
}

Enemy* SpawnEnemy::Spawn(V2 pos) {
    FieldTile* t = g->GetTile(pos);
    if (t == nullptr || !t->isVacant()) {
        g->Emit(Ev::SpawnBlocked, nullptr, {}, pos);
        return nullptr;
    }
    Enemy* e = g->SpawnEnemyAt(pos, enemyToSpawn, this);
    currentLiveChildren++;
    spawnedAnyChildren = true;
    return e;
}

// ---------------------------------------------------------------------------------------------
// Character
// ---------------------------------------------------------------------------------------------
Character::Character(Game* game, EntityType t, const ActorPrefabFlags& f) : Actor(game, t, f), cStun(std::make_unique<Stun>(this)) {}
Character::~Character() = default;

void Character::AddSpawnComponent(const AttackConfig* config) {
    cSpawning = std::make_unique<SpawnEnemy>(g);
    cSpawning->enemyToSpawn = config->enemyToSpawn;
}

void Character::InitialiseAttack(const AttackConfig* config) {
    switch (config->attackBehaviour) {
    case AttackBehaviour::AdjacentTile:
        if (isPlayer()) cAttack = std::make_unique<PlayerAttack<AttackAdjacentTile>>(static_cast<Player*>(this), config);
        else cAttack = std::make_unique<AttackAdjacentTile>(this, config);
        break;
    case AttackBehaviour::Charge:
        if (isPlayer()) cAttack = std::make_unique<PlayerAttack<AttackCharge>>(static_cast<Player*>(this), config);
        else cAttack = std::make_unique<AttackCharge>(this, config);
        break;
    case AttackBehaviour::SpawnAdjacent:
        AddSpawnComponent(config);
        cAttack = std::make_unique<AttackSpawnAdjacent>(this, config);
        break;
    case AttackBehaviour::SpawnNearFurthestPlayer:
        AddSpawnComponent(config);
        cAttack = std::make_unique<AttackSpawnNearPlayer>(this, config);
        break;
    case AttackBehaviour::LightningBolt:
        cAttack = std::make_unique<AttackLightningBolt>(this, config);
        break;
    case AttackBehaviour::None:
        if (config->explodeOnDeath) cAttack = std::make_unique<AttackExplode>(this, config);
        else cAttack = std::make_unique<Attack>(this, config);
        break;
    }
}

void Character::OnHitStart(const AttackData& data) {
    if (data.CausesBallDrop(this)) {
        g->ball->BallDropped(gridPos);
        SetHasBall(false);
    }
}

void Character::GA_Char_Step(V2 direction) {
    std::vector<V2> tiles = g->GetMoveAndAttackTiles(gridPos, direction, moveRange());
    FieldTile* last = g->GetTile(tiles.back());
    V2 target;
    bool attackOnFinish;
    if (tiles.size() > 1) {
        if (last->isOccupiedByActor()) {
            target = tiles[tiles.size() - 2];
            attackOnFinish = true;
        } else {
            target = tiles.back();
            attackOnFinish = false;
        }
    } else {
        target = gridPos;
        attackOnFinish = false;
    }
    if (target != gridPos) {
        StepStarted();
        MoveToPosition(target);
    }
    if (attackOnFinish) cAttack->Attack_Coroutine(direction);
    StepComplete(direction);
}

void Character::CheckTileForBall() {
    if (!stunned() && isAlive && !holdingBall && g->GetTile(gridPos)->containsBall()) g->ball->CollectPickup(this);
}

void Character::AcquiredBall() { SetHasBall(true); }

void Character::SetHasBall(bool has) {
    holdingBall = has;
    if (has) g->ball->carrier = this;
}

bool Character::BallModifierBlocksAttack() const { return holdingBall && g->modifierConfig()->modifier == BallModifier::NoAttack; }
bool Character::BallModifierBlocksMove() const { return holdingBall && g->modifierConfig()->modifier == BallModifier::NoMove; }
bool Character::BallModifierBlocksPass() const { return holdingBall && g->modifierConfig()->modifier == BallModifier::NoPass; }
bool Character::BallModifierBlocksShoot() const { return holdingBall && g->modifierConfig()->modifier == BallModifier::NoShoot; }

// ---------------------------------------------------------------------------------------------
// Enemy (AI parts in ai.cpp)
// ---------------------------------------------------------------------------------------------
Enemy::Enemy(Game* game) : Character(game, EntityType::Enemy, game->data.prefabs.enemy) {}

bool Enemy::spawnOnMove() const { return config->attackConfig->spawnOnMove; }
bool Enemy::explodeOnDeath() const { return config->attackConfig->explodeOnDeath; }

void Enemy::Heal(int amount) {
    health += amount;
    if (health > maxHealth()) health = maxHealth();
    g->Emit(Ev::Healed, this, {}, gridPos, health);
}

void Enemy::ManualStart(V2 pos, const EnemyConfig* c, SpawnEnemy* parent) {
    SetGridPosition(pos);
    ApplyEnemyConfig(c);
    InitHealth();
    if (parent) spawnParent = parent;
}

void Enemy::ApplyEnemyConfig(const EnemyConfig* c) {
    config = c;
    if (config->stayBehindGoal) {
        minimumFieldX = g->fieldX - 1;
        maximumFieldX = g->fieldX - 1;
    } else {
        minimumFieldX = 0;
        maximumFieldX = g->fieldX - 2;
    }
    InitialiseAttack(config->attackConfig);
    if (spawnOnMove() && !cSpawning) AddSpawnComponent(config->attackConfig);
}

void Enemy::GA_Actor_Die(const AttackData& data) {
    IntentionClear();
    if (spawnParent) spawnParent->SpawnChildDied();
    // explodeOnDeath: Explosion.ExplodeAtPoint is empty in the shipped game.
    Actor::GA_Actor_Die(data);
    // CheckKillerForPerk dereferences attackData.attacker: a null attacker (Halloween "hurt all enemies")
    // throws a NullReferenceException in the original, so EnemyManager.EnemyDied is never reached and the
    // dead enemy stays in EnemyManager.enemies.
    if (data.attacker == nullptr) return;
    Player* killer = nullptr;
    if (data.attacker->entityType == EntityType::Player) killer = static_cast<Player*>(data.attacker);
    else if (data.attacker->entityType == EntityType::Ball) killer = g->ball->shooterPlayer;
    if (killer && killer->humanPlayer->HasPerk(Perk::KillsBoostDamage)) killer->humanPlayer->vampireKills++;
    g->EnemyDied(this);
}

void Enemy::StepComplete(V2 direction) {
    if (!spawnOnMove()) return;
    V2 pos = gridPos + V2{-direction.x, -direction.y};
    FieldTile* t = g->GetTile(pos);
    if (t && t->isVacant()) cSpawning->Spawn(pos);
}

void Enemy::OnMoveToPositionFinished() { Character::OnMoveToPositionFinished(); }

void Enemy::OnHitStart(const AttackData& data) {
    MarkerClear(true);
    Character::OnHitStart(data);
}

void Enemy::OnHitEnd() {
    if (isAlive && !stunned() && IntentionStillValid()) ShowEnemyIntention(false);
}

bool Enemy::LiveChildrenAtMaxCapacity() const {
    return cSpawning->currentLiveChildren >= config->attackConfig->maxLiveChildren;
}

// ---------------------------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------------------------
Player::Player(Game* game) : Character(game, EntityType::Player, game->data.prefabs.player) {}

void Player::ManualStart(bool hasBall, HumanPlayer* hp) {
    humanPlayer = hp;
    hp->playerReference = this;
    stamina = maxStamina();
    actionPoints = maxAP();
    hp->vampireKills = 0;
    const Prefabs& pf = g->data.prefabs;
    const AttackConfig* attack = hp->HasPerk(Perk::PierceAttack) ? pf.playerPierceAttack
                               : hp->HasPerk(Perk::ChargeAttack) ? pf.playerChargeAttack
                                                                 : pf.playerDefaultAttack;
    InitialiseAttack(attack);
    SetHasBall(hasBall);
}

void Player::SpendStamina(int cost) {
    stamina -= cost;
    if (stamina < 0) {
        std::fprintf(stderr, "Stamina went below 0.\n");
        stamina = 0;
    }
    g->Emit(Ev::Stamina, this, {}, gridPos, stamina);
}

void Player::ReplenishStamina(int value) {
    value += humanPlayer->PerkValue(Perk::RestBoost);
    stamina += value;
    if (stamina > maxStamina()) stamina = maxStamina();
    g->Emit(Ev::Stamina, this, {}, gridPos, stamina, value);
}

void Player::SpendActionPoints(int points) {
    actionPoints -= points;
    actionPoints = std::clamp(actionPoints, 0, maxAP());
}

void Player::StepStarted() {
    const Command& c = g->playerCommand();
    SpendStamina(g->StaminaCost(c.actionConfig, c.player, c.distance));
    SpendActionPoints(g->data.prefabs.configStep->actionPoints);
}

void Player::GA_Actor_Die(const AttackData& data) {
    g->PlayerDied(this);
    Actor::GA_Actor_Die(data);
}

void Player::GA_Char_Wait() {
    ReplenishStamina(g->playerCommand().staminaReplenish);
    SpendActionPoints(g->data.prefabs.configWait->actionPoints);
}

void Player::StartAttack() {
    const Command& c = g->playerCommand();
    SpendStamina(g->StaminaCost(c.actionConfig, c.player, c.distance));
    SpendActionPoints(g->data.prefabs.configAttack->actionPoints);
}

void Player::GA_Player_Shoot() {
    const Command& c = g->playerCommand();
    SpendStamina(g->StaminaCost(c.actionConfig, c.player, c.distance));
    SpendActionPoints(g->data.prefabs.configThrow->actionPoints);
    SetHasBall(false);
    g->ball->GA_Ball_Shoot(this);
}

void Player::GA_Player_Pass() {
    if (!holdingBall) return;
    Player* other = g->GetNextPlayerInFullList();
    const Command& c = g->playerCommand();
    SpendStamina(g->StaminaCost(c.actionConfig, c.player, c.distance));
    SpendActionPoints(g->data.prefabs.configPass->actionPoints);
    SetHasBall(false);
    g->ball->GA_Ball_Pass(this);
    if (humanPlayer->HasPerk(Perk::PassGivesStamina)) other->ReplenishStamina(humanPlayer->PerkValue(Perk::PassGivesStamina));
}

void Player::GA_Player_Take() {
    if (holdingBall || !g->ball->isHeldBySomeone()) return;
    Character* carrier = g->ball->carrier;
    const Command& c = g->playerCommand();
    SpendStamina(g->StaminaCost(c.actionConfig, c.player, c.distance));
    SpendActionPoints(g->data.prefabs.configTake->actionPoints);
    carrier->SetHasBall(false);
    AcquiredBall();
    g->Emit(Ev::BallAcquired, this, carrier->gridPos, gridPos);
}

void Player::OnMoveToPositionFinished() {
    Character::OnMoveToPositionFinished();
    if (positionOnMoveStarted != gridPos) tilesMovedThisTurn += RoundToInt(Distance(positionOnMoveStarted, gridPos));
    FieldTile* tile = g->GetTile(gridPos);
    while (!tile->pickups.empty()) tile->pickups[0]->CollectPickup(this);
    if (tile->hasBlood() && humanPlayer->HasPerk(Perk::BloodRestoresStamina)) {
        ReplenishStamina(humanPlayer->PerkValue(Perk::BloodRestoresStamina));
        tile->CleanBloodSplat();
    }
    if (g->PositionIsPastFinishLine(gridPos.x)) g->PlayerPassedFinishLine(this);
    else g->BroadcastPlayerMoved(gridPos);
}

void Player::AcquiredBall() {
    Character::AcquiredBall();
    if (humanPlayer->HasPerk(Perk::StaminaFromBallAcquire)) ReplenishStamina(humanPlayer->PerkValue(Perk::StaminaFromBallAcquire));
}

bool Player::SwitchToPlayerIsAllowed() const { return this != g->currentPlayer && actionPoints != 0; }

void Player::CheckAdjacentActors() {
    adjacentMonsters.clear();
    adjacentPlayers.clear();
    for (V2 d : kCardinal) {
        V2 p = gridPos + d;
        if (!g->PositionIsWithinField(p)) continue;
        FieldTile* t = g->GetTile(p);
        if (t->isVacant()) continue;
        if (t->containsEnemy()) adjacentMonsters.push_back(static_cast<Enemy*>(t->occupant));
        if (t->containsPlayer()) adjacentPlayers.push_back(static_cast<Player*>(t->occupant));
    }
}

void Player::NewTurn() {
    CheckTileForBall();
    tilesMovedThisTurn = 0;
}

void Player::CacheUndo() {
    undo.stamina = stamina;
    undo.actionPoints = actionPoints;
    undo.gridPos = gridPos;
    undo.isHoldingBall = holdingBall;
    undo.tilesMovedThisTurn = tilesMovedThisTurn;
}

void Player::RestoreUndo() {
    stamina = undo.stamina;
    actionPoints = undo.actionPoints;
    SetGridPosition(undo.gridPos);
    SetHasBall(undo.isHoldingBall);
    tilesMovedThisTurn = undo.tilesMovedThisTurn;
}

// ---------------------------------------------------------------------------------------------
// BreakableWall, Pickup, Treasure
// ---------------------------------------------------------------------------------------------
Obstacle::Obstacle(Game* game, const ObstacleConfig* c) : Actor(game, EntityType::Obstacle, game->data.prefabs.wall), config(c) {
    InitHealth();
}

void Pickup::CollectPickup(Character*) {
    g->RemovePickupFromTile(this, gridPos);
    SetPickupActive(false);
    acquired = true;
}

Treasure::Treasure(Game* game, const TreasureConfig* c) : Pickup(game, EntityType::Pickup, game->data.prefabs.treasure), config(c) {}

void Treasure::InitialiseTreasure() {
    Run& run = g->run;
    switch (run.session.seasonalEvent) {
    case SeasonalEvent::Halloween:
        if (run.IsHalloween()) {
            int num = SeededRandom(run.halloweenRng, 0, 8);
            halloweenEvent = static_cast<HalloweenTrickOrTreat>(num);
            halloweenIsTrick = num <= static_cast<int>(HalloweenTrickOrTreat::EnemiesHealedFully);
        }
        break;
    case SeasonalEvent::Easter:
        if (run.IsEaster()) {
            spawnEnemy = std::make_unique<SpawnEnemy>(g);
            spawnEnemy->enemyToSpawn = g->data.prefabs.easterEnemyToSpawn;
            HideForEaster();
        }
        break;
    default:
        break;
    }
}

void Treasure::CollectPickup(Character* c) {
    Pickup::CollectPickup(c);
    // Gold.AcquiredTreasure
    g->stageTreasures.push_back(this);
    if (config->treasureType == TreasureType::Gem) g->gems++;
    g->Emit(Ev::TreasureTaken, c, {}, gridPos);
    Run& run = g->run;
    if (run.session.seasonalEvent == SeasonalEvent::Halloween) {
        if (!run.IsHalloween() || config->seasonalVariant != SeasonalEvent::Halloween || !c->isPlayer()) return;
        g->BlockUndoOnNextAction();
        if (gridPos.x == g->fieldX - 1) return;
        Player* player = static_cast<Player*>(c);
        g->Emit(Ev::TrickOrTreat, c, {}, gridPos, static_cast<int>(halloweenEvent), halloweenIsTrick ? 1 : 0);
        switch (halloweenEvent) {
        case HalloweenTrickOrTreat::StaminaDrained: player->DrainStamina(); break;
        case HalloweenTrickOrTreat::StaminaDrainedAll:
            for (Player* p : std::vector<Player*>(g->players)) p->DrainStamina();
            break;
        case HalloweenTrickOrTreat::EnemiesHealedSmall: g->HealAllEnemies(1); break;
        case HalloweenTrickOrTreat::EnemiesHealedFully: g->HealAllEnemiesFully(); break;
        case HalloweenTrickOrTreat::StaminaReplenished: player->ReplenishStamina(player->maxStamina()); break;
        case HalloweenTrickOrTreat::StaminaReplenishedAll:
            for (Player* p : std::vector<Player*>(g->players)) p->ReplenishStamina(player->maxStamina());
            break;
        case HalloweenTrickOrTreat::EnemiesHurtSmall: g->DamageAllEnemies(1); break;
        case HalloweenTrickOrTreat::EnemiesStunned: g->StunAllEnemies(); break;
        default: break;
        }
    } else if (run.session.seasonalEvent == SeasonalEvent::Easter) {
        if (run.IsEaster() && config->seasonalVariant == SeasonalEvent::Easter && c->isPlayer()) {
            Player* p = static_cast<Player*>(c);
            p->ReplenishStamina(p->maxStamina());
        }
    }
}

int Treasure::GetScoreValue() const {
    if (config->treasureType == TreasureType::Gem) return g->run.session.treasureConfig->scoreValue;
    return 0;
}

void Treasure::HideForEaster() {
    hiddenForEaster = true;
    easterEggCountdown = 3; // easterEggCountdownDuration (prefab)
    proximityCheckEnabled = true;
}

void Treasure::RevealForEaster() {
    hiddenForEaster = false;
    proximityCheckEnabled = false;
}

void Treasure::OnNewTurn() {
    if (!spawnEnemy) return; // only subscribed for Easter
    if (g->Turn() > 1 && active && !hiddenForEaster && !acquired) {
        easterEggCountdown--;
        if (easterEggCountdown <= 0) {
            Enemy* e = spawnEnemy->Spawn(gridPos);
            if (e) e->DecideNextAction();
            g->RemovePickupFromTile(this, gridPos);
            SetPickupActive(false);
        }
    }
}

bool Treasure::CheckProximity(V2 pos) {
    if (Distance(gridPos, pos) > 2.0f) return false;
    RevealForEaster();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Ball
// ---------------------------------------------------------------------------------------------
Ball::Ball(Game* game) : Pickup(game, EntityType::Ball, game->data.prefabs.ball) {}

bool Ball::isHeldByPlayer() const { return carrier && carrier->isPlayer(); }

void Ball::CollectPickup(Character* c) {
    Pickup::CollectPickup(c);
    BallAcquired(c);
}

void Ball::GA_Ball_Shoot(Player* p) {
    shooterPlayer = p;
    V2 end = g->GetObstacleFromStraightLine(p->gridPos, kTowardsGoal, 99999, false)->gridPos;
    g->Emit(Ev::BallShot, p, p->gridPos, end);
    Moving(p->gridPos, end, false);
    if (g->PositionIsPastFinishLine(GetBallPosition().x) && !isHeldBySomeone()) shotToFinishLine = true;
}

void Ball::GA_Ball_Pass(Player* p) {
    V2 start = p->gridPos;
    V2 end = g->GetNextPlayerInFullList()->gridPos;
    g->Emit(Ev::BallPass, p, start, end);
    Moving(start, end, true);
}

void Ball::Moving(V2 start, V2 end, bool isPass) {
    SetGridPosition(start);
    FieldTile* tile = g->GetTile(end);
    if (!tile->isOccupiedByActor()) {
        BallDropped(end);
        return;
    }
    Actor* target = tile->occupant;
    bool caught = isPass || ShotTargetShouldCatchBall(target);
    if (caught) {
        BallAcquired(static_cast<Character*>(target));
        if (!isPass && target->isPlayer() && shooterPlayer->humanPlayer->HasPerk(Perk::PassGivesStamina))
            static_cast<Player*>(target)->ReplenishStamina(shooterPlayer->humanPlayer->PerkValue(Perk::PassGivesStamina));
    }
    if (isPass) return;
    if (!target->isEnemy() && !target->isObstacle() && !target->isPlayer()) return;
    int knockback = GetKnockbackTotal(shooterPlayer);
    int damage = 0, stun = 0, rebound = 0;
    if (!caught) {
        damage = GetDamageTotal();
        stun = GetStunDuration();
        rebound = GetReboundTotal();
    }
    AttackData data{kTowardsGoal, this, damage, knockback, stun, caught, true};
    PendingHit hit = target->HitBegin(data);
    V2 reboundPos = end;
    if (rebound > 0) {
        reboundPos = end - kTowardsGoal * rebound;
        if (reboundPos.x < shooterPlayer->gridPos.x) reboundPos.x = shooterPlayer->gridPos.x;
        g->Emit(Ev::BallRebound, this, end, reboundPos);
        BallDropped(reboundPos);
    }
    target->HitEnd(hit);
    if (rebound <= 0) return;
    FieldTile* t2 = g->GetTile(reboundPos);
    if (t2->isOccupiedByActor() && t2->occupant->isCharacter()) static_cast<Character*>(t2->occupant)->CheckTileForBall();
}

void Ball::BallAcquired(Character* c) {
    c->AcquiredBall();
    carrier = c;
    SetPickupActive(false);
    SetGridPosition(c->gridPos);
    if (c->entityType != teamCarrying && c->isPlayer() && teamCarrying != EntityType::Player) ballWasRecoveredFromEnemies = true;
    teamCarrying = c->entityType;
    if (!c->isPlayer()) shotToFinishLine = false;
    g->Emit(Ev::BallAcquired, c, {}, c->gridPos);
}

void Ball::BallDropped(V2 pos) {
    carrier = nullptr;
    SetGridPosition(pos);
    g->AddPickupToTile(this, pos);
    SetPickupActive(true);
    g->Emit(Ev::BallDropped, this, {}, pos);
}

// With no explicit player these read the carrier (still set while a shot is in flight) or the shooter.
static Player* BallPlayer(const Ball* b, Player* p) {
    if (p) return p;
    return b->isHeldBySomeone() ? static_cast<Player*>(b->carrier) : b->shooterPlayer;
}

int Ball::GetDamageTotal(Player* p) const {
    int n = BallPlayer(this, p)->humanPlayer->GetBallDamage();
    if (g->modifierConfig()->modifier == BallModifier::Damage) n += g->modifierConfig()->value;
    return n;
}

int Ball::GetKnockbackTotal(Player* p) const {
    int n = BallPlayer(this, p)->humanPlayer->GetBallKnockback();
    if (g->modifierConfig()->modifier == BallModifier::Knockback) n += g->modifierConfig()->value;
    return n;
}

int Ball::GetReboundTotal(Player* p) const {
    int n = 1 + BallPlayer(this, p)->humanPlayer->PerkValue(Perk::BallRebound);
    if (g->modifierConfig()->modifier == BallModifier::Rebound) n += g->modifierConfig()->value;
    return n;
}

int Ball::GetStunDuration(Player* p) const {
    int n = BallPlayer(this, p)->humanPlayer->GetBallStun();
    if (g->modifierConfig()->modifier == BallModifier::Stun) n += g->modifierConfig()->value;
    return n;
}

int Ball::GetStaminaPenalty() const {
    int n = 1;
    if (g->modifierConfig()->modifier == BallModifier::StaminaPenalty) n += g->modifierConfig()->value;
    return n;
}

bool Ball::ShotTargetShouldCatchBall(Actor* a) const {
    if (a->isEnemy()) {
        Enemy* e = static_cast<Enemy*>(a);
        if (e->stunned()) return false;
        return e->config->ballCatcher;
    }
    return a->isPlayer();
}

// ---------------------------------------------------------------------------------------------
// Hazards (FloorSpikes / LightningHazard)
// ---------------------------------------------------------------------------------------------
Hazard::Hazard(Game* game, const HazardConfig* c, V2 pos) : Entity(game, EntityType::Hazard), config(c), gridPos(pos) {}

void Hazard::PerformAction() {
    FieldTile* tile = g->GetTile(gridPos);
    g->Emit(Ev::HazardTriggered, this, {}, gridPos, static_cast<int>(config->kind));
    if (!tile->isOccupiedByActor()) return;
    if (isSpikeTrap()) {
        bool immune = tile->occupant->isPlayer() &&
                      static_cast<Player*>(tile->occupant)->humanPlayer->HasPerk(Perk::SpikeImmunity);
        if (!immune) tile->occupant->Hit(AttackData{kNone, this, config->damage, 0, 0, false, false});
    } else {
        const AttackConfig* a = g->data.prefabs.lightningAttackConfig;
        tile->occupant->Hit(AttackData{kNone, this, a->attackDamage, a->knockbackDistance, a->stunDuration, a->wind, false});
    }
}

} // namespace dd::sim
