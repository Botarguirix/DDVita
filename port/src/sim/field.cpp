// FieldTile and FieldManager.
#include <algorithm>
#include <cstdio>

#include "sim/sim.h"

namespace dd::sim {

bool FieldTile::containsPlayer() const { return occupant && occupant->isPlayer(); }
bool FieldTile::containsEnemy() const { return occupant && occupant->isEnemy(); }
bool FieldTile::containsObstacle() const { return occupant && occupant->isObstacle(); }

bool FieldTile::containsBall() const {
    for (const Pickup* p : pickups)
        if (p->isBall()) return true;
    return false;
}

bool FieldTile::containsGem() const {
    for (const Pickup* p : pickups)
        if (p->isGem()) return true;
    return false;
}

// FieldTile.ShowIndicators: the animated danger stripes under a player (read by AnyPlayersInDanger).
bool FieldTile::ContainsPlayerInDanger() const {
    if (desiredBy.empty() && !containsHazard()) return false;
    bool danger = false;
    for (const Enemy* e : desiredBy)
        if (e->cAttack->attackBehaviour() != AttackBehaviour::SpawnAdjacent) danger = true;
    if (containsHazard() && hazard->isLightningHazard()) danger = true;
    if (containsHazard() && hazard->isSpikeTrap() && occupant) {
        if (occupant->isPlayer()) {
            if (!static_cast<const Player*>(occupant)->humanPlayer->HasPerk(Perk::SpikeImmunity)) danger = true;
        } else {
            danger = true;
        }
    }
    return danger && containsPlayer();
}

FieldTile* Game::GetTile(V2 p) {
    if (!PositionIsWithinField(p)) {
        std::fprintf(stderr, "(%d, %d) is out of bounds.\n", p.x, p.y);
        return nullptr;
    }
    return &grid[p.x * fieldY + p.y];
}

void Game::ClearTileOccupant(Actor* a, V2 p, bool ignoreIfNewOccupantExists) {
    FieldTile* t = GetTile(p);
    if (t->occupant != a && ignoreIfNewOccupantExists) return;
    t->occupant = nullptr;
}

void Game::MoveTileOccupant(Actor* a, V2 from, V2 to) {
    ClearTileOccupant(a, from, false);
    SetTileOccupant(a, to);
}

void Game::AddPickupToTile(Pickup* p, V2 pos) {
    FieldTile* t = GetTile(pos);
    if (std::find(t->pickups.begin(), t->pickups.end(), p) != t->pickups.end()) {
        std::fprintf(stderr, "Tile at (%d,%d) already contains pickup\n", pos.x, pos.y);
        return;
    }
    t->pickups.push_back(p);
}

void Game::RemovePickupFromTile(Pickup* p, V2 pos) {
    FieldTile* t = GetTile(pos);
    auto it = std::find(t->pickups.begin(), t->pickups.end(), p);
    if (it == t->pickups.end()) {
        std::fprintf(stderr, "Pickup doesn't exist at position (%d,%d)\n", pos.x, pos.y);
        return;
    }
    t->pickups.erase(it);
}

void Game::SetTileDesired(V2 p, Enemy* e) {
    if (!PositionIsWithinField(p)) {
        std::fprintf(stderr, "%s desires an invalid tile: (%d, %d)\n", e->Name().c_str(), p.x, p.y);
        return;
    }
    FieldTile* t = GetTile(p);
    if (std::find(t->desiredBy.begin(), t->desiredBy.end(), e) == t->desiredBy.end()) t->desiredBy.push_back(e);
}

void Game::SetTileUndesiredByEnemy(V2 p, Enemy* e) {
    if (!PositionIsWithinField(p)) {
        std::fprintf(stderr, "Position not within field\n");
        return;
    }
    FieldTile* t = GetTile(p);
    auto it = std::find(t->desiredBy.begin(), t->desiredBy.end(), e);
    if (it != t->desiredBy.end()) t->desiredBy.erase(it);
}

void Game::SetAllTilesToUndesired() {
    for (FieldTile& t : grid) t.desiredBy.clear();
}

// UndoManager.TriggerUndo -> FieldManager.ForceRefreshAllOccupants (FindObjectsOfType<Actor> = active actors).
void Game::ForceRefreshAllOccupants() {
    for (FieldTile& t : grid) t.occupant = nullptr;
    for (auto& e : entities) {
        if (!e->isActor()) continue;
        Actor* a = static_cast<Actor*>(e.get());
        if (a->active && a->tileOccupier()) GetTile(a->gridPos)->occupant = a;
    }
}

FieldTile* Game::GetObstacleFromStraightLine(V2 start, V2 dir, int maxDistance, bool returnPreviousTileIfOccupied) {
    V2 p = start;
    for (int i = 0; i < maxDistance; i++) {
        p += dir;
        if (!PositionIsWithinField(p)) {
            p -= dir;
            break;
        }
        if (!GetTile(p)->isVacant()) {
            if (returnPreviousTileIfOccupied) p -= dir;
            break;
        }
    }
    return GetTile(p);
}

std::vector<V2> Game::GetMoveAndAttackTiles(V2 start, V2 dir, int maxDistance) {
    std::vector<V2> list{start};
    V2 p = start;
    for (int i = 0; i < maxDistance; i++) {
        p += dir;
        if (!PositionIsWithinField(p)) break;
        list.push_back(p);
        if (GetTile(p)->isOccupiedByActor()) break;
    }
    return list;
}

std::vector<V2> Game::GetChargeTiles(V2 start, V2 dir, int maxDistance) {
    std::vector<V2> list;
    V2 p = start + dir;
    for (int i = 1; i <= maxDistance; i++) {
        if (!PositionIsWithinField(p)) break;
        list.push_back(p);
        if (!GetTile(p)->isVacant()) break;
        p += dir;
    }
    return list;
}

std::vector<V2> Game::GetPierceAttackTiles(V2 start, V2 dir, int maxDistance) {
    std::vector<V2> list;
    V2 p = start + dir;
    for (int i = 1; i <= maxDistance; i++) {
        if (!PositionIsWithinField(p)) break;
        list.push_back(p);
        p += dir;
    }
    return list;
}

bool Game::StraightLineToOpponentIsClear(V2 start, V2 dir, int maxDistance, bool byPlayers, bool byEnemies, bool byDesire) {
    V2 p = start + dir;
    for (int i = 1; i <= maxDistance; i++) {
        if (!PositionIsWithinField(p)) break;
        FieldTile* t = GetTile(p);
        if (t->containsEnemy()) return !byEnemies;
        if (t->containsPlayer()) return !byPlayers;
        if (byDesire && t->isDesiredByEnemy()) return false;
        p += dir;
    }
    return false;
}

bool Game::PlayerIsWithinMoveRange(const Character* c, V2 dir) {
    V2 p = c->gridPos + dir;
    for (int i = 0; i < c->moveRange(); i++) {
        if (!PositionIsWithinField(p)) return false;
        FieldTile* t = GetTile(p);
        if (t->containsEnemy()) return false;
        if (t->containsPlayer()) return true;
        p += dir;
    }
    return false;
}

} // namespace dd::sim
