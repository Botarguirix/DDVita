// The app: the flow between the game's screens (title, modes, teams, a stage and its menus, results,
// team management, How to Play, Stats & Achievements) on top of the simulation, its view and sound.
//
//   game_app.cpp      flow and input (one Input* function per screen), the entry point
//   screens*.cpp      drawing of each screen
//   meta.cpp          persistent progress: stats, achievements, Skull Keys, unlocks (meta.json)
//   settings.cpp      options (settings.json)
//   help.cpp          How to Play pages and the in-game info popups (help.json)
#pragma once
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "app/platform.h"
#include "app/save.h"
#include "audio/sound.h"
#include "core/calendar.h"
#include "core/gamedata.h"
#include "render/assets.h"
#include "render/gfx.h"
#include "render/stage_view.h"
#include "sim/sim.h"
#include "ui/ui.h"

namespace dd::app {

// ---------------------------------------------------------------------------------------------
// Options (AppSettingsDeathball)
// ---------------------------------------------------------------------------------------------
struct Settings {
    float master = 0.7f, music = 0.7f, jingles = 0.7f, sounds = 0.7f, voices = 1.0f;
    bool suggestUndo = true;
    bool autoSkipCinematics = false; // AppSettingsDeathball.pref_autoSkipCinematics
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    void Apply(audio::Mixer& mixer) const;
};

// ---------------------------------------------------------------------------------------------
// How to Play (help.json: HowToPlayPageConfig / HowToPlayItemConfig, TileInfoConfig, popups)
// ---------------------------------------------------------------------------------------------
struct HelpImage {
    std::string sprite;
    float x = 0, y = 0;
    int control = 0;     // controllerFunction: a button prompt instead of the sprite (0 = none)
    bool player = false; // generatePlayerSprite: a generated player
};
// texts are loc keys (data/loc) with the config's English text as the fallback
struct HelpItem {
    std::string text, textFallback, sub, color = "#ffffff", subColor = "#cccccc";
    bool hasSub = false, inlineSub = false;
    std::vector<HelpImage> images;
};
struct HelpPage {
    std::string name, header, headerFallback;
    std::vector<HelpItem> items;
};
struct TileInfo {
    std::string header, headerFallback, text, textFallback, color;
    bool isBall = false, isHazard = false;
};
struct Help {
    std::vector<HelpPage> pages;
    std::map<std::string, TileInfo> tiles;
    std::map<std::string, std::map<std::string, std::string>> popups; // popup -> field -> text
    bool Load(const std::string& path);
    const std::string& Popup(const std::string& popup, const std::string& field, const std::string& fallback) const;
};

// ---------------------------------------------------------------------------------------------
// Persistent progress
// ---------------------------------------------------------------------------------------------
// Same order as the original's Achievement enum (achievements.json ids).
enum class Achievement : int {
    NONE, WinAStage, CompleteTrial1, CompleteTrial2, CompleteTrial3, CompleteGame, CompleteBossMode, CompleteActionMode,
    CompleteDailyChallenge, StatDailyChallenges, KillAnEnemy, EnemyHitOtherEnemy, EnemyHitByTwoEnemies, StatEnemyKillEnemies,
    EnemyBreakRock, StopHatcherySpawning, DodgeCharger, KappaTwoKills, KappaHitKappa, KillGoalie, KillAllGoaliesInTrial,
    KillAllGoaliesInGame, MawKillOtherEnemy, PushMaw, KillBrutolith, KillFinalBoss, LongShotThenTouchdown, SurviveSurrounded,
    KillAllEnemiesThenWin, WinWithoutKilling, WinWithoutHitting, KillDinoWithTrap, KillMultipleWithSpikes,
    DealFiveDamageWithBall, ReclaimBallThenTouchdown, TrapEnemyWithWalls, KillThenRebound, KillMultipleWithOneAttack,
    MoveManyTilesInOneTurn, KillGoalieFromOppositeEnd, PassBallEveryTurn, CompleteGameNoDeaths, RankS, RankSConsecutively,
    RankSEveryStage, FinalStageOnePlayer, HardBallsOnly, EveryStageTouchdown, EveryStageWithinPar, EveryStageSpeed,
    EveryStageGems, FinalScore, PerkLevel1, PerkLevel2, PerkLevel3, BothPerksLevel3, MaxOutPlayer, RecruitPlayer,
    ReplaceFullTeam, NoStaminaUpgrades, NoStrengthUpgrades, NoUpgrades, UnlockTeam, CompleteWithUnlockedTeam,
    CompleteWithEveryTeam, StatPass, StatRest, StatKills, StatPushRocks, StatBlockSpawns, StatGold, StatGems, StatRankA,
    StatMove1, StatMove2, StatEscapees, StatTouchdowns, StatStageWins, StatGameWins, GlobalStatEscapees,
    CompleteSeasonalEvent, WinStageDuringEnemyTurn, PassOppositeCorners, FullLightningWin, LotsOfBlood, FinalScore2, Count
};

// GlobalStatTracker
enum class Stat : int {
    Passes, Rests, Kills, RocksPushed, SpawnsBlocked, GoldAcquired, GemsAcquired, StageWinsRankA, TilesMoved, Escapees,
    Deaths, StageWins, GameWins, FriendlyFire, StageWinsTouchdowns, StageWinsWithinPar, DailyChallenges, TeamsCompleted,
    GamesStarted, StagesRestarted, Count
};

struct AchievementDef {
    Achievement id = Achievement::NONE;
    std::string name, nameFallback, desc, descFallback; // loc keys and English texts
    std::string icon, iconLocked;
    bool hidden = false;
    int targetValue = 0;
    bool progress = false; // statType Progress (a counter shown as n / target)
};

struct TeamProgress {
    bool unlocked = false;
    int stagesCompleted = 0, keysAcquired = 0;
    int best[4] = {0, 0, 0, 0}; // best score per GameMode (Daily: Score.highScoreDailyChallenge)
};

class Meta : public sim::MetaSink {
public:
    explicit Meta(const GameData& data);

