#include "gamedata.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace dd {

const char* ToString(PlayerClass c) {
    switch (c) {
    case PlayerClass::Carrier: return "Carrier";
    case PlayerClass::Runner: return "Runner";
    case PlayerClass::Brawler: return "Brawler";
    default: return "Count";
    }
}
const char* ToString(GameMode m) {
    switch (m) {
    case GameMode::Normal: return "Normal";
    case GameMode::DailyChallenge: return "DailyChallenge";
    case GameMode::Boss: return "Boss";
    case GameMode::Action: return "Action";
    default: return "NOT_SET";
    }
}
const char* ToString(FieldSize s) {
    switch (s) {
    case FieldSize::Small: return "Small";
    case FieldSize::Medium: return "Medium";
    default: return "Large";
    }
}
const char* ToString(SeasonalEvent e) {
    switch (e) {
    case SeasonalEvent::Halloween: return "Halloween";
    case SeasonalEvent::Christmas: return "Christmas";
    case SeasonalEvent::Easter: return "Easter";
    default: return "None";
    }
}

const char* ToString(ActorAction a) {
    switch (a) {
    case ActorAction::Char_Attack: return "Attack";
    case ActorAction::Char_Step: return "Step";
    case ActorAction::Char_Wait: return "Wait";
    case ActorAction::Player_Shoot: return "Shoot";
    case ActorAction::Player_Pass: return "Pass";
    case ActorAction::Player_TakeBall: return "TakeBall";
    default: return "Other";
    }
}

int TeamConfig::TraitInt(TeamTrait t) const {
    return (positiveTrait && positiveTrait->trait == t ? positiveTrait->valueInt : 0) +
           (negativeTrait && negativeTrait->trait == t ? negativeTrait->valueInt : 0);
}

float TeamConfig::TraitMultiplier(TeamTrait t) const {
    return (positiveTrait && positiveTrait->trait == t ? positiveTrait->valueFloat : 1.0f) *
           (negativeTrait && negativeTrait->trait == t ? negativeTrait->valueFloat : 1.0f);
}

const PerkConfig* GameData::GetPerkFromPerkChain(const PerkChain* chain, int level) const {
    if (level <= 0) return noPerkConfig;
    level--;
    if (level >= static_cast<int>(chain->perks.size())) {
        std::fprintf(stderr, "Requested %s perk at level %d beyond the chain length.\n", chain->name.c_str(), level);
        level = static_cast<int>(chain->perks.size()) - 1;
    }
    return chain->perks[level];
}

// BuildVersion.IsOlderThan, including its quirks (it is not a plain lexicographic compare).
bool BuildVersion::IsOlderThan(const BuildVersion& v) const {
    if (major < v.major) return true;
    if (minor < v.minor) return major <= v.major;
    if (patch < v.patch) return major <= v.major && minor <= v.minor;
    return false;
}

std::vector<std::string> HumanPlayerConfig::HairsForSkinTone(SkinTone tone) const {
    std::vector<std::string> out;
    for (const auto& h : hairTextures) {
        bool use = tone == SkinTone::Dark ? h.useWithDarkSkin : tone == SkinTone::Light ? h.useWithLightSkin : h.useWithMidSkin;
        if (!use) continue;
        if (h.instancesInArray == 0) {
            std::fprintf(stderr, "InstancesInArray is 0 for hair texture: %s\n", h.texture.c_str());
            continue;
        }
        for (int j = 0; j < h.instancesInArray; j++) out.push_back(h.texture);
    }
    return out;
}

bool TeamConfig::PerkIsBlocked(const PerkChain* perk) const {
    for (const PerkChain* p : blockedPerks) {
        if (p == nullptr) {
            std::fprintf(stderr, "Null blocked perk on TeamConfig: %s\n", name.c_str());
        } else if (p == perk) {
            return true;
        }
    }
    return false;
}

const PlayerClassConfig* GameData::GetPlayerClassConfig(PlayerClass c) const {
    switch (c) {
    case PlayerClass::Carrier: return classCarrier;
    case PlayerClass::Runner: return classRunner;
    case PlayerClass::Brawler: return classBrawler;
    default: return nullptr;
    }
}

