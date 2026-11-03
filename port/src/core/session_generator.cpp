#include "session_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "mono_random.h"

namespace dd {

void TileData::SetEnemy(const EnemyConfig* c) {
    if (occupied) { std::fprintf(stderr, "Tile is already occuped.\n"); return; }
    enemyConfig = c;
    occupied = true;
}
void TileData::SetObstacle(const ObstacleConfig* c) {
    if (occupied) { std::fprintf(stderr, "Tile is already occuped.\n"); return; }
    obstacleConfig = c;
    occupied = true;
}
void TileData::SetHazard(const HazardConfig* c) {
    if (occupied) { std::fprintf(stderr, "Tile is already occuped.\n"); return; }
    hazardConfig = c;
    occupied = true;
}
void TileData::SetTreasure(const TreasureConfig* c) {
    if (occupied) { std::fprintf(stderr, "Tile is already occuped.\n"); return; }
    treasureConfig = c;
    occupied = true;
}

const TeamConfig* TeamForMode(const GameData& data, GameMode mode, int currentTeam) {
    if (mode == GameMode::DailyChallenge) return data.teams[0];
    return data.teams[currentTeam];
}

namespace {

struct Vec2i { int x, y; };

// Mathf.RoundToInt(float): Math.Round on the value, i.e. round-half-to-even.
int RoundToInt(float f) { return static_cast<int>(std::nearbyint(static_cast<double>(f))); }

template <typename T>
void RemoveFirst(std::vector<T>& v, const T& value) {
    auto it = std::find(v.begin(), v.end(), value);
    if (it != v.end()) v.erase(it);
}

class Generator {
public:
    Generator(const GameData& data, const SessionRequest& req)
        : data_(data), cfg_(data.session), req_(req), rng_(req.seed) {}

    Session Run() {
        const GameModeConfig* mode = data_.GetGameModeConfig(req_.mode);
        gameMode_ = mode;
        seasonalEvent_ = mode->isDailyChallengeMode() ? DetectSeasonalEvent(req_.date) : SeasonalEvent::None;
        treasureConfig_ = mode->isDailyChallengeMode() ? data_.seasonal[static_cast<int>(seasonalEvent_)]->treasureConfig
                                                       : cfg_.gemConfig;
        InitClassPerks();
        RefreshPerkChainPools();

        Session s;
        s.seed = req_.seed;
        s.players = GeneratePlayers();
        for (int i = 0; i < 4; i++) s.trials.push_back(GenerateTrial(i));
        s.date = req_.date;
        s.treasureConfig = treasureConfig_;
        s.seasonalEvent = seasonalEvent_;
        return s;
    }

private:
    // PlayerClasses.Initialise: per-class perk chains, in SessionConfig.perkChains order.
    void InitClassPerks() {
        for (const PerkChain* p : cfg_.perkChains) {
            if (p->allowBrawlers) brawlerPerks_.push_back(p);
            if (p->allowCarriers) carrierPerks_.push_back(p);
            if (p->allowRunners) runnerPerks_.push_back(p);
        }
    }

    std::vector<const PerkChain*> RefreshPerkChainPool(const std::vector<const PerkChain*>& classPerks) {
        if (classPerks.empty()) std::fprintf(stderr, "PlayerClassPerks array is empty.\n");
        std::vector<const PerkChain*> pool;
        for (const PerkChain* p : classPerks) {
            if (req_.buildVersion.IsOlderThan(p->minimumVersion)) continue;
            if (!req_.team->PerkIsBlocked(p)) pool.push_back(p);
        }
        return pool;
    }

    void RefreshPerkChainPools() {
        poolBrawlers_ = RefreshPerkChainPool(brawlerPerks_);
        poolCarriers_ = RefreshPerkChainPool(carrierPerks_);
        poolRunners_ = RefreshPerkChainPool(runnerPerks_);
    }

    std::vector<const PerkChain*>& PoolFor(PlayerClass c) {
        switch (c) {
        case PlayerClass::Brawler: return poolBrawlers_;
        case PlayerClass::Carrier: return poolCarriers_;
        default: return poolRunners_;
        }
    }

