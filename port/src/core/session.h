// Generated run data (Session / Session.Trial / Session.Trial.Stage / HumanPlayer identity).
#pragma once
#include <string>
#include <vector>

#include "calendar.h"
#include "gamedata.h"

namespace dd {

struct TileData {
    bool occupied = false;
    const EnemyConfig* enemyConfig = nullptr;
    const ObstacleConfig* obstacleConfig = nullptr;
    const HazardConfig* hazardConfig = nullptr;
    const TreasureConfig* treasureConfig = nullptr;

    void SetEnemy(const EnemyConfig* c);
    void SetObstacle(const ObstacleConfig* c);
    void SetHazard(const HazardConfig* c);
    void SetTreasure(const TreasureConfig* c);
};

struct Stage {
    FieldSize fieldSize = FieldSize::Small;
    int gridX = 0, gridY = 0;
    int par = 0;
    int enemyBudget = 0;
    std::vector<TileData> tiles; // index = x * gridY + y (matches C# TileData[x, y])

    TileData& At(int x, int y) { return tiles[x * gridY + y]; }
    const TileData& At(int x, int y) const { return tiles[x * gridY + y]; }
};

struct Trial {
    std::vector<Stage> stages;
};

// The randomly generated identity of a recruit (HumanPlayer constructor arguments).
struct HumanPlayerSeed {
    int playerNumber = 0;
    bool isMale = false;
    int voice = 0;
    std::string name;
    const PlayerClassConfig* playerClassConfig = nullptr;
    const PerkChain* perkChain = nullptr;
    int bodyIndex = 0;          // PlayerSpriteGenerator index 0
    int hairIndex = 0;          // PlayerSpriteGenerator index 2
    std::string bodyTexture;
    std::string hairTexture;
};

struct Session {
    int seed = 0;
    std::vector<Trial> trials;
    std::vector<HumanPlayerSeed> players;
    Date date;
    const TreasureConfig* treasureConfig = nullptr;
    SeasonalEvent seasonalEvent = SeasonalEvent::None;
};

} // namespace dd
