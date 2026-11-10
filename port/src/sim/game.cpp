// GameController, EnemyTurn, HazardManager, EnemyManager, PlayerManager, UndoManager, FieldGenerator,
// SetTileCommands / CommandObjectButton, StageScore and Gold for one stage.
#include <algorithm>
#include <cstdio>

#include "sim/sim.h"

namespace dd::sim {

const char* PhaseName(Phase p) {
    switch (p) {
    case Phase::Starting: return "Starting";
    case Phase::PlayerInput: return "PlayerInput";
    case Phase::PlayerActing: return "PlayerActing";
    case Phase::EnemiesActing: return "EnemiesActing";
    case Phase::StageEnded: return "StageEnded";
    case Phase::HazardsActing: return "HazardsActing";
    case Phase::SuggestUndo: return "SuggestUndo";
    }
    return "?";
}

Game::Game(Run& r) : run(r), data(r.data), enemyRng(r.session.seed) {}
Game::~Game() = default;

void Game::Emit(Ev type, const Entity* actor, V2 from, V2 to, int value, int extra) {
    Event e;
    e.type = type;
    e.actor = actor ? actor->uid : -1;
    e.from = from;
    e.to = to;
    e.value = value;
    e.extra = extra;
    events.push_back(std::move(e));
    if (meta) meta->OnEvent(*this, events.back());
}

void Game::Log(const std::string& text) {
    Event e;
    e.text = text;
    events.push_back(std::move(e));
}

Entity* Game::FindEntity(int uid) const {
    for (const auto& e : entities)
        if (e->uid == uid) return e.get();
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Spawning (FieldGenerator / EnemySpawner / PlayerManager.SpawnPlayers)
// ---------------------------------------------------------------------------------------------
Enemy* Game::SpawnEnemyAt(V2 pos, const EnemyConfig* c, SpawnEnemy* parent) {
    auto owned = std::make_unique<Enemy>(this);
    Enemy* e = owned.get();
    entities.push_back(std::move(owned));
    e->ManualStart(pos, c, parent);
    enemies.push_back(e);
    SetTileOccupant(e, pos);
    Emit(Ev::Spawned, e, {}, pos);
    e->CheckTileForBall();
    e->actOrder = static_cast<int>(enemies.size());
    return e;
}

void Game::SpawnWall(V2 pos) {
    FieldTile* t = GetTile(pos);
    if (t->containsObstacle()) return;
    auto owned = std::make_unique<Obstacle>(this, data.session.wallConfig);
    Obstacle* w = owned.get();
    entities.push_back(std::move(owned));
    w->SetGridPosition(pos);
    t->occupant = w;
}

void Game::SpawnHazard(V2 pos, const HazardConfig* c) {
    FieldTile* t = GetTile(pos);
    if (t->containsHazard()) return;
    auto owned = std::make_unique<Hazard>(this, c, pos);
    Hazard* h = owned.get();
    entities.push_back(std::move(owned));
    t->hazard = h;
    hazards.push_back(h); // HazardManager.AddHazard (Hazard.Start)
}

void Game::SpawnTreasure(V2 pos, const TreasureConfig* c) {
    FieldTile* t = GetTile(pos);
    auto owned = std::make_unique<Treasure>(this, c);
    Treasure* tr = owned.get();
    entities.push_back(std::move(owned));
    tr->acquired = false;
    tr->SetGridPosition(pos);
    t->pickups.push_back(tr);
    tr->InitialiseTreasure();
    treasures.push_back(tr);
}

void Game::SpawnPlayers() {
    V2 starts[2] = {{0, fieldY - 1}, {0, 0}};
    // PlayerManager.SwapPlayerPositions: two players trade places, a lone player takes the other start
    if (run.swapPositions) std::swap(starts[0], starts[1]);
    for (size_t i = 0; i < run.activePlayers.size() && i < 2; i++) {
        HumanPlayer* hp = run.activePlayers[i];
        if (run.ballCarrierIndex >= static_cast<int>(run.activePlayers.size())) run.ballCarrierIndex = 0;
        bool hasBall = static_cast<int>(i) == run.ballCarrierIndex;
        auto owned = std::make_unique<Player>(this);
        Player* p = owned.get();
        entities.push_back(std::move(owned));
        players.push_back(p);
        allPlayers_.push_back(p);
        SetTileOccupant(p, starts[i]);
        p->SetGridPosition(starts[i]);
        p->ManualStart(hasBall, hp);
    }
    currentPlayer = players.empty() ? nullptr : players[0];
}

// ---------------------------------------------------------------------------------------------
// GameController
// ---------------------------------------------------------------------------------------------
void Game::Start() {
    const Stage& stage = run.CurrentStage();
    modifier_ = run.BallModConfig();
    {
        auto owned = std::make_unique<Ball>(this);
        ball = owned.get();
        entities.push_back(std::move(owned));
        ball->SetPickupActive(false);
    }
    turn_ = 0;
    turnPar_ = stage.par;
    playerOnFinishLine_ = nullptr;
    // FieldManager.Initialise
    fieldX = stage.gridX;
    fieldY = stage.gridY;
    grid.assign(static_cast<size_t>(fieldX * fieldY), FieldTile{});
    for (int x = 0; x < fieldX; x++)
        for (int y = 0; y < fieldY; y++) grid[x * fieldY + y].gridPos = {x, y};
    // FieldGenerator.SpawnOccupants
    for (int x = 0; x < stage.gridX; x++) {
        for (int y = 0; y < stage.gridY; y++) {
            const TileData& td = stage.At(x, y);
            if (td.enemyConfig) SpawnEnemyAt({x, y}, td.enemyConfig, nullptr);
            else if (td.obstacleConfig) SpawnWall({x, y});
            else if (td.hazardConfig) SpawnHazard({x, y}, td.hazardConfig);
            else if (td.treasureConfig) SpawnTreasure({x, y}, td.treasureConfig);
        }
    }
    SpawnPlayers();
    // Gold.NewStage
    stageTreasures.clear();
    gems = 0;
    // EasterEggProximityCheck.Start
    for (Treasure* t : treasures) {
        if (!t->proximityCheckEnabled) continue;
        for (Player* p : players)
            if (t->CheckProximity(p->gridPos)) break;
    }
    // StageStartSequences: enemies revealed, then actions decided, then the first turn.
    DecideNextActions();
    NextTurn();
}

void Game::ChangePhase(Phase p) {
    if (p == phase_) return;
    phase_ = p;
    Emit(Ev::PhaseChanged, nullptr, {}, {}, static_cast<int>(p));
    switch (p) {
    case Phase::PlayerInput: StartPlayerInput(); break;
    case Phase::PlayerActing: ExecutePlayerAction(); break;
    case Phase::EnemiesActing: EnemyTurn(); break;
    case Phase::HazardsActing: HazardsTurn(); break;
    case Phase::StageEnded:
        run.ResetStagePlayerReferences();
        CalculateStageScore();
        ClearAndHideAllIntentions();
        if (meta) meta->OnStageEnded(*this);
        break;
    default: break;
    }
}

void Game::NextTurn() {
    undoValid_ = false;
    TryLightningHazard();
    turn_++;
    Emit(Ev::TurnStarted, nullptr, {}, {}, turn_);
    CheckAllPlayersAdjacentActors();
    // GameController.OnNewTurnStarted subscribers: Easter treasures, then players (enemies only reset an
    // achievement counter).
    for (Treasure* t : std::vector<Treasure*>(treasures)) t->OnNewTurn();
    for (Player* p : std::vector<Player*>(players))
        if (p->active) p->NewTurn();
    StartNewTurnPlayers();
    ChangePhase(Phase::PlayerInput);
    CacheValues();
}

void Game::StartPlayerInput() {
    command_ = Command{};
    CheckAllPlayersAdjacentActors();
    RefreshAllActOrders();
    RefreshIntentionMarkers();
}

bool Game::Select(const Command& c) {
    if (phase() != Phase::PlayerInput || !c.actionConfig || !c.interactable) return false;
    // CommandObject.Select
    if (c.staminaCost > currentPlayer->stamina) return false;
    if (c.actionConfig->switchControl) {
        if (c.onTile && !(c.player && c.player->SwitchToPlayerIsAllowed())) return false;
        SwitchControlToNextPlayer();
        return true;
    }
    if (c.actionConfig->undo) {
        TriggerUndo();
        return true;
    }
    // Command.ConfirmCommand -> PlayerCommand.Set (recomputes the costs) -> GameController.PlayerEnteredCommand
    command_ = MakeCommand(c.actionConfig, c.player, c.direction, c.distance, c.onTile, c.tile);
    ChangePhase(Phase::PlayerActing);
    return true;
}

void Game::ExecutePlayerAction() {
    Command command = command_;
    Player* p = command.player;
    if (meta) meta->OnPlayerAction(*this, command);
    switch (command.actionConfig->action) {
    case ActorAction::Char_Step: p->GA_Char_Step(command.direction); break;
    case ActorAction::Char_Attack: p->cAttack->Attack_Coroutine(command.direction); break;
    case ActorAction::Char_Wait: p->GA_Char_Wait(); break;
    case ActorAction::Player_Shoot: p->GA_Player_Shoot(); break;
    case ActorAction::Player_Pass: p->GA_Player_Pass(); break;
    case ActorAction::Player_TakeBall: p->GA_Player_Take(); break;
    default: break;
    }
    if (anyPlayerPassedFinishLine()) {
        PlayerWon();
        return;
    }
    PlayerActed(command.actionConfig);
    PlayerFinishedActing();
    if (!playersWaitingToAct.empty()) {
        ChangePhase(Phase::PlayerInput);
        return;
    }
    FinishedPlayerTurn();
    if (suggestUndo && undoValid_ && AnyPlayersInDanger()) {
        awaitingUndo_ = true; // GameController.SuggestUndo: the player answers RespondToUndoSuggestion
        return;
    }
    GoToNextPhaseAfterPlayer();
}

void Game::RespondToUndoSuggestion(bool acceptUndo) {
    if (!awaitingUndo_) return;
    awaitingUndo_ = false;
    if (acceptUndo) {
        TriggerUndo();
        ChangePhase(Phase::PlayerInput);
    } else {
        GoToNextPhaseAfterPlayer();
    }
}

void Game::GoToNextPhaseAfterPlayer() { ChangePhase(hazards.empty() ? Phase::EnemiesActing : Phase::HazardsActing); }

void Game::FinishedPlayerTurn() { RefreshIntentionMarkers(); }

// HazardManager.ProcessHazards_Coroutine: hazards act in list order; hits never move anyone, so the
// overlapping coroutines resolve in that same order.
void Game::HazardsTurn() {
    for (size_t i = 0; i < hazards.size(); i++) hazards[i]->PerformAction();
    // GameController.FinishedHazardsTurn
    if (GetNumberOfLivePlayers() <= 0) PlayerLost();
    else ChangePhase(Phase::EnemiesActing);
}

void Game::TryLightningHazard() {
    if (turn_ <= turnPar_) return;
    int num = (turnPar_ - turn_) * -1;
    if (lightningHazardRowsSpawned >= num) return;
    if (num >= fieldX) return;
    for (int y = 0; y < fieldY; y++) {
        V2 p{lightningHazardRowsSpawned, y};
        if (!GetTile(p)->containsHazard()) SpawnHazard(p, data.session.lightningHazardConfig);
    }
    Emit(Ev::LightningRow, nullptr, {}, {}, lightningHazardRowsSpawned);
    lightningHazardRowsSpawned++;
}

void Game::EnemyTurn() {
    if (enemies.empty()) {
        FinishedEnemyTurn();
        return;
    }
    // EnemyManager.ApplyHealthLoss: GA_Enemy_HurtSelf never starts its coroutine, so it only adds a delay.
    std::vector<Enemy*> queue(enemies);
    for (Enemy* e : queue) {
        if (!e->isAlive) continue;
        if (e->stunned()) {
            if (e->cStun->GetStunExpired()) e->cStun->StunExit();
            e->CheckTileForBall();
            continue;
        }
        e->CheckTileForBall();
        if (!e->hasIntention || e->nextAction.action == ActorAction::Char_Wait) continue;
        e->MarkerClear(true);
        EnemyAction a = e->nextAction;
        switch (a.action) {
        case ActorAction::Char_Step: e->GA_Char_Step(a.direction); break;
        case ActorAction::Char_Attack: e->cAttack->Attack_Coroutine(a.direction); break;
        default: break;
        }
        e->IntentionClear();
        RefreshIntentionMarkers();
        if (GetNumberOfLivePlayers() <= 0) {
            PlayerLost();
            return;
        }
        if (anyPlayerPassedFinishLine()) {
            PlayerWon();
            return;
        }
    }
    FinishedEnemyTurn();
}

void Game::FinishedEnemyTurn() {
    DecideNextActions();
    NextTurn();
}

void Game::PlayerWon() {
    Emit(Ev::StageWon, playerOnFinishLine_);
    ChangePhase(Phase::StageEnded);
}

// GameFlow.RestartAllowed offers a restart in the original; the simulation ends the stage in defeat.
void Game::PlayerLost() {
    gameEndedInDefeat = true;
    Emit(Ev::StageLost);
    ChangePhase(Phase::StageEnded);
}

// ---------------------------------------------------------------------------------------------
// EnemyManager
// ---------------------------------------------------------------------------------------------
void Game::EnemyDied(Enemy* e) {
    auto it = std::find(enemies.begin(), enemies.end(), e);
    if (it != enemies.end()) enemies.erase(it);
}

// The OnAllDecideNextAction / OnAllRefreshIntentionMarkers delegates hold the active enemies in the
// order they were enabled, which is EnemyManager.enemies order.
void Game::DecideNextActions() {
    for (Enemy* e : std::vector<Enemy*>(enemies))
        if (e->active) e->DecideNextAction();
}

void Game::RefreshIntentionMarkers() {
    SetAllTilesToUndesired();
    for (Enemy* e : std::vector<Enemy*>(enemies))
        if (e->active) e->RefreshIntentionMarker();
}

void Game::ClearAndHideAllIntentions() {
    SetAllTilesToUndesired();
    for (Enemy* e : std::vector<Enemy*>(enemies))
        if (e->active) e->IntentionClear();
}

void Game::RefreshAllActOrders() {
    for (size_t i = 0; i < enemies.size(); i++) enemies[i]->actOrder = static_cast<int>(i) + 1;
}

void Game::StunAllEnemies() {
    for (Enemy* e : std::vector<Enemy*>(enemies)) {
        e->cStun->StunEnter(1);
        e->IntentionClear();
    }
}

void Game::DamageAllEnemies(int damage) {
    AttackData ad{kNone, nullptr, damage, 0, 0, false, false};
    std::vector<PendingHit> hits;
    for (Enemy* e : std::vector<Enemy*>(enemies)) hits.push_back(e->HitBegin(ad));
    for (PendingHit& h : hits) h.actor->HitEnd(h);
}

void Game::HealAllEnemiesFully() {
    for (Enemy* e : enemies) {
        e->InitHealth();
        Emit(Ev::Healed, e, {}, e->gridPos, e->health);
    }
}

void Game::HealAllEnemies(int amount) {
    for (Enemy* e : enemies) e->Heal(amount);
}

// ---------------------------------------------------------------------------------------------
// PlayerManager
// ---------------------------------------------------------------------------------------------
void Game::StartNewTurnPlayers() {
    int traitAutoRestore = run.team->traitStaminaAutoRestore();
    for (Player* p : players) {
        HumanPlayer* hp = p->humanPlayer;
        p->ReplenishActionPoints();
        p->CheckAdjacentActors();
        int num = hp->PerkValue(Perk::StaminaAutoRecovery) + traitAutoRestore;
        if (hp->HasPerk(Perk::StaminaBoostSurrounded)) {
            int n = static_cast<int>(p->adjacentMonsters.size());
            int cap = hp->PerkValue(Perk::StaminaBoostSurrounded);
            if (n > cap) n = cap;
            num += 2 * n;
        }
        for (Player* other : p->adjacentPlayers) num += other->humanPlayer->PerkValue(Perk::TransferStaminaOnAdjacent);
        if (hp->HasPerk(Perk::BloodRestoresStamina)) {
            FieldTile* t = GetTile(p->gridPos);
            if (t->hasBlood()) {
                num += hp->PerkValue(Perk::BloodRestoresStamina);
                t->CleanBloodSplat();
            }
        }
        if (num > 0) p->ReplenishStamina(num);
    }
    for (Player* p : players)
        if (p->isAlive) playersWaitingToAct.push_back(p);
    if (!playersWaitingToAct.empty()) SwitchControl(playersWaitingToAct[0]);
}

void Game::PlayerFinishedActing() {
    if (currentPlayer->actionPoints <= 0) {
        auto it = std::find(playersWaitingToAct.begin(), playersWaitingToAct.end(), currentPlayer);
        if (it != playersWaitingToAct.end()) playersWaitingToAct.erase(it);
        if (!playersWaitingToAct.empty()) SwitchControlToNextPlayer();
    }
}

Player* Game::GetNextPlayerInFullList() {
    int num = 0;
    auto it = std::find(players.begin(), players.end(), currentPlayer);
    if (it != players.end()) num = static_cast<int>(it - players.begin());
    int next = num + 1;
    if (next > static_cast<int>(players.size()) - 1) next = 0;
    return players[next];
}

Player* Game::GetNextPlayerInWaitingList() {
    int num = 0;
    auto it = std::find(playersWaitingToAct.begin(), playersWaitingToAct.end(), currentPlayer);
    if (it != playersWaitingToAct.end()) num = static_cast<int>(it - playersWaitingToAct.begin());
    int next = num + 1;
    if (next > static_cast<int>(playersWaitingToAct.size()) - 1) next = 0;
    return playersWaitingToAct[next];
}

void Game::SwitchControl(Player* next) {
    currentPlayer = next;
    Emit(Ev::CurrentPlayer, next, {}, next->gridPos);
}

int Game::GetNumberOfLivePlayers() const {
    int n = 0;
    for (const Player* p : players)
        if (p->isAlive) n++;
    return n;
}

void Game::CheckAllPlayersAdjacentActors() {
    for (Player* p : players) p->CheckAdjacentActors();
}

bool Game::AnyPlayersInDanger() {
    for (Player* p : players)
        if (p->isAlive && GetTile(p->gridPos)->ContainsPlayerInDanger()) return true;
    return false;
}

void Game::PlayerDied(Player* p) {
    players.erase(std::remove(players.begin(), players.end(), p), players.end());
    run.PlayerDied(p->humanPlayer->playerNumber);
    playersWaitingToAct.erase(std::remove(playersWaitingToAct.begin(), playersWaitingToAct.end(), p), playersWaitingToAct.end());
    deathsThisStage++;
    if (run.ballCarrierIndex > 0) run.ballCarrierIndex--;
}

void Game::BroadcastPlayerMoved(V2 pos) {
    for (Treasure* t : treasures)
        if (t->proximityCheckEnabled) t->CheckProximity(pos);
}

Player* Game::PlayerById(int uid) const {
    for (Player* p : allPlayers_)
        if (p->uid == uid) return p;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// UndoManager (subscribers: UndoBall, UndoFieldTile, UndoTreasure, UndoPlayer in Awake order)
// ---------------------------------------------------------------------------------------------
void Game::PlayerActed(const PlayerActionConfig* c) {
    if (run.blockUndoOnNextAction) {
        undoValid_ = false;
        run.blockUndoOnNextAction = false;
    } else {
        ActorAction a = c->action;
        bool invalidates = !(a == ActorAction::Char_Step || a == ActorAction::Char_Wait || a == ActorAction::Player_AcquireBall ||
                             a == ActorAction::Player_Pass || a == ActorAction::Player_TakeBall);
        undoValid_ = !invalidates;
    }
    if (!undoValid_) CacheValues();
}

void Game::CacheValues() {
    undoValid_ = false;
    ball->cachedGridPos = ball->gridPos;
    ball->cachedCarrier = ball->carrier;
    for (FieldTile& t : grid) t.cachedBloodSplats = t.bloodSplats;
    for (Treasure* t : treasures) {
        t->cachedAcquired = t->acquired;
        t->cachedHiddenForEaster = t->hiddenForEaster;
    }
    for (Player* p : allPlayers_) p->CacheUndo();
}

void Game::TriggerUndo() {
    undoValid_ = false;
    // UndoBall
    if (ball->cachedCarrier) {
        if (ball->carrier == nullptr) {
            ball->CollectPickup(ball->cachedCarrier);
        } else if (ball->carrier != ball->cachedCarrier) {
            ball->BallDropped(ball->cachedCarrier->gridPos);
            ball->CollectPickup(ball->cachedCarrier);
        }
    } else {
        if (ball->carrier) {
            ball->BallDropped(ball->cachedGridPos);
            ball->acquired = false;
        }
        if (ball->cachedGridPos != ball->gridPos) ball->SetGridPosition(ball->cachedGridPos);
    }
    // UndoFieldTile
    for (FieldTile& t : grid) t.bloodSplats = t.cachedBloodSplats;
    // UndoTreasure
    for (Treasure* t : treasures) {
        if (!t->cachedAcquired && t->acquired) {
            t->acquired = false;
            t->SetPickupActive(true);
            AddPickupToTile(t, t->gridPos);
            auto it = std::find(stageTreasures.begin(), stageTreasures.end(), t);
            if (it != stageTreasures.end()) {
                stageTreasures.erase(it);
                if (t->config->treasureType == TreasureType::Gem) gems--;
            }
        }
        if (run.IsEaster() && t->cachedHiddenForEaster && !t->hiddenForEaster && !t->acquired) t->HideForEaster();
    }
    // UndoPlayer
    for (Player* p : allPlayers_) p->RestoreUndo();
    ForceRefreshAllOccupants();
    // PlayerManager.Undo
    playersWaitingToAct.clear();
    for (Player* p : players)
        if (p->actionPoints > 0) playersWaitingToAct.push_back(p);
    if (!playersWaitingToAct.empty()) currentPlayer = playersWaitingToAct[0];
    CheckAllPlayersAdjacentActors();
    RefreshIntentionMarkers();
    Emit(Ev::Undo);
}

// ---------------------------------------------------------------------------------------------
// Commands (Calculate, Command.Set, SetTileCommands, CommandObjectButton)
// ---------------------------------------------------------------------------------------------
int Game::StaminaCost(const PlayerActionConfig* c, const Player* p, int distance) const {
    if (c->noStaminaCost || c->switchControl || c->undo) return 0;
    int num = c->staminaCost;
    if (p->isHoldingBall()) num += ball->GetStaminaPenalty();
    if (c->action == ActorAction::Char_Attack) {
        num += p->cAttack->additionalStaminaCost();
        if (p->cAttack->attackBehaviour() == AttackBehaviour::Charge) num += distance - 1;
    }
    num -= p->humanPlayer->PerkValue(Perk::StaminaReduction);
    num += run.team->traitStaminaCosts();
    if (num < 1) num = 1;
    return num;
}

Command Game::MakeCommand(const PlayerActionConfig* c, Player* p, V2 dir, int distance, bool onTile, V2 tile) {
    Command cmd;
    cmd.actionConfig = c;
    cmd.player = p;
    cmd.direction = dir;
    cmd.distance = distance;
    cmd.staminaCost = StaminaCost(c, p, distance);
    cmd.staminaReplenish = c->action == ActorAction::Char_Wait ? data.levelUp.restBase : 0;
    cmd.actionPointCost = c->actionPoints;
    cmd.onTile = onTile;
    cmd.tile = tile;
    cmd.tooExpensive = currentPlayer && cmd.staminaCost > currentPlayer->stamina;
    return cmd;
}

std::vector<Command> Game::GetCommands() {
    std::vector<Command> out;
    if (phase() != Phase::PlayerInput || !currentPlayer) return out;
    Player* player = currentPlayer;
    const Prefabs& pf = data.prefabs;
    std::vector<int> tileCommand(grid.size(), -1); // CommandObjectFieldTile.hasCommand
    auto has = [&](V2 p) { return tileCommand[p.x * fieldY + p.y] >= 0; };
    auto set = [&](V2 p, const PlayerActionConfig* c, V2 dir, int distance) {
        Player* associated = c->switchControl ? static_cast<Player*>(GetTile(p)->occupant) : player;
        tileCommand[p.x * fieldY + p.y] = static_cast<int>(out.size());
        out.push_back(MakeCommand(c, associated, dir, distance, true, p));
    };
    auto attackTileAllowed = [&](FieldTile* t) {
        return !has(t->gridPos) && t->gridPos != player->gridPos && t->isOccupiedByActor() && !t->containsPlayer();
    };
    // SetSwitchControlTile
    if (GetNumberOfLivePlayers() > 1) {
        Player* next = GetNextPlayerInWaitingList();
        if (next != player) set(next->gridPos, pf.configSwitchControl, kNone, 0);
    }
    for (V2 dir : {kUp, kDown, kLeft, kRight}) {
        V2 pos = player->gridPos + dir;
        if (!PositionIsWithinField(pos)) continue;
        FieldTile* tile = GetTile(pos);
        bool blocked = player->BallModifierBlocksAttack() || run.team->traitBlockAttack();
        if (player->cAttack->range() > 1) {
            // TryLongRangeAttack (its result is ignored: movement is tried afterwards)
            if (!blocked) {
                std::vector<V2> list;
                if (player->cAttack->attackBehaviour() == AttackBehaviour::AdjacentTile)
                    list = player->cAttack->pierce() ? GetPierceAttackTiles(player->gridPos, dir, player->cAttack->range())
                                                     : GetChargeTiles(player->gridPos, dir, player->cAttack->range());
                else if (player->cAttack->attackBehaviour() == AttackBehaviour::Charge)
                    list = GetChargeTiles(player->gridPos, dir, player->cAttack->range());
                for (V2 p : list) {
                    FieldTile* t = GetTile(p);
                    if (!attackTileAllowed(t)) continue;
                    int distance = player->cAttack->attackBehaviour() == AttackBehaviour::Charge
                                       ? RoundToInt(Distance(player->gridPos, t->gridPos)) : 0;
                    set(p, pf.configAttack, dir, distance);
                }
            }
        } else if (!blocked && attackTileAllowed(tile)) {
            set(pos, pf.configAttack, dir, 0); // TryShortRangeAttack
            continue;
        }
        // TryMovement
        if (!tile->isOccupiedByActor() && !has(pos) && !player->BallModifierBlocksMove()) set(pos, pf.configStep, dir, 0);
    }
    // Buttons: Pass, Take, Shoot, Rest, Undo (CommandObjectButton.RefreshOnShow)
    bool takePossible = false;
    if (!player->isHoldingBall() && ball->isHeldBySomeone() &&
        (ball->carrier->isPlayer() || player->humanPlayer->HasPerk(Perk::BallSnatcher)) &&
        Distance(player->gridPos, ball->GetBallPosition()) <= 1.0f)
        takePossible = true;
    auto button = [&](const PlayerActionConfig* c, V2 dir) {
        Command cmd = MakeCommand(c, player, dir, 0, false, {});
        bool ok = true;
        if (c->lockWithoutBall && !player->isHoldingBall()) ok = false;
        if (c->lockWithoutAnotherPlayer && GetNumberOfLivePlayers() < 2) ok = false;
        if (c->lockIfBallOutOfReach && !takePossible) ok = false;
        if (c->lockIfUndoInvalid && !undoValid_) ok = false;
        if (c->action == ActorAction::Player_Pass && player->BallModifierBlocksPass()) ok = false;
        if (c->action == ActorAction::Player_Shoot && player->BallModifierBlocksShoot()) ok = false;
        cmd.interactable = ok;
        out.push_back(cmd);
    };
    if (player->isHoldingBall() || !takePossible) button(pf.configPass, kNone);
    if (takePossible) button(pf.configTake, kNone);
    button(pf.configThrow, kRight);
    button(pf.configWait, kNone);
    if (pf.configUndo) button(pf.configUndo, kNone);
    return out;
}

// ---------------------------------------------------------------------------------------------
// StageScore / Gold
// ---------------------------------------------------------------------------------------------
int Game::ParBonusRankTarget() const {
    const ScoringConfig& sc = data.scoring;
    FieldSize f = run.CurrentStage().fieldSize;
    return f == FieldSize::Small ? sc.parBonusRankTargetSmall : f == FieldSize::Medium ? sc.parBonusRankTargetMedium : sc.parBonusRankTargetLarge;
}

void Game::CalculateStageScore() {
    const ScoringConfig& sc = data.scoring;
    const TeamConfig* team = run.team;
    const Stage& stage = run.CurrentStage();
    bool complete = anyPlayerPassedFinishLine();
    bool touchdown = BallReachedOtherSide() && ball->isHeldByPlayer();
    int parDiff = complete ? ParDifference() : 0;
    int parTarget = stage.fieldSize == FieldSize::Small ? sc.parBonusRankTargetSmall
                  : stage.fieldSize == FieldSize::Medium ? sc.parBonusRankTargetMedium
                                                         : sc.parBonusRankTargetLarge;
    bool speedTarget = parDiff <= parTarget;
    int gemCount = 0;
    for (const FieldSizeData& fsd : data.session.fieldSizeData)
        if (fsd.fieldSize == stage.fieldSize) gemCount = fsd.GetGemCount(run.session.seasonalEvent);
    bool gemTarget = gems >= gemCount;
    int scoreComplete = 0, scoreTouchdown = 0, scoreWithinPar = 0, scoreParBonus = 0, scoreTreasure = 0;
    if (complete) {
        scoreComplete = sc.pointsStageComplete;
        scoreTouchdown = touchdown ? sc.pointsTouchdown : 0;
        scoreTouchdown = RoundToInt(static_cast<float>(scoreTouchdown) * team->traitMultiplyScoreBonusTouchdown());
        scoreWithinPar = parDiff <= 0 ? sc.pointsWithinPar : 0;
        scoreWithinPar = RoundToInt(static_cast<float>(scoreWithinPar) * team->traitMultiplyScoreBonusPar());
        scoreParBonus = parDiff > 0 ? 0 : parDiff * sc.pointsPerPar * -1;
        int treasure = 0;
        for (Treasure* t : stageTreasures) treasure += t->GetScoreValue();
        scoreTreasure = RoundToInt(static_cast<float>(treasure) * team->traitMultiplyScoreBonusTreasure());
    }
    int before = scoreComplete + scoreTouchdown + scoreWithinPar + scoreParBonus + scoreTreasure;
    // Score.GetBallModMultiplier(currentEffect, 1)
    float ballMult = 1.0f;
    if (run.ballEffect == DifficultyEffect::Easier)
        ballMult = 1.0f - data.session.ballMultiplierPerStageEasy * static_cast<float>(run.easyBallModsUsed + 1);
    else if (run.ballEffect == DifficultyEffect::Harder)
        ballMult = 1.0f + data.session.ballMultiplierPerStageHard * static_cast<float>(run.hardBallModsUsed + 1);
    int score = RoundToInt(static_cast<float>(before) * ballMult);
    score = RoundToInt(static_cast<float>(score) * team->traitMultiplyScore());
    int rank = static_cast<int>(Rank::F);
    if (complete) {
        int num = 2;
        if (touchdown) num++;
        if (parDiff <= 0) num++;
        if (num == 4 && speedTarget && gemTarget && deathsThisStage == 0) num++;
        rank = num;
    }
    results_ = StageResults{complete, touchdown, parDiff, speedTarget, gemTarget, static_cast<int>(run.ballEffect), rank, score};
    int overall = run.currentStageNumberAsOverallStage();
    if (overall < 12) run.stageResults[overall] = results_;
    run.stagesPlayed = overall + 1;
    // Gold.CalculateGoldBasedOnScore
    int g1 = RoundToInt(static_cast<float>(before) * sc.goldScoreBonusMultiplier) + overall * sc.goldBonusPerStage;
    float f = static_cast<float>(stage.enemyBudget) * sc.goldDifficultyBonusMultiplier;
    int g2 = g1 + RoundToInt(f);
    goldEarned_ = RoundToInt(static_cast<float>(g2) * team->traitMultiplyGold());
    run.AddGold(goldEarned_);
}

} // namespace dd::sim