    // PerkManager.GetRandomPerkChainFromPool
    const PerkChain* GetRandomPerkChainFromPool(PlayerClass c) {
        if (PoolFor(c).empty()) RefreshPerkChainPools();
        std::vector<const PerkChain*>& pool = PoolFor(c);
        int index = SeededRandom(rng_, 0, static_cast<int>(pool.size()));
        const PerkChain* chain = pool[index];
        RemoveFirst(poolBrawlers_, chain);
        RemoveFirst(poolCarriers_, chain);
        RemoveFirst(poolRunners_, chain);
        return chain;
    }

    // HumanPlayerGenerator.GetRandomNameOrBio. Note the original removes the *value* equal to the
    // picked position rather than the entry at that position, so names can repeat; kept as is.
    std::string GetRandomNameOrBio(const std::vector<std::string>& names, std::vector<int>& unused) {
        int num = SeededRandom(rng_, 0, static_cast<int>(unused.size()));
        int nameIndex = unused[num];
        std::string result = names[nameIndex];
        RemoveFirst(unused, num);
        return result;
    }

    static SkinTone GetSkinTone(int bodyIndex) {
        switch (bodyIndex) {
        case 0: return SkinTone::Light;
        case 1: return SkinTone::Mid;
        case 2: return SkinTone::Dark;
        default:
            std::fprintf(stderr, "Undefined SkinTone index: %d\n", bodyIndex);
            return SkinTone::Mid;
        }
    }

    std::vector<HumanPlayerSeed> GeneratePlayers() {
        const auto& maleNames = cfg_.playerMaleConfig->playerNames;
        const auto& femaleNames = cfg_.playerFemaleConfig->playerNames;
        std::vector<int> maleUnused, femaleUnused;
        for (int i = 0; i < static_cast<int>(maleNames.size()); i++) maleUnused.push_back(i);
        for (int i = 0; i < static_cast<int>(femaleNames.size()); i++) femaleUnused.push_back(i);

        // GetPlayerClassList, then GetPerkChainList: each consumes the RNG in its own pass.
        std::vector<const PlayerClassConfig*> classes;
        for (int i = 0; i < 16; i++) {
            const auto& setup = req_.team->GetPlayerSetupAtIndex(i);
            classes.push_back(setup.playerClassConfig
                                  ? setup.playerClassConfig
                                  : data_.GetPlayerClassConfig(static_cast<PlayerClass>(SeededRandom(rng_, 0, 3))));
        }
        std::vector<const PerkChain*> perks;
        for (int i = 0; i < 16; i++) {
            const auto& setup = req_.team->GetPlayerSetupAtIndex(i);
            perks.push_back(setup.playerPerk ? setup.playerPerk : GetRandomPerkChainFromPool(classes[i]->playerClass));
        }

        std::vector<HumanPlayerSeed> players;
        for (int i = 0; i < 16; i++) {
            HumanPlayerSeed p;
            p.playerNumber = i;
            p.isMale = SeededRandom(rng_, 0, 100) >= 50;
            p.voice = SeededRandom(rng_, 0, 3);
            p.name = p.isMale ? GetRandomNameOrBio(maleNames, maleUnused) : GetRandomNameOrBio(femaleNames, femaleUnused);
            p.playerClassConfig = classes[i];
            p.perkChain = perks[i];
            // PlayerSpriteGenerator.GetSpritePartIndices
            const HumanPlayerConfig* hpc = p.isMale ? cfg_.playerMaleConfig : cfg_.playerFemaleConfig;
            const auto& bodies = cfg_.playerGeneralConfig->bodyTextures;
            p.bodyIndex = SeededRandom(rng_, 0, static_cast<int>(bodies.size()));
            std::vector<std::string> hairs = hpc->HairsForSkinTone(GetSkinTone(p.bodyIndex));
            p.hairIndex = SeededRandom(rng_, 0, static_cast<int>(hairs.size()));
            p.bodyTexture = bodies[p.bodyIndex];
            p.hairTexture = p.hairIndex < static_cast<int>(hairs.size()) ? hairs[p.hairIndex] : hairs[0];
            players.push_back(p);
        }
        return players;
    }