const GameModeConfig* GameData::GetGameModeConfig(GameMode m) const {
    switch (m) {
    case GameMode::DailyChallenge: return modeDaily;
    case GameMode::Boss: return modeBoss;
    case GameMode::Action: return modeAction;
    default: return modeNormal;
    }
}

namespace {

std::string RefId(const json& v) {
    if (v.is_object() && v.contains("$ref")) return v["$ref"].get<std::string>();
    return {};
}

std::string AssetName(const json& v) {
    if (v.is_object() && v.contains("$asset")) {
        std::string s = v["$asset"].get<std::string>();
        auto colon = s.find(':');
        return colon == std::string::npos ? s : s.substr(colon + 1);
    }
    return {};
}

int I(const json& o, const char* k, int def = 0) { return o.contains(k) && o[k].is_number() ? o[k].get<int>() : def; }
bool B(const json& o, const char* k, bool def = false) { return o.contains(k) && o[k].is_number() ? o[k].get<int>() != 0 : def; }
float F(const json& o, const char* k, float def = 0.0f) {
    return o.contains(k) && o[k].is_number() ? static_cast<float>(o[k].get<double>()) : def;
}
std::string S(const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }

BuildVersion Version(const json& v) {
    BuildVersion b;
    if (v.is_object()) { b.major = I(v, "major"); b.minor = I(v, "minor"); b.patch = I(v, "patch"); }
    return b;
}

template <typename T>
const T* Lookup(const std::map<std::string, std::unique_ptr<T>>& store, const json& ref) {
    std::string id = RefId(ref);
    if (id.empty()) return nullptr;
    auto it = store.find(id);
    return it == store.end() ? nullptr : it->second.get();
}

template <typename T>
const T* LookupOptional(const std::map<std::string, std::unique_ptr<T>>& store, const json& opt) {
    if (!opt.is_object() || !B(opt, "enabled")) return nullptr;
    return Lookup(store, opt["value"]);
}

void FillActor(ActorConfig& a, const std::string& id, const json& o) {
    a.id = id;
    a.name = S(o, "_name");
    a.actorName = S(o, "actorName");
    a.budget = I(o, "budget", 5);
    a.minimumPerPool = I(o, "minimumPerPool", 1);
    a.maximumPerPool = I(o, "maximumPerPool", 4);
}

} // namespace