    bool LoadDefs(const std::string& achievementsJson);
    bool Load(const std::string& path);
    bool Save(const std::string& path);

    std::vector<AchievementDef> defs; // AchievementManagerDeathball.achievements order
    std::string lockedIcon;
    bool unlocked[static_cast<int>(Achievement::Count)] = {};
    long long stats[static_cast<int>(Stat::Count)] = {};
    std::vector<TeamProgress> teams;
    int keys = 0, unreportedKeys = 0;
    int lastDailyChallengeSeed = 0, highScoreDaily = 0;
    bool completedMode[4] = {}; // Unlocks.completed*Mode
    bool completedHalloween = false;
    bool seenModeUnlock[4] = {};
    bool seenMusicPack[4] = {};
    std::deque<Achievement> newlyUnlocked; // "Achievement Unlocked!" toasts waiting
    bool dirty = false;

    // ---- achievements and stats ----
    void Unlock(Achievement a);
    bool Unlocked(Achievement a) const { return unlocked[static_cast<int>(a)]; }
    void Progress(Achievement a, long long value); // TryAchievementProgress
    long long ProgressValue(const AchievementDef& d) const; // for the n / target display
    int UnlockedCount() const;
    void AddStat(Stat s, long long n);
    long long StatValue(Stat s) const { return stats[static_cast<int>(s)]; }
    const AchievementDef* Def(Achievement a) const;

    // ---- unlocks ----
    bool ModeUnlocked(const GameModeConfig* m) const; // GameModeConfig.unlockCriteriaMet
    bool ModeComplete(GameMode m) const { return completedMode[static_cast<int>(m) & 3]; }
    bool MusicPackUnlocked(int pack) const;           // 0 = A (always) .. 3 = D
    int MusicPacksUnlocked() const;
    bool TeamSelectionLocked() const;                 // LockCondition.NotEarnedEnoughKeysForFirstTeam
    bool TeamUnlockable(int team) const;              // the required achievement is unlocked
    bool TryUnlockTeam(int team);                     // Teams.TryUnlockTeam (spends Skull Keys)
    void StartedNewDailyChallenge(int seed);
    int TeamsCompleted() const;                       // Teams.GetNumberOfTeamsCompleted
    int BestScore(int team, GameMode mode) const;