    Trial GenerateTrial(int iTrial) {
        Trial t;
        for (int i = 0; i < 3; i++) t.stages.push_back(GenerateStage(i, iTrial));
        return t;
    }

    static std::vector<Vec2i> GetAvailableTiles(int minX, int maxX, const Stage& st) {
        std::vector<Vec2i> list;
        for (int i = minX; i <= maxX; i++)
            for (int j = 0; j < st.gridY; j++)
                if (!st.At(i, j).occupied) list.push_back({i, j});
        return list;
    }

    Vec2i Take(std::vector<Vec2i>& tiles, int index) {
        Vec2i v = tiles[index];
        tiles.erase(tiles.begin() + index);
        return v;
    }

    // GetSpecialEnemyTypesForStage. "<=" picks specialTypeCount + 1 types in the original; kept.
    std::vector<const EnemyConfig*> GetSpecialEnemyTypesForStage(const TrialData& trial, int specialTypeCount) {
        std::vector<const EnemyConfig*> list;
        if (trial.specialEnemies.empty()) return list;
        std::vector<const EnemyConfig*> remaining = trial.specialEnemies;
        while (static_cast<int>(list.size()) <= specialTypeCount && !remaining.empty()) {
            int index = SeededRandom(rng_, 0, static_cast<int>(remaining.size()));
            list.push_back(remaining[index]);
            remaining.erase(remaining.begin() + index);
        }
        return list;
    }

