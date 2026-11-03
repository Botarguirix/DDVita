// Enemy decision making: EnemyIntention, IntentionMarker / IntentionMarkerEnemy (the parts that change
// game state: target tiles marked as "desired" and cancelled intentions) and EnemyActionPlanner.
#include <algorithm>
#include <limits>

#include "sim/sim.h"

namespace dd::sim {

namespace {
bool Contains(const std::vector<V2>& v, V2 x) { return std::find(v.begin(), v.end(), x) != v.end(); }
const EnemyAction kWait{ActorAction::Char_Wait, kNone};
EnemyAction Step(V2 d) { return {ActorAction::Char_Step, d}; }
EnemyAction AttackTo(V2 d) { return {ActorAction::Char_Attack, d}; }
} // namespace

// ---------------------------------------------------------------------------------------------
// EnemyIntention / IntentionMarker
// ---------------------------------------------------------------------------------------------
void Enemy::IntentionClear() {
    hasIntention = false;
    MarkerClear(true);
}

void Enemy::MarkerClear(bool alsoClearDesiredTiles) {
    if (!alsoClearDesiredTiles) return;
    for (V2 t : targetTiles) g->SetTileUndesiredByEnemy(t, this);
    targetTiles.clear();
}

void Enemy::TargetTilesInDirection(V2 direction, int distance, bool stopAtFirstOccupiedTile) {
    targetTiles.clear();
    for (int i = 1; i <= distance; i++) {
        V2 p = gridPos + direction * i;
        if (!g->PositionIsWithinField(p)) break;
        targetTiles.push_back(p);
        if (!g->GetTile(p)->isVacant() && stopAtFirstOccupiedTile) break;
    }
}

bool Enemy::StepWillResultInAttack() const {
    if (BallModifierBlocksAttack()) return false;
    for (V2 t : targetTiles)
        if (!g->GetTile(t)->isVacant()) return true;
    return false;
}

// IntentionMarkerEnemy.Show -> IntentionMarker.Show (fullDetails previews are UI only).
bool Enemy::MarkerShow(ActorAction action, V2 direction, bool) {
    auto cancel = [&] { IntentionSet(EnemyAction{ActorAction::Char_Wait, kNone}); };
    bool result = [&]() -> bool {
        if (stunned()) return false;
        if (action == ActorAction::Char_Step) {
            TargetTilesInDirection(direction, moveRange(), true);
            if (StepWillResultInAttack() && cAttack->pierce()) TargetTilesInDirection(direction, cAttack->range(), false);
            return true;
        }
        if (action != ActorAction::Char_Attack) {
            MarkerClear(false);
            return false;
        }
        if (BallModifierBlocksAttack()) {
            cancel();
            return false;
        }
        switch (cAttack->attackBehaviour()) {
        case AttackBehaviour::AdjacentTile: TargetTilesInDirection(direction, cAttack->range(), !cAttack->pierce()); break;
        case AttackBehaviour::Charge: targetTiles = g->GetChargeTiles(gridPos, direction, cAttack->range()); break;
        case AttackBehaviour::SpawnAdjacent: TargetTilesInDirection(direction, 1, true); break;
        case AttackBehaviour::SpawnNearFurthestPlayer:
        case AttackBehaviour::LightningBolt:
            targetTiles.clear();
            targetTiles.push_back(direction);
            break;
        default: break;
        }
        if (targetTiles.empty()) {
            cancel();
            return false;
        }
        if (!g->PositionIsWithinField(targetTiles.back())) {
            cancel();
            return false;
        }
        return true;
    }();
    if (!targetTiles.empty())
        for (V2 t : targetTiles) g->SetTileDesired(t, this);
    return result;
}

void Enemy::ShowEnemyIntention(bool fullDetails) {
    if (!hasIntention) return;
    EnemyAction a = nextAction;
    MarkerShow(a.action, a.direction, fullDetails);
}

void Enemy::RefreshIntentionMarker() {
    MarkerClear(true);
    ShowEnemyIntention(false);
}

// ---------------------------------------------------------------------------------------------
// EnemyActionPlanner
// ---------------------------------------------------------------------------------------------
void Enemy::DecideNextAction() {
    EnemyAction a = GetNextAction();
    IntentionSet(a);
    ShowEnemyIntention(false);
    g->Emit(Ev::Intention, this, gridPos, nextAction.direction, static_cast<int>(nextAction.action));
}

EnemyAction Enemy::GetNextAction() {
    checkedMovementsThisTurn = false;
    CachePossibleMovements();
    possibleAttacks.clear();
    possibleAttacksNull = false;
    if (stunned()) return kWait;
    EnemyMoveBehaviour mb = moveBehaviour();
    bool chase = mb == EnemyMoveBehaviour::ChaseBall || mb == EnemyMoveBehaviour::ChasePlayers || mb == EnemyMoveBehaviour::BlockBall;
    if (chase) chaseTargetPos = GetChaseTargetPosition();
    if (currentFieldTile()->containsHazard() && !possibleMovements.empty()) return GetBestPossibleMovement(true);
    possibleAttacksNull = !cAttack->GetPossibleEnemyAttacks(false, possibleAttacks);
    if (!possibleAttacksNull && !possibleAttacks.empty()) return GetBestPossibleAttack();
    if (mb == EnemyMoveBehaviour::Static) return kWait;
    if (!possibleMovements.empty()) return GetBestPossibleMovement(false);
    possibleAttacksNull = !cAttack->GetPossibleEnemyAttacks(true, possibleAttacks);
    if (!possibleAttacksNull && !possibleAttacks.empty()) return GetBestPossibleAttack();
    return kWait;
}

void Enemy::CachePossibleMovements() {
    if (checkedMovementsThisTurn) return;
    checkedMovementsThisTurn = true;
    possibleMovements.clear();
    if (moveBehaviour() == EnemyMoveBehaviour::Static || BallModifierBlocksMove()) return;
    if (moveRange() > 1) {
        for (V2 d : kCardinal)
            if (LongMoveIsPossible(d)) possibleMovements.push_back(d);
    } else if (moveRange() == 1) {
        auto ok = [&](V2 p) { return g->PositionIsWithinField(p) && g->GetTile(p)->isVacantSafeAndUndesired(); };
        V2 p = gridPos + kLeft;
        if (p.x >= minimumFieldX && ok(p)) possibleMovements.push_back(kLeft);
        p = gridPos + kRight;
        if (p.x <= maximumFieldX && ok(p)) possibleMovements.push_back(kRight);
        p = gridPos + kUp;
        if (ok(p)) possibleMovements.push_back(kUp);
        p = gridPos + kDown;
        if (ok(p)) possibleMovements.push_back(kDown);
    }
}

bool Enemy::LongMoveIsPossible(V2 direction) const {
    V2 p = gridPos;
    FieldTile* tile = nullptr;
    for (int i = 1; i <= moveRange(); i++) {
        p += direction;
        if (!g->PositionIsWithinField(p)) return false;
        if (p.x < minimumFieldX) return false;
        if (p.x > maximumFieldX) return false;
        tile = g->GetTile(p);
        if (tile->containsPlayer()) return true;
        if (!tile->isVacantAndUndesiredButMayBeDangerous()) return false;
    }
    if (tile == nullptr) return false;
    return tile->isVacantSafeAndUndesired();
}

EnemyAction Enemy::GetBestPossibleAttack() {
    if (cAttack->attackBehaviour() == AttackBehaviour::LightningBolt) return AttackTo(possibleAttacks[0]);
    if (Contains(possibleAttacks, kDown)) return AttackTo(kDown);
    if (Contains(possibleAttacks, kLeft)) return AttackTo(kLeft);
    if (Contains(possibleAttacks, kRight)) return AttackTo(kRight);
    if (Contains(possibleAttacks, kUp)) return AttackTo(kUp);
    EnemyMoveBehaviour mb = moveBehaviour();
    if (mb == EnemyMoveBehaviour::ChaseBall || mb == EnemyMoveBehaviour::ChasePlayers) {
        if (chaseTargetPos.x > gridPos.x && Contains(possibleAttacks, kRight)) return AttackTo(kRight);
        if (chaseTargetPos.x < gridPos.x && Contains(possibleAttacks, kLeft)) return AttackTo(kLeft);
        if (chaseTargetPos.x == gridPos.x) {
            if (chaseTargetPos.y < gridPos.y && Contains(possibleAttacks, kDown)) return AttackTo(kDown);
            if (chaseTargetPos.y > gridPos.y && Contains(possibleAttacks, kUp)) return AttackTo(kUp);
        }
    }
    int index = SeededRandom(g->enemyRng, 0, static_cast<int>(possibleAttacks.size()));
    return AttackTo(possibleAttacks[index]);
}

EnemyAction Enemy::GetBestPossibleMovement(bool tryingToEscapeHazard) {
    EnemyMoveBehaviour mb = moveBehaviour();
    if (!isHoldingBall()) {
        for (V2 m : possibleMovements) {
            if (m == kNone) continue;
            V2 p = gridPos + m * moveRange();
            if (!g->PositionIsWithinField(p)) continue;
            FieldTile* tile = g->GetTile(p);
            if (tile->containsBall()) return Step(m);
            if (mb != EnemyMoveBehaviour::Random) continue;
            for (V2 d : kCardinal) {
                V2 q = tile->gridPos + d;
                if (g->PositionIsWithinField(q) && g->GetTile(q)->containsPlayer()) return Step(m);
            }
        }
    }
    if (config->stayBehindGoal) {
        if (Contains(possibleMovements, kRight) && !isHoldingBall()) return Step(kRight);
    } else if (gridPos.x >= g->fieldX && Contains(possibleMovements, kLeft)) {
        return Step(kLeft);
    }
    if (isHoldingBall() && gridPos.x > 0) {
        if (Contains(possibleMovements, kLeft)) return Step(kLeft);
        if (Contains(possibleMovements, kDown)) return Step(kDown);
        if (Contains(possibleMovements, kUp)) return Step(kUp);
    } else if (!tryingToEscapeHazard &&
               (mb == EnemyMoveBehaviour::ChaseBall || mb == EnemyMoveBehaviour::ChasePlayers || mb == EnemyMoveBehaviour::BlockBall)) {
        if (chaseTargetPos.x > gridPos.x && Contains(possibleMovements, kRight)) return Step(kRight);
        if (chaseTargetPos.x < gridPos.x && Contains(possibleMovements, kLeft)) return Step(kLeft);
        if (chaseTargetPos.y < gridPos.y && Contains(possibleMovements, kDown)) return Step(kDown);
        if (chaseTargetPos.y > gridPos.y && Contains(possibleMovements, kUp)) return Step(kUp);
        return kWait;
    }
    if (moveRange() > 1) {
        for (V2 m : possibleMovements)
            if (m != kNone && g->PlayerIsWithinMoveRange(this, m)) return Step(m);
    }
    int index = SeededRandom(g->enemyRng, 0, static_cast<int>(possibleMovements.size()));
    return Step(possibleMovements[index]);
}

V2 Enemy::GetChaseTargetPosition() const {
    EnemyMoveBehaviour mb = moveBehaviour();
    if (mb == EnemyMoveBehaviour::ChaseBall || mb == EnemyMoveBehaviour::BlockBall) return g->ball->GetBallPosition();
    if (mb == EnemyMoveBehaviour::ChasePlayers) {
        size_t index = 0;
        float best = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < g->players.size(); i++) {
            float d = Distance(gridPos, g->players[i]->gridPos); // Vector3.Distance of the transforms (y = 0)
            if (d < best) {
                index = i;
                best = d;
            }
        }
        return g->players[index]->gridPos;
    }
    return gridPos;
}

} // namespace dd::sim