    // ---- the run ----
    void RunEnded(const sim::Run& run, int team);     // final stage done (won or lost): high score
    // the stage being played (sim::MetaSink)
    void OnEvent(const sim::Game& g, const sim::Event& e) override;
    void OnStageEnded(const sim::Game& g) override;
    void OnPlayerAction(const sim::Game& g, const sim::Command& c) override;
    int teamIndex = 0;   // the run's team (Teams.currentTeam)
    bool newHighScore = false;

private:
    const GameData& data_;
    // AchievementManagerDeathball / SessionStatTracker state of the stage and the run
    const sim::Game* stage_ = nullptr;
    int enemiesHit_ = 0, enemiesKilled_ = 0, spikeKills_ = 0;
    bool finalStageOnePlayer_ = false;
    std::vector<bool> turnsWithPasses_;
    bool goaliesLeftTrial_ = false, goaliesLeftGame_ = false;
    std::map<int, int> lastAttacker_; // victim uid -> attacker uid of the last hit
    std::map<int, int> enemyHitsThisTurn_;  // Enemy.timesHitByEnemiesThisTurn
    std::map<int, int> tilesMovedThisTurn_; // Player.tilesMovedThisTurn
    std::vector<int> surrounded_;           // playersSurroundedThisStage
    int attacker_ = -1, attackKills_ = 0;   // the attack playing out and its kills (Attack.cs)
    int shooter_ = -1, ballKillShooter_ = -1;
    sim::V2 reboundTo_;
    int reboundDist_ = -1; // the current shot's rebound (it may play before the kill)
    bool shotToFinishLine_ = false, enemyHadBall_ = false, ballRecovered_ = false;
    sim::Phase lastPhase_ = sim::Phase::Starting;
    void CheckSurrounded(const sim::Game& g);
    void CheckTrappedEnemies(const sim::Game& g);
    void CheckBlood(const sim::Game& g);
    void NewStage(const sim::Game& g);
    void StageWon(const sim::Game& g);
    void GameComplete(const sim::Game& g);
};

// ---------------------------------------------------------------------------------------------
// The app
// ---------------------------------------------------------------------------------------------
enum class Screen {
    Title, Modes, Teams, StageStart, Play, Pause, PauseConfirm, Options, Defeat, Flash, Results, Manage, ManageConfirm,
    KeepGoing, Overwrite, Ending, HowToPlay, Achievements, SkullKeys, Unlocks,
    Intro,     // the Judgment's speech before the title (CutsceneController, "Intro" scene)
    TrialIntro // the next trial's monsters (TrialIntro, "LeagueIntro" scene)
};

// One stage on screen: the simulation, its view and the player's cursor.
struct StageScreen {
    std::unique_ptr<sim::Game> game;
    std::unique_ptr<gfx::StageView> view;
    sim::V2 cursor;
    int cursorFor = -1; // uid of the player the cursor was placed for
    std::vector<sim::Command> commands;
    bool started = false; // Play Deathball pressed (the stage start panel is gone)
};

// Frame timing for the performance readout (F1 / Select on the Vita).
struct Perf {
    float acc = 0, worst = 0, cpuAcc = 0;
    int frames = 0;
    float fps = 0, avgMs = 0, worstMs = 0, cpuMs = 0;
    void Add(float frameSec, float cpuSec);
};

class App {
public:
    App(gfx::Renderer& r, gfx::Assets& assets, const GameData& data, audio::Sound& sound, audio::Mixer& mixer,
        const std::string& dataDir);
    ~App();

    // ---- testing knobs (RunGame) ----
    bool autoPlay = false, testing = false, fixedDate = false;
    Date date;
    float clock = 0;
    Perf perf;

    void LoadMeta(const std::string& path = "");
    void GoToTitle();
    void NewRun(GameMode mode, int team, int seed);
    bool ContinueRun();
    bool Frame(const std::vector<Input>& inputs, float dt); // false: quit
    void Draw();

    static constexpr int kAchievementCols = 12;
    static constexpr int kOptionRows = 8; // 5 volumes, Suggest Undo, Auto Skip Cinematics, Restore Defaults
    void StartIntro(); // the boot cutscene, unless cinematics are skipped
    static constexpr int kMusicPopup = 100;
    static constexpr float kToastTime = 3.0f;

private:
    gfx::Renderer& r_;
    gfx::Assets& assets_;
    const GameData& data_;
    audio::Sound& sound_;
    audio::Mixer& mixer_;
    ui::Ui ui_;
    ui::Loc loc_;
    Help help_;
    Meta meta_;
    Settings settings_;
    std::string savePath_, settingsPath_, metaPath_;

