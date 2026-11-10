// The app: the flow between the game's screens (title, modes, teams, a stage and its menus, results,
// team management, How to Play, Stats & Achievements) on top of the simulation, its view and sound.
//
//   game_app.cpp      flow and input (one Input* function per screen), the entry point
//   screens*.cpp      drawing of each screen
//   meta.cpp          persistent progress: stats, achievements, Skull Keys, unlocks (meta.json)
//   settings.cpp      options (settings.json)
//   help.cpp          How to Play pages and the in-game info popups (help.json)
#pragma once
#include <algorithm>
#include <deque>

#include <nlohmann/json.hpp>
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
#include "ui/canvas.h"
#include "ui/ui.h"

namespace dd::app {

// ---------------------------------------------------------------------------------------------
// Options (AppSettingsDeathball)
// ---------------------------------------------------------------------------------------------
struct Settings {
    float master = 0.7f, music = 0.7f, jingles = 0.7f, sounds = 0.7f, voices = 1.0f, crowd = 1.0f;
    bool suggestUndo = true;
    bool autoSkipCinematics = false;
    bool healthBars = true; // the port's: a HealthBar_WithActOrder over every actor
    bool fastMode = false, dynamicCamera = true, greyBlood = false;
    bool actionPopups = true, commandCostPopups = true, menuPopups = true, enemyPopups = true, tilePopups = true;
    // Graphics: the original's pixel filter is its 640x360 frame; off here (the screen's own resolution)
    // as the Vita stretches it by an uneven 1.5. Smooth fonts: on for the same reason.
    bool pixelFilter = false, smoothFonts = true; // AppSettingsDeathball.pref_autoSkipCinematics
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
void DrawHints(ui::Ui& u, const std::vector<std::pair<Input, std::string>>& hints); // the bottom row of button hints
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
    Meta& operator=(const Meta& o) {
        // only the progress (data_ stays: a reference)
        std::copy(o.unlocked, o.unlocked + static_cast<int>(Achievement::Count), unlocked);
        std::copy(o.stats, o.stats + static_cast<int>(Stat::Count), stats);
        teams = o.teams;
        keys = o.keys, unreportedKeys = o.unreportedKeys, lastDailyChallengeSeed = o.lastDailyChallengeSeed, highScoreDaily = o.highScoreDaily;
        std::copy(o.completedMode, o.completedMode + 4, completedMode);
        completedHalloween = o.completedHalloween;
        std::copy(o.seenModeUnlock, o.seenModeUnlock + 4, seenModeUnlock);
        std::copy(o.seenMusicPack, o.seenMusicPack + 4, seenMusicPack);
        hallOfFame = o.hallOfFame;
        return *this;
    }

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
    std::vector<std::pair<std::string, std::string>> hallOfFame; // player name, short date
    void AddHallOfFameEntry(const std::string& name, const std::string& date);
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
    bool newHighScore = false;     // the team's (or the daily challenge's)
    bool newModeHighScore = false; // over every team
    int prevModeBest = 0, prevTeamBest = 0;

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
    bool LoadJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------------------------
// The app
// ---------------------------------------------------------------------------------------------
enum class Screen {
    Title, Modes, NewGame, Teams, StageStart, Play, Pause, PauseConfirm, Options, Defeat, Flash, Results, Manage, ManageConfirm,
    KeepGoing, Overwrite, Ending, TrialComplete, HowToPlay, Achievements, SkullKeys, Unlocks,
    Intro,     // the Judgment's speech before the title (CutsceneController, "Intro" scene)
    TrialIntro, // the next trial's monsters (TrialIntro, "LeagueIntro" scene)
    Credits,    // MenuPage_Credits
    TeamInfo    // Canvas_Gameplay's MenuPage_Help
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

    static constexpr int kAchievementCols = 14; // AchievementGrid's GridLayoutGroup constraint
    int optionsPage_ = 0; // 0 main, 1 gameplay, 2 sound
    int OptionRows();
    int ScreenIndex() const { return static_cast<int>(screen_); }
    float ScreenTime() const { return timer_; }
    struct ToggleRow {
        const char* node;
        const char* key;
        const char* fb;
        bool* value;
    };
    std::vector<ToggleRow> ToggleRows();
    void ApplyVisualSettings(); // pixel filter, fonts
    bool DrawOptionsCanvas();
    void StartIntro(bool force = false); // the boot cutscene, unless cinematics are skipped
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
    // the original's menus (render/ui_title.json, ui_game.json: the scenes' Canvas trees)
    ui::Canvas titleUi_, gameUi_, prefabUi_, optionsUi_, endingUi_, transitionUi_, introUi_, trialIntroUi_;
    double transAt_ = -100; // ScreenTransition (Slam): when the last change of scene happened
    void DrawTransition(); // prefabUi_: the gameplay UI prefabs (PlayerInfoPanel...)
    int hudMark_ = -1;                        // gameUi_'s own nodes; the HUD's instances follow
    std::vector<uint32_t> phaseColors_;
    bool DrawHudCanvas();
    bool DrawStageStartCanvas();
    bool DrawTitleCanvas(const char* page = "MenuPage_MainMenu"); // false without Canvas trees (older install)
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
    int newTeam_ = 0; // Teams.currentTeam for the next run
    Screen optionsReturn_ = Screen::Title, helpReturn_ = Screen::Title;
    bool defeat_ = false;
    int stageGold_ = 0, revealed_ = 0;
    static constexpr int kResultSteps = 12; // the results screen's appearance steps
    bool DrawResultsCanvas();
    bool DrawManageCanvas();
    bool DrawKeepGoingCanvas();
    bool DrawOverwriteCanvas();
    bool DrawPauseCanvas();
    bool DrawPauseConfirmCanvas();
    int TitlePage(const char* page);
    bool DrawModesCanvas();
    bool DrawNewGameCanvas();
    bool DrawTeamsCanvas();
    bool DrawUnlockCanvas();
    bool DrawStatsCanvas();
    void InputNewGame(Input in);
    void DrawBlackFade();
    bool DrawFlashCanvas();
    bool DrawDefeatCanvas();
    bool DrawSkullKeysCanvas();
    bool DrawTrialCompleteCanvas();
    bool DrawHealthBars();
    bool DrawTeamInfoCanvas();
    bool DrawGameplayPopups();
    bool DrawSuggestUndoCanvas();
    bool DrawHowToPlayCanvas();
    std::vector<int> HowToPlayButtons(const ui::Canvas& c, int page) const;
    void InputTeamInfo(Input in);
    void FillAttackTooltip(ui::Canvas& c, int tip, const AttackConfig* cfg, int damage, int knockback, int stun, bool shoot);
    void BuildTimeline(ui::Canvas& c, int root, int dataRoot, int reveal = 12, bool survivalBonus = false);
    bool DrawEndingCanvas();
    float EndingDoneTime() const;
    int GameplayMenuPage(const char* page);
    void DrawGameplayMenu();
    std::string flashText_;
    bool showInfo_ = true, showLog_ = false, showPerf_ = false;
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
