// Attack and its subclasses (AttackAdjacentTile, AttackCharge, AttackExplode, AttackSpawnAdjacent,
// AttackSpawnNearPlayer, AttackLightningBolt; PlayerAttack* are in sim.h).
#include <algorithm>

#include "sim/sim.h"

namespace dd::sim {

AttackData Attack::CreateAttackData(V2 direction) const {
    return AttackData{direction, character, attackDamage(), knockbackDistance(), stunValue(), wind(), false};
}

bool Attack::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    return false;
}

void Attack::Attack_Coroutine(V2 direction) {
    Game* g = character->g;
    if (character->BallModifierBlocksAttack() || (character->isPlayer() && g->run.team->traitBlockAttack())) return;
    cachedDirection = direction;
    OnAttackStart();
    if (!config->spawner()) {
        cachedRecipients.clear();
        CacheAttackRecipients();
        // Fast mode may skip an attack without recipients here; the resulting state is the same.
    }
    g->Emit(Ev::Attack, character, character->gridPos, direction);
    OnAttackExecuted();
    if (config->spawner() || cachedRecipients.empty()) return;
    // All hit coroutines start together: each runs until its knockback movement starts, then they
    // finish in the order their movements end (shorter knockbacks first; ties in start order).
    AttackData data = CreateAttackData(cachedDirection);
    std::vector<PendingHit> hits;
    for (Actor* r : cachedRecipients)
        if (r) hits.push_back(r->HitBegin(data));
    std::stable_sort(hits.begin(), hits.end(), [](const PendingHit& a, const PendingHit& b) { return a.moveDistance < b.moveDistance; });
    for (PendingHit& h : hits) h.actor->HitEnd(h);
}

// ---- AttackAdjacentTile ----
void AttackAdjacentTile::CacheAttackRecipients() {
    Game* g = character->g;
    for (int i = 1; i <= range(); i++) {
        V2 p = character->gridPos + cachedDirection * i;
        if (!g->PositionIsWithinField(p)) continue;
        FieldTile* t = g->GetTile(p);
        if (t->isOccupiedByActor()) {
            cachedRecipients.push_back(t->occupant);
            if (!pierce()) break;
        }
    }
}

bool AttackAdjacentTile::GetPossibleEnemyAttacks(bool targetObstacles, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    Game* g = character->g;
    for (V2 d : kCardinal) {
        FieldTile* t = g->GetObstacleFromStraightLine(character->gridPos, d, range(), false);
        if (!targetObstacles) {
            if (t->containsPlayer()) out.push_back(d);
        } else if (t->containsObstacle()) {
            out.push_back(d);
        }
    }
    return true;
}

// ---- AttackCharge ----
void AttackCharge::OnAttackStart() {
    FieldTile* t = character->g->GetObstacleFromStraightLine(character->gridPos, cachedDirection, range(), false);
    cachedEndPos = t->gridPos;
    cachedEndPosActor = t->isOccupiedByActor() ? t->occupant : nullptr;
}

void AttackCharge::CacheAttackRecipients() {
    FieldTile* t = character->g->GetTile(cachedEndPos);
    if (!t->isVacant()) cachedRecipients.push_back(t->occupant);
}

void AttackCharge::OnAttackExecuted() {
    Game* g = character->g;
    if (cachedEndPosActor && cachedEndPosActor != character) cachedEndPos = cachedEndPos - cachedDirection;
    V2 from = character->gridPos;
    g->MoveTileOccupant(character, character->gridPos, cachedEndPos);
    character->SetGridPosition(cachedEndPos);
    g->Emit(Ev::Move, character, from, cachedEndPos);
    character->OnMoveToPositionFinished();
}

bool AttackCharge::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    for (V2 d : kCardinal)
        if (character->g->StraightLineToOpponentIsClear(character->gridPos, d, range(), false, true, true)) out.push_back(d);
    return true;
}

// ---- AttackExplode ----
void AttackExplode::CacheAttackRecipients() {
    // Explosion.GetActorsHitByExplosion
    Game* g = character->g;
    cachedRecipients.clear();
    for (V2 d : kCardinal) {
        V2 p = character->gridPos + d;
        if (g->PositionIsWithinField(p) && g->GetTile(p)->occupant) cachedRecipients.push_back(g->GetTile(p)->occupant);
    }
}

bool AttackExplode::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    out.push_back(kNone);
    return true;
}

// ---- AttackSpawnAdjacent ----
void AttackSpawnAdjacent::OnAttackExecuted() { character->cSpawning->Spawn(character->gridPos + cachedDirection); }

bool AttackSpawnAdjacent::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    Enemy* e = static_cast<Enemy*>(character);
    if (e->LiveChildrenAtMaxCapacity()) return false;
    if (e->spawnOnMove()) return false;
    Game* g = character->g;
    for (V2 d : kCardinal) {
        V2 p = character->gridPos + d;
        if (g->PositionIsWithinField(p) && g->GetTile(p)->isVacantSafeAndUndesired()) out.push_back(d);
    }
    return true;
}

// ---- AttackSpawnNearPlayer ----
void AttackSpawnNearPlayer::OnAttackExecuted() { character->cSpawning->Spawn(cachedDirection); }

void AttackSpawnNearPlayer::CacheAttackRecipients() {
    cachedRecipients.push_back(character->g->GetTile(character->gridPos + cachedDirection)->occupant);
}

bool AttackSpawnNearPlayer::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    Game* g = character->g;
    auto freeTile = [&](V2 p) { return g->PositionIsWithinField(p) && g->GetTile(p)->isVacantSafeAndUndesired(); };
    bool anyFree = false;
    for (Player* p : g->players)
        for (V2 d : kCardinal)
            if (freeTile(p->gridPos + d)) anyFree = true;
    if (!anyFree) return true;
    V2 target = kNone; // GetBomberTargetTile
    bool found = false;
    for (Player* p : g->players) {
        for (V2 d : {kUp, kRight, kLeft, kDown}) {
            if (freeTile(p->gridPos + d)) {
                target = p->gridPos + d;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    out.push_back(target);
    return true;
}

// ---- AttackLightningBolt ----
void AttackLightningBolt::CacheAttackRecipients() {
    FieldTile* t = character->g->GetTile(cachedDirection);
    if (t->isOccupiedByActor()) cachedRecipients.push_back(t->occupant);
}

bool AttackLightningBolt::GetPossibleEnemyAttacks(bool, std::vector<V2>& out) {
    out.clear();
    if (character->BallModifierBlocksAttack()) return false;
    Game* g = character->g;
    if (g->players.size() == 1) {
        out.push_back(g->players[0]->gridPos);
        return true;
    }
    Player* target = nullptr;
    for (Player* p : g->players)
        if (p->gridPos.x >= g->fieldX - 3 && (p->isHoldingBall() || target == nullptr)) target = p;
    if (target) {
        out.push_back(target->gridPos);
        return true;
    }
    float best = 3.0f;
    for (Player* p : g->players) {
        float d = Distance(character->gridPos, p->gridPos);
        if (!(d > 2.0f) && (d < best || target == nullptr)) {
            target = p;
            best = d;
        }
    }
    if (target) {
        out.push_back(target->gridPos);
        return true;
    }
    if (nextPlayerTarget >= static_cast<int>(g->players.size())) nextPlayerTarget = 0;
    out.push_back(g->players[nextPlayerTarget]->gridPos);
    nextPlayerTarget++;
    return true;
}

} // namespace dd::sim