    // ---- flow ----
    Screen screen_ = Screen::Title;
    int sel_ = 0, sel2_ = 0;
    float timer_ = 0;
    bool hasSave_ = false;
    RunRecord saved_; // the saved run, shown on the title's Continue
    RunRecord rec_;
    std::unique_ptr<sim::Run> run_;
    StageScreen st_;
    GameMode newMode_ = GameMode::Normal;
    Screen optionsReturn_ = Screen::Title, helpReturn_ = Screen::Title;
    bool defeat_ = false;
    int stageGold_ = 0, revealed_ = 0;
    std::string flashText_;
    bool showInfo_ = false, showLog_ = false, showPerf_ = false;
    int metaTab_ = 0;
    float helpScroll_ = 0, helpMaxScroll_ = 0;
    std::vector<int> unlockPopups_;
    SeasonalEvent season_ = SeasonalEvent::None;
    float seasonJingleTimer_ = -1;
    int trickEvent_ = 0;
    bool trickIsTrick_ = false;
    float trickTimer_ = 0, toastTimer_ = 0, autoDelay_ = 0;
    bool trialIntroPending_ = false;
    std::vector<const EnemyConfig*> trialLineup_; // TrialIntro.GetEnemiesFromTrial
    void InputCinematic(Input in);

    // ---- portraits (SpriteMaker) of the run's players, made on demand ----
    std::map<int, gfx::RuntimeSprite> portraits_;
    std::map<std::string, gfx::RuntimeSprite> helpSprites_;
    const gfx::RuntimeSprite& Portrait(const sim::HumanPlayer& hp);
    void ClearPortraits();
    void ClearHelpSprites();

    // ---- helpers ----
    const std::string& T(const std::string& key, const std::string& fallback) const { return loc_.Get(key, fallback); }
    Date Today() const { return fixedDate ? date : Date::TodayUtc(); }
    const GameModeConfig* ModeConfig() const;
    bool PlayedDailyToday() const;
    bool RestartAllowed() const;
    std::string StageName() const;
    std::string ModeName(GameMode m) const;
    std::string TeamName(int team) const;
    std::string PerkName(const PerkChain* chain) const;
    std::string ClassName(const PlayerClassConfig* c) const;
    float* Volume(int row);
    int HelpPageCount() const { return static_cast<int>(help_.pages.size()) + 1; } // + the port's controls page

    // ---- flow ----
    void Go(Screen s, int sel = 0);
    void SaveMeta();
    void QueueUnlockPopups();
    void RebuildRun();
    void StartStage();
    void StageOver();
    void AfterResults();
    void AfterManage(bool confirmed);
    void KeepGoingOrQuit();
    void NextStage(bool saveAndQuit);
    void EndRun();
    void OpenHowToPlay(Screen from);
    void ManageAchievements(int code);

    // ---- input ----
    void InputTitle(Input in);
    void InputModes(Input in);
    void InputTeams(Input in);
    void InputStageStart(Input in);
    void InputPlay(const std::vector<Input>& inputs, bool idle);
    void InputPause(Input in);
    void InputOptions(Input in);
    void InputManage(Input in);
    void InputHowToPlay(Input in);
    void InputAchievements(Input in);
    void AutoInputs(std::vector<Input>* inputs, bool idle, float dt);

    // ---- drawing (screens*.cpp) ----
    void DrawStage();
    void DrawHud();
    void DrawInfoPopup();
    void DrawTitle();
    void DrawModes();
    void DrawTeams();
    void DrawStageStart();
    void DrawPause();
    void DrawConfirm(const std::string& header, const std::string& body, const std::vector<std::string>& options, int sel);
    void DrawOptions();
    void DrawDefeat();
    void DrawFlash();
    void DrawResults();
    void DrawManage();
    void DrawKeepGoing();
    void DrawOverwrite();
    void DrawEnding();
    void DrawHowToPlay();
    void DrawAchievements();
    void DrawSkullKeys();
    void DrawUnlocks();
    void DrawToast();
    void DrawIntro();
    void DrawTrialIntro();
    void DrawDebug();
    void DrawPlayerCard(const sim::HumanPlayer& hp, float x, float y, float w, bool selected, int selRow);
    std::string ButtonLabel(Input in) const;
};

} // namespace dd::app