bool GameData::Load(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    json root;
    try {
        root = json::parse(f);
    } catch (const std::exception& e) {
        if (error) *error = std::string("JSON parse error: ") + e.what();
        return false;
    }
    const json& objects = root["objects"];
    const json& singletons = root["singletons"];

    // Pass 1: create every object so cross references can be resolved regardless of order.
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const std::string& id = it.key();
        const std::string cls = (*it)["_class"].get<std::string>();
        if (cls == "AttackConfig") attacks_[id] = std::make_unique<AttackConfig>();
        else if (cls == "PerkConfig") perkConfigs_[id] = std::make_unique<PerkConfig>();
        else if (cls == "PlayerActionConfig") actionConfigs_[id] = std::make_unique<PlayerActionConfig>();
        else if (cls == "BallModifierConfig") ballMods_[id] = std::make_unique<BallModifierConfig>();
        else if (cls == "TeamTraitConfig") traits_[id] = std::make_unique<TeamTraitConfig>();
        else if (cls == "EnemyConfig") enemies_[id] = std::make_unique<EnemyConfig>();
        else if (cls == "ObstacleConfig") obstacles_[id] = std::make_unique<ObstacleConfig>();
        else if (cls == "HazardConfig") hazards_[id] = std::make_unique<HazardConfig>();
        else if (cls == "TreasureConfig") treasures_[id] = std::make_unique<TreasureConfig>();
        else if (cls == "PerkChain") perkChains_[id] = std::make_unique<PerkChain>();
        else if (cls == "PlayerClassConfig") playerClasses_[id] = std::make_unique<PlayerClassConfig>();
        else if (cls == "HumanPlayerConfig") humanPlayers_[id] = std::make_unique<HumanPlayerConfig>();
        else if (cls == "HumanPlayerGeneralConfig") humanGenerals_[id] = std::make_unique<HumanPlayerGeneralConfig>();
        else if (cls == "GameModeConfig") gameModes_[id] = std::make_unique<GameModeConfig>();
        else if (cls == "TeamConfig") teams_[id] = std::make_unique<TeamConfig>();
        else if (cls == "SeasonalEventConfig") seasonalConfigs_[id] = std::make_unique<SeasonalEventConfig>();
    }

    auto single = [&](const char* name) -> const json* {
        if (!singletons.contains(name)) return nullptr;
        return &objects[singletons[name].get<std::string>()];
    };
    auto singleId = [&](const char* name) -> std::string {
        return singletons.contains(name) ? singletons[name].get<std::string>() : std::string();
    };

    // Pass 2: fill fields.
    for (auto& [id, p] : attacks_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->attackBehaviour = static_cast<AttackBehaviour>(I(o, "attackBehaviour"));
        p->attackDamage = I(o, "attackDamage");
        if (o.contains("range") && o["range"].is_object()) {
            p->rangeEnabled = B(o["range"], "enabled");
            p->range = I(o["range"], "value");
        }
        p->stunDuration = I(o, "stunDuration");
        p->knockbackDistance = I(o, "knockbackDistance", 1);
        p->explodeOnDeath = B(o, "explodeOnDeath");
        p->spawnOnMove = B(o, "spawnOnMove");
        p->pierce = B(o, "pierce");
        p->wind = B(o, "wind");
        p->enemyToSpawn = Lookup(enemies_, o.value("enemyToSpawn", json()));
        p->maxLiveChildren = I(o, "maxLiveChildren");
        p->additionalStaminaCost = I(o, "additionalStaminaCost");
    }
    for (auto& [id, p] : perkConfigs_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->perkAdditionalCost = I(o, "perkAdditionalCost");
        p->perkStatValue = I(o, "perkStatValue");
    }
    for (auto& [id, p] : actionConfigs_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->action = static_cast<ActorAction>(I(o, "action"));
        p->actionPoints = I(o, "actionPoints", 1);
        p->noStaminaCost = B(o, "noStaminaCost");
        p->staminaCost = I(o, "staminaCost", 1);
        p->switchControl = B(o, "switchControl");
        p->undo = B(o, "undo");
        p->lockWithoutBall = B(o, "lockWithoutBall");
        p->lockWithoutAnotherPlayer = B(o, "lockWithoutAnotherPlayer");
        p->lockIfBallOutOfReach = B(o, "lockIfBallOutOfReach");
        p->lockIfUndoInvalid = B(o, "lockIfUndoInvalid");
    }
    for (auto& [id, p] : ballMods_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->friendlyName = S(o, "friendlyName");
        p->description = S(o, "description");
        p->ballSprite = AssetName(o.value("ballSprite", json()));
        p->modifier = static_cast<BallModifier>(I(o, "modifier"));
        p->difficultyEffect = static_cast<DifficultyEffect>(I(o, "difficultyEffect"));
        p->value = I(o, "value");
    }
    for (auto& [id, p] : traits_) {
        const json& o = objects[id];
        p->trait = static_cast<TeamTrait>(I(o, "trait"));
        p->valueInt = I(o, "valueInt");
        p->valueFloat = F(o, "valueFloat");
        p->description = S(o, "description");
    }
    for (auto& [id, p] : enemies_) {
        const json& o = objects[id];
        FillActor(*p, id, o);
        p->maximumHealth = I(o, "maximumHealth", 1);
        p->knockbackResistance = I(o, "knockbackResistance");
        p->enemyType = static_cast<EnemyType>(I(o, "enemyType"));
        p->sprite = AssetName(o.value("sprite", json()));
        p->boss = B(o, "boss");
        p->goalie = B(o, "goalie");
        p->ballCatcher = B(o, "ballCatcher");
        p->healthLossPerTurn = I(o, "healthLossPerTurn");
        p->moveBehaviour = static_cast<EnemyMoveBehaviour>(I(o, "moveBehaviour"));
        p->moveRange = I(o, "moveRange", 1);
        p->stayBehindGoal = B(o, "stayBehindGoal");
        p->attackConfig = Lookup(attacks_, o.value("attackConfig", json()));
    }
    for (auto& [id, p] : obstacles_) {
        const json& o = objects[id];
        FillActor(*p, id, o);
        p->maximumHealth = I(o, "maximumHealth", 1);
        p->knockbackResistance = I(o, "knockbackResistance");
    }
    const std::string lightningPrefab = singleId("LightningHazard");
    for (auto& [id, p] : hazards_) {
        const json& o = objects[id];
        FillActor(*p, id, o);
        p->damage = I(o, "damage");
        p->kind = RefId(o.value("prefab", json())) == lightningPrefab ? HazardKind::Lightning : HazardKind::FloorSpikes;
    }
    for (auto& [id, p] : treasures_) {
        const json& o = objects[id];
        FillActor(*p, id, o);
        p->treasureType = static_cast<TreasureType>(I(o, "treasureType", 1));
        p->scoreValue = I(o, "scoreValue");
        p->sprite = AssetName(o.value("sprite", json()));
        p->seasonalVariant = static_cast<SeasonalEvent>(I(o, "seasonalVariant"));
    }

    for (auto& [id, p] : perkChains_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->chainName = S(o, "chainName");
        p->perk = static_cast<Perk>(I(o, "perk"));
        for (const auto& r : o.value("perks", json::array())) p->perks.push_back(Lookup(perkConfigs_, r));
        p->allowCarriers = B(o, "allowCarriers");
        p->allowRunners = B(o, "allowRunners");
        p->allowBrawlers = B(o, "allowBrawlers");
        p->minimumVersion = Version(o.value("minimumVersion", json()));
    }
    for (auto& [id, p] : playerClasses_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->className = S(o, "className");
        p->playerClass = static_cast<PlayerClass>(I(o, "playerClass"));
        p->maxLevelStamina = I(o, "maxLevelStamina");
        p->maxLevelStrength = I(o, "maxLevelStrength");
        p->maxLevelActionPoints = I(o, "maxLevelActionPoints");
    }
    for (auto& [id, p] : humanPlayers_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        for (const auto& n : o["playerNames"]) p->playerNames.push_back(n.get<std::string>());
        for (const auto& h : o["hairTextures"]) {
            HumanPlayerTexture t;
            t.texture = AssetName(h["texture"]);
            t.instancesInArray = I(h, "instancesInArray", 1);
            t.useWithLightSkin = B(h, "useWithLightSkin", true);
            t.useWithDarkSkin = B(h, "useWithDarkSkin", true);
            t.useWithMidSkin = B(h, "useWithMidSkin", true);
            p->hairTextures.push_back(t);
        }
    }
    for (auto& [id, p] : humanGenerals_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        for (const auto& t : o["bodyTextures"]) p->bodyTextures.push_back(AssetName(t));
        for (const auto& b : o["bios"]) p->bios.push_back(b.get<std::string>());
    }
    for (auto& [id, p] : gameModes_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->friendlyName = S(o, "friendlyName");
        p->mode = static_cast<GameMode>(I(o, "mode"));
        p->enemyBudgetMultiplier = F(o, "enemyBudgetMultiplier", 1.0f);
        p->turnParMultiplier = F(o, "turnParMultiplier", 1.0f);
        p->stagesPerTrial = I(o, "stagesPerTrial", 3);
        p->basicWanderer = Lookup(enemies_, o["basicWanderer"]);
        p->basicAggressive = Lookup(enemies_, o["basicAggressive"]);
        p->basicGoalie = Lookup(enemies_, o["basicGoalie"]);
    }
    for (auto& [id, p] : teams_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->teamName = S(o, "teamName");
        for (const auto& s : o["players"]) {
            TeamConfig::PlayerSetup ps;
            ps.playerClassConfig = Lookup(playerClasses_, s["playerClassConfig"]);
            ps.playerPerk = Lookup(perkChains_, s["playerPerk"]);
            p->players.push_back(ps);
        }
        for (const auto& b : o["blockedPerks"]) p->blockedPerks.push_back(Lookup(perkChains_, b));
        p->positiveTrait = Lookup(traits_, o.value("positiveTrait", json()));
        p->negativeTrait = Lookup(traits_, o.value("negativeTrait", json()));
    }
    for (auto& [id, p] : seasonalConfigs_) {
        const json& o = objects[id];
        p->id = id;
        p->name = S(o, "_name");
        p->seasonalEvent = static_cast<SeasonalEvent>(I(o, "seasonalEvent"));
        p->treasureConfig = Lookup(treasures_, o["treasureConfig"]);
    }

    // Singletons.
    const json* sc = single("SessionConfig");
    if (!sc) {
        if (error) *error = "SessionConfig missing";
        return false;
    }
    {
        const json& o = *sc;
        session.enemySpawnMinX = I(o, "enemySpawnMinX");
        session.wallSpawnMinX = I(o, "wallSpawnMinX");
        session.hazardSpawnMinX = I(o, "hazardSpawnMinX");
        session.treasureSpawnMinX = I(o, "treasureSpawnMinX");
        session.enemySpawnMaxX = I(o, "enemySpawnMaxX");
        session.wallSpawnMaxX = I(o, "wallSpawnMaxX");
        session.hazardSpawnMaxX = I(o, "hazardSpawnMaxX");
        session.treasureSpawnMaxX = I(o, "treasureSpawnMaxX");
        session.bossSpawnMaxX = I(o, "bossSpawnMaxX");
        session.parIncreasePerTrial = I(o, "parIncreasePerTrial");
        session.enemyBudgetIncreasePerTrial = I(o, "enemyBudgetIncreasePerTrial");
        for (const auto& t : o["trials"]) {
            TrialData td;
            td.trialName = S(t, "trialName");
            td.spareEnemy = Lookup(enemies_, t["spareEnemy"]);
            for (const auto& e : t["specialEnemies"]) td.specialEnemies.push_back(Lookup(enemies_, e));
            td.hazard = Lookup(hazards_, t["hazard"]);
            td.goalieOverride = LookupOptional(enemies_, t["goalieOverride"]);
            session.trials.push_back(td);
        }
        for (const auto& d : o["fieldSizeData"]) {
            FieldSizeData fs;
            fs.fieldSize = static_cast<FieldSize>(I(d, "fieldSize"));
            fs.gridX = d["gridSize"]["x"].get<int>();
            fs.gridY = d["gridSize"]["y"].get<int>();
            fs.backgroundScene = S(d, "backgroundScene");
            fs.par = I(d, "par");
            fs.maxObstacles = I(d, "maxObstacles");
            fs.minWalls = I(d, "minWalls");
            fs.maxWalls = I(d, "maxWalls");
            fs.minHazards = I(d, "minHazards");
            fs.maxHazards = I(d, "maxHazards");
            fs.minWanderers = I(d, "minWanderers");
            fs.maxWanderers = I(d, "maxWanderers");
            fs.minAggressives = I(d, "minAggressives");
            fs.maxAggressives = I(d, "maxAggressives");
            fs.goalies = I(d, "goalies");
            fs.gems = I(d, "gems");
            fs.gemsDuringEasterEvent = I(d, "gemsDuringEasterEvent");
            fs.specialEnemyTypesMin = I(d, "specialEnemyTypesMin");
            fs.specialEnemyTypesMax = I(d, "specialEnemyTypesMax");
            fs.enemyBudget = I(d, "enemyBudget");
            session.fieldSizeData.push_back(fs);
        }
        session.midGameBoss = Lookup(enemies_, o["midGameBoss"]);
        session.endGameBoss = Lookup(enemies_, o["endGameBoss"]);
        session.wallConfig = Lookup(obstacles_, o["wallConfig"]);
        session.gemConfig = Lookup(treasures_, o["gemConfig"]);
        session.playerMaleConfig = Lookup(humanPlayers_, o["playerMaleConfig"]);
        session.playerFemaleConfig = Lookup(humanPlayers_, o["playerFemaleConfig"]);
        session.playerGeneralConfig = Lookup(humanGenerals_, o["playerGeneralConfig"]);
        for (const auto& c : o["perkChains"]) session.perkChains.push_back(Lookup(perkChains_, c));
        session.lightningHazardConfig = Lookup(hazards_, o.value("lightningHazardConfig", json()));
        session.ballModifierDefault = Lookup(ballMods_, o.value("ballModifierDefault", json()));
        for (const auto& r : o.value("ballModsEasier", json::array())) session.ballModsEasier.push_back(Lookup(ballMods_, r));
        for (const auto& r : o.value("ballModsHarder", json::array())) session.ballModsHarder.push_back(Lookup(ballMods_, r));
        session.ballMultiplierPerStageEasy = F(o, "ballMultiplierPerStageEasy");
        session.ballMultiplierPerStageHard = F(o, "ballMultiplierPerStageHard");
    }
    if (const json* l = single("LevelUpConfig")) {
        const json& o = *l;
        levelUp.perkCostMultiplier = I(o, "perkCostMultiplier", 1);
        levelUp.newRecruitPrice = I(o, "newRecruitPrice");
        levelUp.staminaBase = I(o, "staminaBase");
        levelUp.actionPointBase = I(o, "actionPointBase");
        levelUp.restBase = I(o, "restBase");
        levelUp.strengthBase = I(o, "strengthBase");
        levelUp.knockbackBase = I(o, "knockbackBase");
        levelUp.stunBase = I(o, "stunBase");
        levelUp.ballDamageBase = I(o, "ballDamageBase");
        levelUp.ballKnockbackBase = I(o, "ballKnockbackBase");
        levelUp.ballStunBase = I(o, "ballStunBase");
        auto ups = [&](const char* k, std::vector<LevelUpConfig::Upgrade>& out) {
            for (const auto& u : o.value(k, json::array())) out.push_back({I(u, "statBuff"), I(u, "price")});
        };
        ups("staminaUpgrades", levelUp.staminaUpgrades);
        ups("strengthUpgrades", levelUp.strengthUpgrades);
        ups("actionPointUpgrades", levelUp.actionPointUpgrades);
    }
    if (const json* s = single("ScoringConfig")) {
        const json& o = *s;
        scoring.pointsStageComplete = I(o, "pointsStageComplete");
        scoring.pointsTouchdown = I(o, "pointsTouchdown");
        scoring.pointsWithinPar = I(o, "pointsWithinPar");
        scoring.pointsPerPar = I(o, "pointsPerPar");
        scoring.finalSurvivalBonusPerStage = I(o, "finalSurvivalBonusPerStage");
        scoring.parBonusRankTargetSmall = I(o, "parBonusRankTargetSmall");
        scoring.parBonusRankTargetMedium = I(o, "parBonusRankTargetMedium");
        scoring.parBonusRankTargetLarge = I(o, "parBonusRankTargetLarge");
        scoring.goldScoreBonusMultiplier = F(o, "goldScoreBonusMultiplier");
        scoring.goldBonusPerStage = I(o, "goldBonusPerStage");
        scoring.goldDifficultyBonusMultiplier = F(o, "goldDifficultyBonusMultiplier");
    }
    if (const json* pm = single("PerkManager")) noPerkConfig = Lookup(perkConfigs_, (*pm)["noPerkConfig"]);
    auto flags = [&](const char* prefab, ActorPrefabFlags& f) {
        if (const json* p = single(prefab)) {
            f.obstructPlayer = B(*p, "obstructPlayer", true);
            f.obstructEnemy = B(*p, "obstructEnemy", true);
            f.splatBloodOnTile = B(*p, "splatBloodOnTile");
        }
    };
    flags("Player", prefabs.player);
    flags("Enemy", prefabs.enemy);
    flags("BreakableWall", prefabs.wall);
    flags("Ball", prefabs.ball);
    flags("Treasure", prefabs.treasure);
    if (const json* p = single("Player")) {
        const json& o = *p;
        prefabs.playerDefaultAttack = Lookup(attacks_, o["defaultAttack"]);
        prefabs.playerPierceAttack = Lookup(attacks_, o["pierceAttack"]);
        prefabs.playerChargeAttack = Lookup(attacks_, o["chargeAttack"]);
        prefabs.configStep = Lookup(actionConfigs_, o["configStep"]);
        prefabs.configAttack = Lookup(actionConfigs_, o["configAttack"]);
        prefabs.configWait = Lookup(actionConfigs_, o["configWait"]);
        prefabs.configThrow = Lookup(actionConfigs_, o["configThrow"]);
        prefabs.configPass = Lookup(actionConfigs_, o["configPass"]);
        prefabs.configSwitchControl = Lookup(actionConfigs_, o["configSwitchControl"]);
        prefabs.configTake = Lookup(actionConfigs_, o["configTake"]);
    }
    for (auto& [id, p] : actionConfigs_)
        if (p->undo) prefabs.configUndo = p.get();
    if (const json* b = single("Ball")) prefabs.ballAttackConfig = Lookup(attacks_, (*b)["attackConfig"]);
    if (const json* l = single("LightningHazard")) prefabs.lightningAttackConfig = Lookup(attacks_, (*l)["attackConfig"]);
    if (const json* t = single("Treasure")) prefabs.easterEnemyToSpawn = Lookup(enemies_, (*t)["easterEnemyToSpawn"]);

    if (const json* t = single("Teams")) {
        for (const auto& r : (*t)["teamConfigs"]) teams.push_back(Lookup(teams_, r));
    }
    if (const json* pc = single("PlayerClasses")) {
        classCarrier = Lookup(playerClasses_, (*pc)["configCarrier"]);
        classRunner = Lookup(playerClasses_, (*pc)["configRunner"]);
        classBrawler = Lookup(playerClasses_, (*pc)["configBrawler"]);
    }
    if (const json* a = single("AppDeathball")) {
        modeNormal = Lookup(gameModes_, (*a)["gameModeConfigNormal"]);
        modeDaily = Lookup(gameModes_, (*a)["gameModeConfigDailyChallenge"]);
        modeBoss = Lookup(gameModes_, (*a)["gameModeConfigBoss"]);
        modeAction = Lookup(gameModes_, (*a)["gameModeConfigAction"]);
    }
    if (const json* c = single("Configs")) {
        seasonal[0] = Lookup(seasonalConfigs_, (*c)["seasonalEventNone"]);
        seasonal[1] = Lookup(seasonalConfigs_, (*c)["seasonalEventHalloween"]);
        seasonal[2] = Lookup(seasonalConfigs_, (*c)["seasonalEventChristmas"]);
        seasonal[3] = Lookup(seasonalConfigs_, (*c)["seasonalEventEaster"]);
    }
    if (const json* b = single("BuildConfig")) {
        buildVersion = Version((*b)["buildVersion"]);
        platform = I(*b, "platform", 1);
    }

    std::ostringstream missing;
    if (!session.wallConfig) missing << " wallConfig";
    if (!session.playerMaleConfig || !session.playerFemaleConfig || !session.playerGeneralConfig) missing << " playerConfigs";
    if (teams.empty()) missing << " Teams";
    if (!classCarrier || !classRunner || !classBrawler) missing << " PlayerClasses";
    if (!modeNormal || !modeDaily || !modeBoss || !modeAction) missing << " GameModes";
    if (!prefabs.playerDefaultAttack || !prefabs.configStep || !prefabs.configAttack || !prefabs.configWait ||
        !prefabs.configThrow || !prefabs.configPass || !prefabs.configSwitchControl || !prefabs.configTake ||
        !prefabs.ballAttackConfig || !prefabs.lightningAttackConfig)
        missing << " Prefabs";
    if (!session.ballModifierDefault || !session.lightningHazardConfig) missing << " SessionConfig.ballMods/lightning";
    if (!noPerkConfig) missing << " PerkManager.noPerkConfig";
    if (!missing.str().empty()) {
        if (error) *error = "gamedata incomplete:" + missing.str();
        return false;
    }
    return true;
}

} // namespace dd