    Stage GenerateStage(int iStage, int iTrial) {
        const TrialData& trialData = cfg_.trials[iTrial];
        const FieldSizeData& fs = cfg_.fieldSizeData[iStage];
        int gx = fs.gridX;
        int par = fs.par;
        int enemyBudget = fs.enemyBudget;

        int obstacles = SeededRandom(rng_, fs.minWalls + fs.minHazards, fs.maxObstacles, true);
        int walls = (iStage != 0 || iTrial != 0 || gameMode_->isDailyChallengeMode())
                        ? SeededRandom(rng_, fs.minWalls, fs.maxWalls, true) : 0;
        if (walls > fs.maxObstacles) walls = fs.maxObstacles;
        int hazards = (obstacles - walls > fs.minHazards) ? SeededRandom(rng_, fs.minHazards, obstacles - walls, true)
                                                          : fs.minHazards;
        if (hazards > fs.maxHazards) hazards = fs.maxHazards;
        int gemCount = fs.GetGemCount(seasonalEvent_);
        int wanderers = SeededRandom(rng_, fs.minWanderers, fs.maxWanderers, true);
        int aggressives = SeededRandom(rng_, fs.minAggressives, fs.maxAggressives, true);
        int goalies = fs.goalies;
        int specialTypes = SeededRandom(rng_, fs.specialEnemyTypesMin, fs.specialEnemyTypesMax, true);
        if (specialTypes > static_cast<int>(trialData.specialEnemies.size()))
            specialTypes = static_cast<int>(trialData.specialEnemies.size());

        par += iTrial * cfg_.parIncreasePerTrial;
        par = RoundToInt(static_cast<float>(par) * gameMode_->turnParMultiplier);
        enemyBudget += iTrial * cfg_.enemyBudgetIncreasePerTrial;
        enemyBudget = RoundToInt(static_cast<float>(enemyBudget) * gameMode_->enemyBudgetMultiplier);
        int budget = enemyBudget;

        Stage st;
        st.fieldSize = fs.fieldSize;
        st.gridX = gx;
        st.gridY = fs.gridY;
        st.par = par;
        st.enemyBudget = enemyBudget;
        st.tiles.assign(static_cast<size_t>(gx) * fs.gridY, TileData{});

        const EnemyConfig* boss = nullptr;
        if (req_.mode == GameMode::Boss) boss = cfg_.endGameBoss;
        else if (iTrial >= 3 && iStage >= 2) boss = cfg_.endGameBoss;   // StageIsFinalStageInFinalTrial
        else if (iTrial == 1 && iStage >= 2) boss = cfg_.midGameBoss;   // StageIsFinalStageInHalfwayTrial

        std::vector<Vec2i> avail;
        if (boss) {
            avail = GetAvailableTiles(gx - cfg_.bossSpawnMaxX, gx - cfg_.bossSpawnMaxX, st);
            int index = SeededRandom(rng_, 1, static_cast<int>(avail.size()) - 1);
            Vec2i v = Take(avail, index);
            st.At(v.x, v.y).SetEnemy(boss);
            budget -= boss->budget;
        }

        avail = GetAvailableTiles(cfg_.wallSpawnMinX, gx - cfg_.wallSpawnMaxX, st);
        for (int n = 0; budget > 0 && !avail.empty() && n < walls && n < cfg_.wallConfig->maximumPerPool; n++) {
            Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
            st.At(v.x, v.y).SetObstacle(cfg_.wallConfig);
            budget -= cfg_.wallConfig->budget;
        }

        if (trialData.HasHazards()) {
            avail = GetAvailableTiles(cfg_.hazardSpawnMinX, gx - cfg_.hazardSpawnMaxX, st);
            const HazardConfig* hazard = trialData.hazard;
            for (int n = 0; budget > 0 && !avail.empty() && n < hazards && n < hazard->maximumPerPool; n++) {
                Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
                st.At(v.x, v.y).SetHazard(hazard);
                budget -= hazard->budget;
            }
        }

        avail = GetAvailableTiles(gx - 1, gx - 1, st);
        const EnemyConfig* goalie = trialData.goalieOverride ? trialData.goalieOverride : gameMode_->basicGoalie;
        for (int k = 0; k < goalies; k++) {
            Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
            st.At(v.x, v.y).SetEnemy(goalie);
        }

        avail = GetAvailableTiles(cfg_.enemySpawnMinX, gx - cfg_.enemySpawnMaxX, st);
        std::vector<const EnemyConfig*> pool;
        if (trialData.HasSpecialEnemies()) {
            for (const EnemyConfig* e : GetSpecialEnemyTypesForStage(trialData, specialTypes)) {
                int count = SeededRandom(rng_, e->minimumPerPool, e->maximumPerPool, true);
                for (int m = 0; m < count; m++) pool.push_back(e);
            }
        }
        for (int n = 0; n < aggressives; n++) pool.push_back(gameMode_->basicAggressive);
        for (int n = 0; n < wanderers; n++) pool.push_back(gameMode_->basicWanderer);
        while (budget > 0 && !avail.empty()) {
            Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
            const EnemyConfig* e;
            if (!pool.empty()) {
                int index = SeededRandom(rng_, 0, static_cast<int>(pool.size()));
                e = pool[index];
                pool.erase(pool.begin() + index);
            } else {
                e = trialData.spareEnemy;
            }
            budget -= e->budget;
            st.At(v.x, v.y).SetEnemy(e);
        }

        int treasures = 0;
        if (seasonalEvent_ != SeasonalEvent::Halloween) {
            avail = GetAvailableTiles(gx - 1, gx - 1, st);
            Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
            st.At(v.x, v.y).SetTreasure(treasureConfig_);
            treasures = 1;
        }
        avail = GetAvailableTiles(cfg_.treasureSpawnMinX, gx - cfg_.treasureSpawnMaxX, st);
        while (!avail.empty() && treasures < gemCount) {
            Vec2i v = Take(avail, SeededRandom(rng_, 0, static_cast<int>(avail.size())));
            st.At(v.x, v.y).SetTreasure(treasureConfig_);
            treasures++;
        }
        return st;
    }

    const GameData& data_;
    const SessionConfig& cfg_;
    const SessionRequest& req_;
    MonoRandom rng_;
    const GameModeConfig* gameMode_ = nullptr;
    SeasonalEvent seasonalEvent_ = SeasonalEvent::None;
    const TreasureConfig* treasureConfig_ = nullptr;
    std::vector<const PerkChain*> brawlerPerks_, carrierPerks_, runnerPerks_;
    std::vector<const PerkChain*> poolBrawlers_, poolCarriers_, poolRunners_;
};

} // namespace

Session GenerateSession(const GameData& data, const SessionRequest& request) {
    return Generator(data, request).Run();
}

} // namespace dd
