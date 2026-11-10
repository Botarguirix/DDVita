// The game loop and the flow between screens (see app.h).
//
//   ddgame [data_dir] [seed|daily|continue] [mode] [team] [--shot out.bmp seconds] [--auto] [--speed k]
//          [--inputs keys]
//   (--auto plays by itself, --speed scales time, --inputs plays a key script: all for testing)
//
// Without a seed it opens the title screen. --inputs takes one key every 0.3 s: U D L R (d-pad),
// C confirm, B back, M menu, S switch, P pass/take, T shoot, Z rest, X undo, I info, '.' nothing.
#include "app/game_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/platform.h"
#include "core/calendar.h"
#include "core/session_generator.h"
#include "install/installer.h"

namespace dd::app {

using namespace dd::sim;
using namespace dd::gfx;

namespace {

bool IsPassTake(const PlayerActionConfig* a) { return a->action == ActorAction::Player_Pass || a->action == ActorAction::Player_TakeBall; }
bool IsShoot(const PlayerActionConfig* a) { return a->action == ActorAction::Player_Shoot; }
bool IsRest(const PlayerActionConfig* a) { return a->action == ActorAction::Char_Wait; }
bool IsUndo(const PlayerActionConfig* a) { return a->undo; }

bool IsBack(Input in) { return in == Input::Back || in == Input::Rest || in == Input::No; }

// Menu cursor over count rows; true when it moved.
bool Nav(Input in, int* sel, int count) {
    if (count <= 0) return false;
    int s = *sel;
    if (in == Input::Up) s = (s + count - 1) % count;
    if (in == Input::Down) s = (s + 1) % count;
    if (s == *sel) return false;
    *sel = s;
    return true;
}

Input ScriptKey(char k) {
    switch (k) {
    case 'U': return Input::Up;
    case 'D': return Input::Down;
    case 'L': return Input::Left;
    case 'R': return Input::Right;
    case 'C': return Input::Confirm;
    case 'B': return Input::Back;
    case 'M': return Input::Menu;
    case 'S': return Input::Switch;
    case 'P': return Input::PassTake;
    case 'T': return Input::Shoot;
    case 'Z': return Input::Rest;
    case 'X': return Input::Undo;
    case 'I': return Input::Info;
    default: return Input::None;
    }
}

} // namespace

void Perf::Add(float frameSec, float cpuSec) {
    acc += frameSec;
    cpuAcc += cpuSec;
    worst = std::max(worst, frameSec);
    if (++frames, acc >= 1.0f) {
        fps = frames / acc;
        avgMs = acc * 1000 / frames;
        cpuMs = cpuAcc * 1000 / frames;
        worstMs = worst * 1000;
        acc = cpuAcc = worst = 0;
        frames = 0;
    }
}

App::App(Renderer& r, Assets& assets, const GameData& data, audio::Sound& sound, audio::Mixer& mixer, const std::string& dataDir)
    : r_(r), assets_(assets), data_(data), sound_(sound), mixer_(mixer), ui_(r, assets), meta_(data) {
    savePath_ = platform::SaveDir() + "/run.json";
    settingsPath_ = platform::SaveDir() + "/settings.json";
    metaPath_ = platform::SaveDir() + "/meta.json";
    if (!meta_.LoadDefs(dataDir + "/achievements.json")) std::fprintf(stderr, "no achievements.json: achievements are not shown\n");
    loc_.Load(dataDir + "/loc/en.json"); // language selection: meta milestone
    help_.Load(dataDir + "/help.json");
    titleUi_.Load(dataDir + "/render/ui_title.json");
    gameUi_.Load(dataDir + "/render/ui_game.json");
    prefabUi_.Load(dataDir + "/render/ui_prefabs.json");
    settings_.Load(settingsPath_);
    settings_.Apply(mixer_);
    hasSave_ = RecordExists(savePath_);
}

// ---------------------------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------------------------
void App::Go(Screen s, int sel) {
    if (testing && s != screen_) std::printf("screen %d at %.1f s\n", static_cast<int>(s), static_cast<double>(clock));
    screen_ = s;
    sel_ = sel;
    sel2_ = 0;
    timer_ = 0;
}

const GameModeConfig* App::ModeConfig() const { return data_.GetGameModeConfig(rec_.mode); }

bool App::PlayedDailyToday() const { return meta_.lastDailyChallengeSeed == DailyChallengeSeed(data_.platform, Today()); }

bool App::RestartAllowed() const {
    const GameModeConfig* m = ModeConfig();
    return !rec_.restartUsed && (!m || m->allowRestartStage); // GameFlow.RestartAllowed
}

std::string App::StageName() const {
    return ui::Format(T("Gameplay/StageNumber", "Stage {0.trial}-{1.stage}"),
                      {{"trial", ui::Int(run_->currentTrial + 1)}, {"stage", ui::Int(run_->currentStage + 1)}});
}

void App::LoadMeta(const std::string& path) {
    if (!meta_.Load(path.empty() ? metaPath_ : path) && !path.empty()) std::fprintf(stderr, "cannot read %s\n", path.c_str());
}

// TitleScreen.FinishedSequence: modes and music packs unlocked since the last visit
void App::QueueUnlockPopups() {
    unlockPopups_.clear();
    for (GameMode m : {GameMode::DailyChallenge, GameMode::Boss, GameMode::Action}) {
        const GameModeConfig* c = data_.GetGameModeConfig(m);
        if (c && meta_.ModeUnlocked(c) && !meta_.seenModeUnlock[static_cast<int>(m)]) unlockPopups_.push_back(static_cast<int>(m));
    }
    for (int p = 1; p < 4; p++)
        if (meta_.MusicPackUnlocked(p) && !meta_.seenMusicPack[p]) {
            unlockPopups_.push_back(kMusicPopup);
            break;
        }
}

void App::SaveMeta() {
    if (testing || !meta_.dirty) return;
    if (!meta_.Save(metaPath_)) std::fprintf(stderr, "cannot write %s\n", metaPath_.c_str());
}

void App::GoToTitle() {
    SaveMeta();
    sound_.StopCrowd(0.5f);
    st_ = StageScreen{};
    run_.reset();
    hasSave_ = LoadRecord(savePath_, &saved_);
    ClearPortraits();
    sound_.FadeOutMusic(0.5f);
    sound_.PlayJingle("jingle.title");
    sound_.PlayNamedMusic("music.title", 1.0f, 1.0f);
    // TitleScreen.TitleScreenSequence: the season's jingle comes in with the logo
    const SeasonalEventConfig* sc = data_.SeasonalConfig(DetectSeasonalEvent(Today()));
    season_ = sc ? sc->seasonalEvent : SeasonalEvent::None;
    seasonJingleTimer_ = sc && season_ != SeasonalEvent::None && sc->titleScreenJingle ? 0.9f : -1;
    QueueUnlockPopups();
    Go(unlockPopups_.empty() ? Screen::Title : Screen::Unlocks, hasSave_ ? 0 : 1);
}

void App::NewRun(GameMode mode, int team, int seed) {
    rec_ = RunRecord{};
    rec_.date = Today();
    rec_.mode = mode;
    rec_.team = team;
    rec_.seed = mode == GameMode::DailyChallenge ? DailyChallengeSeed(data_.platform, rec_.date) : seed;
    ClearPortraits();
    RebuildRun();
    meta_.AddStat(Stat::GamesStarted, 1); // GlobalStatTracker.StartedNewGame
    if (mode == GameMode::DailyChallenge) meta_.StartedNewDailyChallenge(rec_.seed);
    meta_.unreportedKeys = 0; // SkullKeys.NewGame
    sound_.Shuffle(meta_.MusicPacksUnlocked());
    std::printf("new run: seed %d mode %d team %d\n", rec_.seed, static_cast<int>(rec_.mode), rec_.team);
    std::fflush(stdout);
    StartStage();
}

bool App::ContinueRun() {
    RunRecord rec;
    if (!LoadRecord(savePath_, &rec)) return false;
    rec_ = rec;
    ClearPortraits();
    RebuildRun();
    meta_.unreportedKeys = 0;
    sound_.Shuffle(meta_.MusicPacksUnlocked());
    for (size_t i = 0; i < rec_.stages.size(); i++) sound_.IncrementStageMusicIndex();
    if (!testing) DeleteRecord(savePath_); // Continue_Warning: the mid-game save loads once
    hasSave_ = false;
    // no %zu: the Vita's newlib printf does not know the z modifier (it shifts the arguments)
    std::printf("continue: seed %d mode %d stage %d\n", rec_.seed, static_cast<int>(rec_.mode), static_cast<int>(rec_.stages.size()) + 1);
    std::fflush(stdout);
    StartStage();
    return true;
}

// The run at the start of the current stage, replayed from the record (also the stage restart:
// SessionStageBackup.Restore puts the team back as it was when the stage started).
void App::RebuildRun() {
    st_ = StageScreen{};
    SessionRequest req = RequestFor(data_, rec_);
    run_ = std::make_unique<Run>(data_, GenerateSession(data_, req), req.team, req.mode);
    if (!ReplayStages(*run_, rec_)) { // saved with other game data: start that run again
        std::fprintf(stderr, "saved run does not replay; restarting it\n");
        rec_.stages.clear();
        run_ = std::make_unique<Run>(data_, GenerateSession(data_, req), req.team, req.mode);
    }
    run_->ballEffect = static_cast<DifficultyEffect>(rec_.current.ball);
}

void App::StartStage() {
    sound_.StartCrowd();
    st_.view.reset();
    st_.game = std::make_unique<Game>(*run_);
    st_.game->suggestUndo = settings_.suggestUndo;
    meta_.teamIndex = rec_.mode == GameMode::DailyChallenge ? 0 : rec_.team;
    st_.game->meta = &meta_; // only the stage being played reports (never the replays)
    st_.game->Start();
    st_.view = std::make_unique<StageView>(assets_, r_, *st_.game);
    Game* g = st_.game.get();
    st_.view->onEvent = [this, g](const Event& e) {
        sound_.OnEvent(e, *g);
        if (e.type == Ev::TrickOrTreat) { // SeasonalEventGameplayPopup.DisplayHalloween
            trickEvent_ = e.value;
            trickIsTrick_ = e.extra != 0;
            trickTimer_ = 3.5f;
        }
    };
    trickTimer_ = 0;
    st_.cursorFor = -1;
    st_.started = false;
    rec_.current.inputs.clear();
    rec_.current.meta.clear();
    rec_.current.ball = static_cast<int>(run_->ballEffect);
    rec_.current.swap = run_->swapPositions;
    rec_.current.carrier = run_->ballCarrierIndex;
    defeat_ = false;
    if (trialIntroPending_) {
        trialIntroPending_ = false;
        // TrialIntro.GetEnemiesFromTrial: the trial's monsters in tile order, up to five
        trialLineup_.clear();
        for (const Stage& stage : run_->session.trials[static_cast<size_t>(run_->currentTrial)].stages)
            for (const TileData& t : stage.tiles)
                if (t.enemyConfig && trialLineup_.size() < 5 &&
                    std::find(trialLineup_.begin(), trialLineup_.end(), t.enemyConfig) == trialLineup_.end())
                    trialLineup_.push_back(t.enemyConfig);
        sound_.FadeOutMusic(0.5f);
        Go(Screen::TrialIntro, 0);
        return;
    }
    Go(Screen::StageStart, 0);
}

// GameController.PlayerLost / PlayerWon -> StageEndUI.GameEnded
void App::StageOver() {
    SaveMeta();
    Game& g = *st_.game;
    if (g.gameEndedInDefeat && RestartAllowed()) {
        sound_.FadeOutMusic(0.25f);
        Go(Screen::Defeat);
        return;
    }
    defeat_ = g.gameEndedInDefeat;
    stageGold_ = g.goldEarned();
    const StageResults& res = g.results();
    flashText_ = defeat_ ? T("Gameplay/Defeat_YouLose", "You Lose")
                 : res.touchdown ? T("Gameplay/Victory_Touchdown", "Touchdown!")
                                 : T("Gameplay/Victory_NoTouchdown", "Made It!");
    // StageEndUI.GameEndMusic
    bool stillTriumphant = sound_.triumphantMusicIsPlaying();
    if (defeat_ || !stillTriumphant) sound_.FadeOutMusic(0.25f);
    if (defeat_) {
        sound_.PlayNamedMusic("music.defeat", 0, 0.25f);
    } else {
        sound_.PlayJingle("jingle.victory");
        if (!stillTriumphant) sound_.PlayNamedMusic("music.results", 2.0f, 4.25f);
    }
    Go(Screen::Flash);
}

// StageEndUI.ResultsScreenHidden -> team management when the run goes on
void App::AfterResults() {
    if (!defeat_ && !run_->currentStageIsFinalStageInFinalTrial()) Go(Screen::Manage);
    else KeepGoingOrQuit();
}

void App::AfterManage(bool confirmed) {
    if (!confirmed && run_->AffordableUpgradesOrRecruitsAvailable()) Go(Screen::ManageConfirm, 1);
    else KeepGoingOrQuit();
}

// StageEndUI.ShowSkullKeyUnlocksIfApplicable, then ShowContinueOrSaveAndQuitIfApplicable
void App::KeepGoingOrQuit() {
    if (meta_.unreportedKeys > 0) {
        sound_.PlayCue("SkullKeyAcquired");
        Go(Screen::SkullKeys);
        return;
    }
    const GameModeConfig* m = ModeConfig();
    if (defeat_ || rec_.mode == GameMode::DailyChallenge || run_->currentStageIsFinalStageInFinalTrial() || (m && !m->allowSaveAndQuit))
        NextStage(false);
    else
        Go(Screen::KeepGoing);
}

// GameFlow.NextStage (and QuitRun with the session saved)
void App::NextStage(bool saveAndQuit) {
    if (defeat_ || run_->currentStageIsFinalStageInFinalTrial()) {
        EndRun();
        return;
    }
    rec_.stages.push_back(rec_.current);
    rec_.restartUsed = false; // LeagueManager.IncrementStage
    run_->AdvanceStage();
    run_->swapPositions = false;
    // GameFlow.NextStage: a new trial opens with its intro (unless cinematics are skipped)
    trialIntroPending_ = run_->currentStage == 0 && !settings_.autoSkipCinematics && !testing;
    sound_.IncrementStageMusicIndex();
    rec_.current = StageRecord{};
    rec_.current.ball = static_cast<int>(run_->ballEffect);
    if (saveAndQuit) {
        if (!testing && !SaveRecord(savePath_, rec_)) std::fprintf(stderr, "cannot write %s\n", savePath_.c_str());
        GoToTitle();
        return;
    }
    sound_.FadeOutMusic(1.0f);
    StartStage();
}

void App::EndRun() {
    sound_.StopCrowd(5.0f);
    if (!defeat_) {
        sound_.FadeOutMusic(1.0f);
        sound_.PlayJingle("jingle.endingVictory");
        sound_.PlayNamedMusic("music.endingVictory", 1.0f, 3.0f);
    }
    meta_.RunEnded(*run_, rec_.team);
    SaveMeta();
    std::printf("run over: final score %d\n", run_->GetFinalScoreTotal());
    std::fflush(stdout);
    Go(Screen::Ending);
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------
void App::InputTitle(Input in) {
    // MenuPage_MainMenu's buttons, all always there: Continue (locked without a saved run),
    // New Game, How to Play, Stats & Achievements, Options, Credits
    if (Nav(in, &sel_, 6)) sound_.PlayCue("MenuMove");
    if (in != Input::Confirm) return;
    if (sel_ == 0 && !hasSave_) { // MenuObjectLocker (LockCondition.SessionSaveDataDoesNotExist)
        sound_.PlayCue("MenuInvalid");
        return;
    }
    sound_.PlayCue("MenuSubmit");
    switch (sel_) {
    case 0:
        if (!ContinueRun()) hasSave_ = false;
        break;
    case 1: Go(Screen::Modes); break;
    case 2: OpenHowToPlay(Screen::Title); break;
    case 3:
        metaTab_ = 0;
        Go(Screen::Achievements);
        break;
    case 4:
        optionsReturn_ = Screen::Title;
        Go(Screen::Options);
        break;
    case 5: Go(Screen::Credits); break;
    }
}

// Stats & Achievements: the d-pad moves through the achievement grid, Switch flips to the stats
void App::InputAchievements(Input in) {
    if (in == Input::Menu || IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        Go(Screen::Title, 3);
        return;
    }
    if (in == Input::Switch || (metaTab_ == 1 && in == Input::Confirm)) {
        metaTab_ ^= 1;
        sound_.PlayCue("MenuMove");
        return;
    }
    int n = static_cast<int>(meta_.defs.size());
    if (metaTab_ != 0 || n == 0) return;
    int s = sel_;
    switch (in) {
    case Input::Left: s = s % kAchievementCols == 0 ? std::min(s + kAchievementCols - 1, n - 1) : s - 1; break;
    case Input::Right: s = s % kAchievementCols == kAchievementCols - 1 || s == n - 1 ? s - s % kAchievementCols : s + 1; break;
    case Input::Up: s = s >= kAchievementCols ? s - kAchievementCols : s; break;
    case Input::Down: s = s + kAchievementCols < n ? s + kAchievementCols : s; break;
    default: break;
    }
    if (s != sel_) {
        sel_ = s;
        sound_.PlayCue("MenuMove");
    }
}

void App::StartIntro(bool force) {
    if (!force && (settings_.autoSkipCinematics || testing)) {
        GoToTitle();
        return;
    }
    sound_.PlayNamedMusic("music.boss", 0.5f, 2.0f);
    Go(Screen::Intro);
}

// CutsceneController / TrialIntro: Confirm, Start or Back skip
void App::InputCinematic(Input in) {
    if ((in != Input::Confirm && in != Input::Menu && !IsBack(in)) || timer_ < 0.3f) return;
    sound_.PlayCue("MenuSubmit");
    if (screen_ == Screen::Intro) GoToTitle();
    else Go(Screen::StageStart, 0);
}

void App::OpenHowToPlay(Screen from) {
    helpReturn_ = from;
    helpScroll_ = 0;
    Go(Screen::HowToPlay);
}

// How to Play: up / down pick the page, left / right scroll a long page
void App::InputHowToPlay(Input in) {
    if (in == Input::Menu || IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        ClearHelpSprites();
        // back to the How to Play entry of the menu it came from
        Go(helpReturn_, helpReturn_ == Screen::Pause ? 3 : 2);
        return;
    }
    if (Nav(in, &sel_, HelpPageCount())) {
        sound_.PlayCue("MenuMove");
        helpScroll_ = 0;
    } else if (in == Input::Left || in == Input::Right) {
        float s = std::clamp(helpScroll_ + (in == Input::Right ? 48.0f : -48.0f), 0.0f, helpMaxScroll_);
        if (s != helpScroll_) sound_.PlayCue("MenuMove");
        helpScroll_ = s;
    }
}

void App::InputModes(Input in) {
    static const GameMode modes[4] = {GameMode::Normal, GameMode::DailyChallenge, GameMode::Boss, GameMode::Action};
    if (Nav(in, &sel_, 4)) sound_.PlayCue("MenuMove");
    if (IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        Go(Screen::Title, 1);
        return;
    }
    if (in != Input::Confirm) return;
    const GameModeConfig* m = data_.GetGameModeConfig(modes[sel_]);
    if (!meta_.ModeUnlocked(m)) { // LockCondition.GameModeCriteria
        sound_.PlayCue("MenuInvalid");
        return;
    }
    sound_.PlayCue("MenuSubmit");
    newMode_ = modes[sel_];
    int seed = static_cast<int>(static_cast<long long>(platform::Seconds() * 1e6) & 0x7fffffff);
    // the Teams button stays locked until the first Skull Key (LockCondition.NotEarnedEnoughKeysForFirstTeam)
    if ((m && !m->allowTeamSelection) || meta_.TeamSelectionLocked()) NewRun(newMode_, 0, seed);
    else Go(Screen::Teams);
}

void App::InputTeams(Input in) {
    int n = static_cast<int>(data_.teams.size());
    if (Nav(in, &sel_, n)) sound_.PlayCue("MenuMove");
    if (IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        int m = newMode_ == GameMode::Normal ? 0 : newMode_ == GameMode::DailyChallenge ? 1 : newMode_ == GameMode::Boss ? 2 : 3;
        Go(Screen::Modes, m);
        return;
    }
    if (in != Input::Confirm) return;
    if (!meta_.teams[static_cast<size_t>(sel_)].unlocked) { // mooDeathballTeam.TryUnlockTeam
        if (meta_.TryUnlockTeam(sel_)) {
            sound_.PlayCue("RecruitPlayer");
            SaveMeta();
        } else {
            sound_.PlayCue("MenuInvalid");
        }
        return;
    }
    sound_.PlayCue("MenuSubmit");
    NewRun(newMode_, sel_, static_cast<int>(static_cast<long long>(platform::Seconds() * 1e6) & 0x7fffffff));
}

// StageStartPanel: swap ball (Easy / Normal / Hard), then Play Deathball
// StageStartPanel's buttons: sel_ 0 Play Deathball, 1 Swap Players, 2 Swap Ball, 3..5 the Easy /
// Normal / Hard balls (the row above). L and Square keep their shortcuts.
void App::InputStageStart(Input in) {
    bool ballMods = !run_->team->traitBlockBallMods();
    bool two = run_->activePlayers.size() == 2;
    static const DifficultyEffect order[3] = {DifficultyEffect::Easier, DifficultyEffect::Neutral, DifficultyEffect::Harder};
    auto rebuild = [&] { // the stage is set up again; the cursor stays where it was
        int keep = sel_;
        StartStage();
        sel_ = keep;
    };
    auto swapPlayers = [&] {
        sound_.PlayCue("MenuSubmit");
        run_->swapPositions = !run_->swapPositions;
        rebuild();
    };
    auto swapBall = [&] {
        if (!two) return sound_.PlayCue("MenuInvalid");
        sound_.PlayCue("MenuSubmit");
        run_->ballCarrierIndex = run_->ballCarrierIndex == 0 ? 1 : 0;
        rebuild();
    };
    int cur = run_->ballEffect == DifficultyEffect::Easier ? 0 : run_->ballEffect == DifficultyEffect::Neutral ? 1 : 2;
    int prev = sel_;
    switch (in) {
    case Input::Up:
        if (sel_ == 0) sel_ = 2;
        else if (sel_ == 2) sel_ = 1;
        else if (sel_ == 1 && ballMods) sel_ = 3 + cur;
        break;
    case Input::Down:
        if (sel_ >= 3) sel_ = 1;
        else if (sel_ == 1) sel_ = 2;
        else if (sel_ == 2) sel_ = 0;
        break;
    case Input::Left:
    case Input::Right:
        if (sel_ >= 3) sel_ = std::clamp(sel_ + (in == Input::Right ? 1 : -1), 3, 5);
        break;
    case Input::Switch: swapPlayers(); return;
    case Input::PassTake: swapBall(); return;
    case Input::Menu:
        sound_.PlayCue("MenuSubmit");
        Go(Screen::Pause);
        return;
    case Input::Confirm:
        if (sel_ == 1) return swapPlayers();
        if (sel_ == 2) return swapBall();
        if (sel_ >= 3) { // mooDeathballChangeBall: BallMods.SetBallMod (the stage is rebuilt with the new ball)
            sound_.PlayCue("MenuSubmit");
            if (sel_ - 3 != cur) {
                run_->ballEffect = order[sel_ - 3];
                rebuild();
            }
            return;
        }
        sound_.PlayCue("MenuSubmit");
        st_.started = true;
        sound_.StageStarted(*st_.game);
        Go(Screen::Play);
        return;
    default: break;
    }
    if (sel_ != prev) sound_.PlayCue("MenuMove");
}

void App::InputPlay(const std::vector<Input>& inputs, bool idle) {
    Game& g = *st_.game;
    // every command goes through here so the save can replay it
    auto select = [&](const Command& c) {
        if (c.tooExpensive || !c.interactable) {
            sound_.CommandPressed(true);
            return;
        }
        int index = static_cast<int>(&c - st_.commands.data());
        sound_.CommandPressed(false);
        if (c.actionConfig->switchControl) sound_.PlayerSwitched();
        if (g.Select(c)) rec_.current.inputs.push_back(index); // a refused command changes nothing
    };
    auto selectButton = [&](bool (*pred)(const PlayerActionConfig*)) {
        for (const Command& c : st_.commands)
            if (!c.onTile && pred(c.actionConfig)) {
                select(c);
                return;
            }
    };
    for (Input in : inputs) {
        if (in == Input::Menu) {
            sound_.PlayCue("MenuSubmit");
            Go(Screen::Pause);
            return;
        }
        if (in == Input::Info) { // "View more info": the popup follows the cursor until toggled off
            showInfo_ = !showInfo_;
            sound_.PlayCue(showInfo_ ? "MenuSubmit" : "MenuCancel");
            continue;
        }
        if (!idle) continue;
        if (g.phase() == Phase::SuggestUndo) { // GameController.SuggestUndo popup: Undo / Ignore
            if (in == Input::Left || in == Input::Right || in == Input::Up || in == Input::Down) {
                sel2_ ^= 1;
                sound_.PlayCue("MenuMove");
                continue;
            }
            bool yes = in == Input::Yes || in == Input::Undo || (in == Input::Confirm && sel2_ == 0);
            bool no = in == Input::No || IsBack(in) || (in == Input::Confirm && sel2_ == 1);
            if (yes || no) {
                sound_.PlayCue(yes ? "MenuSubmit" : "MenuCancel");
                rec_.current.inputs.push_back(yes ? RunRecord::kUndoYes : RunRecord::kUndoNo);
                g.RespondToUndoSuggestion(yes);
                sel2_ = 0;
                break;
            }
            continue;
        }
        if (g.phase() != Phase::PlayerInput) continue;
        V2& c = st_.cursor;
        switch (in) {
        case Input::Up: c.y = std::min(c.y + 1, g.fieldY - 1); break;
        case Input::Down: c.y = std::max(c.y - 1, 0); break;
        case Input::Left: c.x = std::max(c.x - 1, 0); break;
        case Input::Right: c.x = std::min(c.x + 1, g.fieldX - 1); break;
        case Input::Confirm:
            for (const Command& cmd : st_.commands)
                if (cmd.onTile && cmd.tile == c) {
                    select(cmd);
                    break;
                }
            break;
        case Input::PassTake: selectButton(IsPassTake); break;
        case Input::Shoot: selectButton(IsShoot); break;
        case Input::Rest: selectButton(IsRest); break;
        case Input::Undo: selectButton(IsUndo); break;
        case Input::Switch:
            for (const Command& cmd : st_.commands)
                if (cmd.actionConfig->switchControl) {
                    select(cmd);
                    break;
                }
            break;
        default: break;
        }
        if (st_.view->Busy()) break; // the action started: wait for it to play out
    }
}

// Pause: Resume, Restart Stage, Options, How to Play, Quit (the run is lost: GameFlow.QuitRun
// without saving)
void App::InputPause(Input in) {
    if (in == Input::Menu || IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        Go(st_.started ? Screen::Play : Screen::StageStart);
        return;
    }
    if (Nav(in, &sel_, 5)) sound_.PlayCue("MenuMove");
    if (in != Input::Confirm) return;
    switch (sel_) {
    case 0:
        sound_.PlayCue("MenuSubmit");
        Go(st_.started ? Screen::Play : Screen::StageStart);
        break;
    case 1:
        if (!RestartAllowed() || !st_.started) {
            sound_.PlayCue("MenuInvalid");
            break;
        }
        sound_.PlayCue("MenuSubmit");
        Go(Screen::PauseConfirm, 1);
        sel2_ = 1; // 1 = restart confirmation
        break;
    case 2:
        sound_.PlayCue("MenuSubmit");
        optionsReturn_ = Screen::Pause;
        Go(Screen::Options);
        break;
    case 3:
        sound_.PlayCue("MenuSubmit");
        OpenHowToPlay(Screen::Pause);
        break;
    case 4:
        sound_.PlayCue("MenuSubmit");
        Go(Screen::PauseConfirm, 0);
        sel2_ = 0; // 0 = quit confirmation
        break;
    }
}

float* App::Volume(int row) {
    float* v[5] = {&settings_.master, &settings_.music, &settings_.jingles, &settings_.sounds, &settings_.voices};
    return row >= 0 && row < 5 ? v[row] : nullptr;
}

void App::InputOptions(Input in) {
    if (Nav(in, &sel_, kOptionRows)) sound_.PlayCue("MenuMove");
    if (in == Input::Menu || IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        if (!testing) settings_.Save(settingsPath_);
        Screen back = optionsReturn_;
        Go(back, back == Screen::Pause ? 2 : back == Screen::Title ? 4 : 0);
        return;
    }
    if (float* v = Volume(sel_)) {
        if (in == Input::Left || in == Input::Right) {
            *v = std::clamp(std::round(*v * 10 + (in == Input::Right ? 1 : -1)) / 10, 0.0f, 1.0f);
            settings_.Apply(mixer_);
            sound_.PlayCue("MenuMove");
        }
    } else if (sel_ == 5 && (in == Input::Confirm || in == Input::Left || in == Input::Right)) {
        settings_.suggestUndo = !settings_.suggestUndo;
        if (st_.game) st_.game->suggestUndo = settings_.suggestUndo;
        sound_.PlayCue("MenuSubmit");
    } else if (sel_ == 6 && (in == Input::Confirm || in == Input::Left || in == Input::Right)) {
        settings_.autoSkipCinematics = !settings_.autoSkipCinematics;
        sound_.PlayCue("MenuSubmit");
    } else if (sel_ == 7 && in == Input::Confirm) { // mooDeathballRestoreDefaultSettings
        settings_ = Settings{};
        settings_.Apply(mixer_);
        if (st_.game) st_.game->suggestUndo = settings_.suggestUndo;
        sound_.PlayCue("MenuSubmit");
    }
}

// Manage Team: a grid of the active players' upgrades (Stamina, Strength, Perk) and, when the team
// has room, the three recruits; Done at the bottom.
void App::InputManage(Input in) {
    int players = static_cast<int>(run_->activePlayers.size());
    bool recruits = !run_->teamIsAtMaximumCapacity() && !run_->reservePlayers.empty();
    int cols = players + (recruits ? 1 : 0);
    int recruitCount = std::min<int>(3, static_cast<int>(run_->reservePlayers.size()));
    // sel2_ = column, sel_ = row; row 3 (or the recruit count) = Done
    int rows = sel2_ < players ? 3 : recruitCount;
    if (sel_ > rows) sel_ = rows;
    if (in == Input::Up && sel_ > 0) sel_--, sound_.PlayCue("MenuMove");
    else if (in == Input::Down && sel_ < rows) sel_++, sound_.PlayCue("MenuMove");
    else if ((in == Input::Left || in == Input::Right) && sel_ < rows && cols > 1) {
        sel2_ = (sel2_ + (in == Input::Right ? 1 : cols - 1)) % cols;
        int r = sel2_ < players ? 3 : recruitCount;
        sel_ = std::min(sel_, r - 1);
        sound_.PlayCue("MenuMove");
    }
    if (in == Input::Menu || IsBack(in)) {
        sound_.PlayCue("MenuCancel");
        AfterManage(false);
        return;
    }
    if (in != Input::Confirm) return;
    if (sel_ >= rows) {
        sound_.PlayCue("MenuSubmit");
        AfterManage(false);
        return;
    }
    int code = sel2_ < players ? RunRecord::Upgrade(sel2_, sel_ == 0 ? PlayerStat::Stamina : sel_ == 1 ? PlayerStat::Strength : PlayerStat::Perk)
                               : RunRecord::Recruit(sel_);
    if (ApplyMeta(*run_, code)) {
        rec_.current.meta.push_back(code);
        ManageAchievements(code);
        sound_.PlayCue(code >= 100 ? "RecruitPlayer" : "UpgradePlayer");
        if (code >= 100) { // TeamManagementScreen.RecruitedPlayer: the new player's panel takes the slot
            sel2_ = static_cast<int>(run_->activePlayers.size()) - 1;
            sel_ = 0;
        }
    } else {
        sound_.PlayCue("MenuInvalid");
    }
}

// AchievementManagerDeathball.PlayerUpgradedPerk / PlayerUpgradedStat and NewRecruitButton.TryRecruit
void App::ManageAchievements(int code) {
    if (code >= 100) {
        meta_.Unlock(Achievement::RecruitPlayer);
    } else {
        const HumanPlayer& hp = *run_->activePlayers[static_cast<size_t>(code / 10)];
        if (static_cast<PlayerStat>(code % 10) == PlayerStat::Perk) {
            if (hp.perkLevel >= 1 && hp.perkLevel <= 3)
                meta_.Unlock(hp.perkLevel == 1 ? Achievement::PerkLevel1 : hp.perkLevel == 2 ? Achievement::PerkLevel2 : Achievement::PerkLevel3);
            if (run_->teamIsAtMaximumCapacity()) {
                bool all = true;
                for (const HumanPlayer* p : run_->activePlayers) all = all && p->perkLevel >= 3;
                if (all) meta_.Unlock(Achievement::BothPerksLevel3);
            }
        }
        if (hp.StatIsMaxLevel(PlayerStat::Stamina) && hp.StatIsMaxLevel(PlayerStat::Strength) && hp.StatIsMaxLevel(PlayerStat::Perk))
            meta_.Unlock(Achievement::MaxOutPlayer);
    }
    SaveMeta();
}

// --auto: plays itself (furthest affordable tile command, else rest) and takes the default path
// through every screen, to exercise the game.
void App::AutoInputs(std::vector<Input>* inputs, bool idle, float dt) {
    autoDelay_ -= dt;
    if (autoDelay_ > 0) return;
    autoDelay_ = 0.4f;
    switch (screen_) {
    case Screen::Play: {
        if (!idle) return;
        Game& g = *st_.game;
        if (g.phase() == Phase::SuggestUndo) inputs->push_back(Input::No);
        else if (g.phase() == Phase::PlayerInput) {
            const Command* pick = nullptr;
            for (const Command& c : st_.commands)
                if (c.onTile && c.interactable && !c.tooExpensive && !c.actionConfig->switchControl && (!pick || c.tile.x > pick->tile.x))
                    pick = &c;
            if (pick) {
                st_.cursor = pick->tile;
                inputs->push_back(Input::Confirm);
            } else {
                inputs->push_back(Input::Rest);
            }
        }
        break;
    }
    case Screen::Manage:
        if (timer_ < 1.5f) return;
        // buy what it can, column by column, then Done
        if (run_->AffordableUpgradesOrRecruitsAvailable()) {
            for (int p = 0; p < static_cast<int>(run_->activePlayers.size()); p++)
                for (int s = 0; s < 3; s++)
                    if (run_->activePlayers[p]->AffordableUpgradeAvailable(s == 0 ? PlayerStat::Stamina : s == 1 ? PlayerStat::Strength : PlayerStat::Perk)) {
                        sel2_ = p;
                        sel_ = s;
                        inputs->push_back(Input::Confirm);
                        return;
                    }
            if (!run_->teamIsAtMaximumCapacity() && !run_->reservePlayers.empty()) {
                sel2_ = static_cast<int>(run_->activePlayers.size());
                sel_ = 0;
                inputs->push_back(Input::Confirm);
                return;
            }
        }
        inputs->push_back(Input::Back);
        break;
    case Screen::Ending: // stops the test (Frame returns false), after a moment on screen
        if (timer_ > 3.0f) inputs->push_back(Input::Menu);
        break;
    case Screen::Title:
    case Screen::Modes:
    case Screen::Teams:
    case Screen::StageStart:
    case Screen::Defeat:
    case Screen::ManageConfirm:
    case Screen::KeepGoing:
    case Screen::SkullKeys:
    case Screen::Unlocks: inputs->push_back(Input::Confirm); break;
    case Screen::Results:
        if (revealed_ >= 9 && timer_ > 4.0f) inputs->push_back(Input::Confirm);
        break;
    default: break;
    }
}

// One frame: inputs, then the simulation and the view. False when the app must close.
bool App::Frame(const std::vector<Input>& rawInputs, float dt) {
    clock += dt;
    timer_ += dt;
    std::vector<Input> inputs = rawInputs;
    for (Input in : inputs) {
        if (in == Input::Quit) return false;
        if (in == Input::Log) {
            // F1 cycles: nothing -> event log -> log + performance
            if (!showLog_) showLog_ = true;
            else if (!showPerf_) showPerf_ = true;
            else showLog_ = showPerf_ = false;
        }
    }
    bool idle = st_.view && !st_.view->Busy();
    if (st_.game && idle && st_.game->phase() == Phase::PlayerInput && screen_ == Screen::Play) {
        Game& g = *st_.game;
        st_.commands = g.GetCommands();
        if (g.currentPlayer && g.currentPlayer->uid != st_.cursorFor) {
            st_.cursorFor = g.currentPlayer->uid;
            st_.cursor = g.currentPlayer->gridPos;
        }
    } else {
        st_.commands.clear();
    }
    if (autoPlay) AutoInputs(&inputs, idle, dt);
    if (autoPlay && screen_ == Screen::Ending && !inputs.empty()) return false;

    switch (screen_) {
    case Screen::Title:
        for (Input in : inputs) InputTitle(in);
        if (sel_ < 0) return false;
        break;
    case Screen::Modes:
        for (Input in : inputs) InputModes(in);
        break;
    case Screen::Teams:
        for (Input in : inputs) InputTeams(in);
        break;
    case Screen::StageStart:
        for (Input in : inputs)
            if (screen_ == Screen::StageStart) InputStageStart(in);
        break;
    case Screen::Play: InputPlay(inputs, idle); break;
    case Screen::Pause:
        for (Input in : inputs)
            if (screen_ == Screen::Pause) InputPause(in);
        break;
    case Screen::PauseConfirm:
        for (Input in : inputs) {
            // sel2_: 0 quit, 1 restart; sel_: 0 = do it, 1 = stay
            if (in == Input::Left || in == Input::Right || in == Input::Up || in == Input::Down) {
                sel_ ^= 1;
                sound_.PlayCue("MenuMove");
            } else if (IsBack(in) || in == Input::Menu || (in == Input::Confirm && sel_ == 1)) {
                sound_.PlayCue("MenuCancel");
                Go(Screen::Pause, sel2_ == 0 ? 4 : 1);
                break;
            } else if (in == Input::Confirm) {
                sound_.PlayCue("MenuSubmit");
                if (sel2_ == 0) { // quit: the run is lost
                    GoToTitle();
                } else { // GameFlow.RestartStage
                    meta_.AddStat(Stat::StagesRestarted, 1);
                    RebuildRun();
                    rec_.restartUsed = true;
                    StartStage();
                }
                break;
            }
        }
        break;
    case Screen::Options:
        for (Input in : inputs) InputOptions(in);
        break;
    case Screen::Defeat: // Restart Stage / Give Up
        for (Input in : inputs) {
            if (Nav(in, &sel_, 2)) sound_.PlayCue("MenuMove");
            if (in != Input::Confirm) continue;
            sound_.PlayCue("MenuSubmit");
            if (sel_ == 0) {
                meta_.AddStat(Stat::StagesRestarted, 1);
                RebuildRun();
                rec_.restartUsed = true;
                StartStage();
            } else { // GameController.EndStageInDefeat
                rec_.restartUsed = true;
                StageOver();
            }
            break;
        }
        break;
    case Screen::Flash:
        if (timer_ >= 3.0f) {
            revealed_ = 0;
            defeat_ ? NextStage(false) : Go(Screen::Results);
        }
        break;
    case Screen::Results:
        if (int n = static_cast<int>((timer_ - 0.4f) / 0.3f); n > revealed_ && revealed_ < 9) { // rows appear one by one
            revealed_ = n;
            sound_.PlayCue(revealed_ >= 9 ? "Results_ShowRank" : "Results_ShowStat");
        }
        for (Input in : inputs)
            if (in == Input::Confirm && timer_ > 0.5f) {
                sound_.PlayCue("MenuSubmit");
                AfterResults();
                break;
            }
        break;
    case Screen::Manage:
        for (Input in : inputs)
            if (screen_ == Screen::Manage) InputManage(in);
        break;
    case Screen::ManageConfirm: // More Upgrades Available: Continue / Back
        for (Input in : inputs) {
            if (Nav(in, &sel_, 2)) sound_.PlayCue("MenuMove");
            if (IsBack(in) || (in == Input::Confirm && sel_ == 1)) {
                sound_.PlayCue("MenuCancel");
                Go(Screen::Manage);
                break;
            }
            if (in == Input::Confirm) {
                sound_.PlayCue("MenuSubmit");
                AfterManage(true);
                break;
            }
        }
        break;
    case Screen::KeepGoing: // Continue Playing / Save & Quit
        for (Input in : inputs) {
            if (Nav(in, &sel_, 2)) sound_.PlayCue("MenuMove");
            if (in != Input::Confirm) continue;
            sound_.PlayCue("MenuSubmit");
            if (sel_ == 0) NextStage(false);
            else if (RecordExists(savePath_)) Go(Screen::Overwrite);
            else NextStage(true);
            break;
        }
        break;
    case Screen::Overwrite: // Overwrite Old Data / Quit Without Saving / Back
        for (Input in : inputs) {
            if (Nav(in, &sel_, 3)) sound_.PlayCue("MenuMove");
            if (IsBack(in) || (in == Input::Confirm && sel_ == 2)) {
                sound_.PlayCue("MenuCancel");
                Go(Screen::KeepGoing, 1);
                break;
            }
            if (in != Input::Confirm) continue;
            sound_.PlayCue("MenuSubmit");
            if (sel_ == 0) NextStage(true);
            else GoToTitle();
            break;
        }
        break;
    case Screen::HowToPlay:
        for (Input in : inputs)
            if (screen_ == Screen::HowToPlay) InputHowToPlay(in);
        break;
    case Screen::Achievements:
        for (Input in : inputs)
            if (screen_ == Screen::Achievements) InputAchievements(in);
        break;
    case Screen::SkullKeys: // UnlockPopupSkullKeys
        for (Input in : inputs)
            if ((in == Input::Confirm || IsBack(in)) && timer_ > 1.0f) {
                sound_.PlayCue("MenuSubmit");
                meta_.unreportedKeys = 0;
                KeepGoingOrQuit();
                break;
            }
        break;
    case Screen::Unlocks: // the title's unlock popups, one at a time
        for (Input in : inputs)
            if ((in == Input::Confirm || IsBack(in)) && timer_ > 0.5f && !unlockPopups_.empty()) {
                sound_.PlayCue("MenuSubmit");
                int u = unlockPopups_.front();
                if (u == kMusicPopup) {
                    for (int p = 1; p < 4; p++) meta_.seenMusicPack[p] = meta_.seenMusicPack[p] || meta_.MusicPackUnlocked(p);
                } else {
                    meta_.seenModeUnlock[u] = true;
                }
                meta_.dirty = true;
                unlockPopups_.erase(unlockPopups_.begin());
                if (unlockPopups_.empty()) {
                    SaveMeta();
                    Go(Screen::Title);
                } else {
                    timer_ = 0;
                }
                break;
            }
        break;
    case Screen::Credits:
        for (Input in : inputs)
            if (in == Input::Menu || IsBack(in) || in == Input::Confirm) {
                sound_.PlayCue("MenuCancel");
                Go(Screen::Title, 5);
                break;
            }
        break;
    case Screen::Intro:
    case Screen::TrialIntro:
        for (Input in : inputs) InputCinematic(in);
        if (screen_ == Screen::Intro && timer_ > 5 * 3.4f + 1.0f) GoToTitle();
        else if (screen_ == Screen::TrialIntro && timer_ > 5.0f) Go(Screen::StageStart, 0);
        break;
    case Screen::Ending:
        for (Input in : inputs)
            if ((in == Input::Confirm || in == Input::Menu) && timer_ > 1.0f) {
                sound_.PlayCue("MenuSubmit");
                GoToTitle();
                break;
            }
        break;
    }

    // the stage keeps playing under the pause menu only while an action finishes
    if (st_.view && (screen_ == Screen::Play || (st_.started && st_.view->Busy()))) {
        sound_.Update(dt);
        st_.view->Update(dt);
        if (screen_ == Screen::Play && !st_.view->Busy() && st_.game->phase() == Phase::StageEnded) StageOver();
    } else {
        sound_.Update(dt);
    }
    if (seasonJingleTimer_ >= 0 && (seasonJingleTimer_ -= dt) < 0) {
        if (screen_ == Screen::Title || screen_ == Screen::Unlocks)
            sound_.PlayJingle(season_ == SeasonalEvent::Halloween ? "jingle.titleHalloween" : "jingle.titleEaster");
    }
    if (trickTimer_ > 0 && screen_ == Screen::Play) trickTimer_ -= dt;
    // the "Achievement Unlocked!" popups, one after another
    if (!meta_.newlyUnlocked.empty() && (toastTimer_ += dt) >= kToastTime) {
        meta_.newlyUnlocked.pop_front();
        toastTimer_ = 0;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------
namespace {

// the data format this build reads (tools/prepare_data.py DATA_FORMAT); older data is refused
constexpr int kDataFormat = 1;

// version.json's "format" (0: no version.json, data made before the installer)
int DataFormat(const std::string& dataDir) {
    std::ifstream f(dataDir + "/version.json", std::ios::binary);
    if (!f) return 0;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    return j.is_object() ? j.value("format", 0) : 0;
}

} // namespace

int RunGame(int argc, char** argv) {
    std::string dataDir = platform::DataDir(), shotPath, script, metaFile;
    Date date;
    bool fixedDate = false;
    float shotTime = 0, speed = 1;
    bool autoPlay = false, intro = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 2 < argc) {
            shotPath = argv[++i];
            shotTime = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--speed" && i + 1 < argc) {
            speed = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--inputs" && i + 1 < argc) {
            script = argv[++i];
        } else if (a == "--date" && i + 1 < argc) { // YYYY-MM-DD
            if (std::sscanf(argv[++i], "%d-%d-%d", &date.year, &date.month, &date.day) == 3) fixedDate = true;
        } else if (a == "--meta" && i + 1 < argc) {
            metaFile = argv[++i];
        } else if (a == "--auto") {
            autoPlay = true;
        } else if (a == "--intro") { // the boot cutscene even in automated runs (screenshots)
            intro = true;
        } else {
            pos.push_back(a);
        }
    }
    if (!pos.empty()) dataDir = pos[0];

    int ww = 0, wh = 0;
    std::string err;
    if (!platform::Init(&ww, &wh, &err)) {
        std::fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    Renderer r;
    r.Init(ww, wh);

    // missing or outdated data: say what to do (a message box, except in automated runs) and quit
    bool testing = !shotPath.empty() || !script.empty() || autoPlay;
    auto fail = [testing](const std::string& msg) {
        std::fprintf(stderr, "%s\n", msg.c_str());
        if (!testing) platform::ShowError(msg);
        platform::Shutdown();
        return 1;
    };
    // the user's own game copied to the console: the data is built from it on the first start (and
    // again when a new version of the port exports more)
    std::string gameCopy = platform::GameCopyDir();
    if (!testing && pos.empty() && !gameCopy.empty() && install::HasGameCopy(gameCopy) &&
        install::NeedsInstall(platform::SaveDir())) {
        bool ok = install::Install(gameCopy, platform::SaveDir(), platform::FontDir(),
                                   [](const char* what, float f) { platform::ShowProgress(what, f); }, &err);
        platform::EndProgress();
        if (!ok) return fail("Installing the game data from " + gameCopy + " failed:\n" + err);
        dataDir = platform::DataDir();
    }
    std::string howTo = gameCopy.empty()
                            ? "Run instalar.bat on a PC with your Steam copy of Dungeon Deathball."
                            : "Copy the \"Dungeon Deathball_Data\" folder and UnityPlayer.dll from your Steam copy of "
                              "Dungeon Deathball into " + gameCopy + "/ and start the game again.";
    GameData data;
    Assets assets;
    if (!data.Load(dataDir + "/gamedata.json", &err) || !assets.Load(dataDir + "/render", &r, &err))
        return fail("The game data was not found (" + err + ").\n\n" + howTo);
    if (int format = DataFormat(dataDir); format < kDataFormat)
        return fail("The game data is from an older version of the port (format " + std::to_string(format) + ", this one needs " +
                    std::to_string(kDataFormat) + ").\n\n" + howTo);

    audio::Mixer mixer;
    audio::Sound sound;
    if (!sound.Load(dataDir + "/audio", &mixer, &err)) std::fprintf(stderr, "no audio: %s\n", err.c_str());
    else platform::StartAudio(&mixer);

    auto app = std::make_unique<App>(r, assets, data, sound, mixer, dataDir);
    app->autoPlay = autoPlay;
    app->fixedDate = fixedDate;
    app->date = date;
    app->testing = testing;
    if (!app->testing) app->LoadMeta();
    else if (!metaFile.empty()) app->LoadMeta(metaFile); // read only: testing never saves
    // a seed on the command line goes straight into a run (testing); otherwise the title screen
    std::string mode = pos.size() > 1 ? pos[1] : "";
    if (mode == "daily") {
        app->NewRun(GameMode::DailyChallenge, 0, 0);
    } else if (mode == "continue") {
        if (!app->ContinueRun()) app->GoToTitle();
    } else if (!mode.empty()) {
        app->NewRun(pos.size() > 2 ? static_cast<GameMode>(std::atoi(pos[2].c_str())) : GameMode::Normal,
                    pos.size() > 3 ? std::atoi(pos[3].c_str()) : 0, std::atoi(mode.c_str()));
    } else {
        app->StartIntro(intro);
    }

    double last = platform::Seconds();
    size_t scriptPos = 0;
    float scriptTimer = 0.5f;
    for (;;) {
        double now = platform::Seconds();
        float realDt = static_cast<float>(now - last);
        last = now;
        float dt = std::min(realDt, 0.1f) * speed;

        std::vector<Input> inputs;
        bool alive = platform::Poll(&inputs, &ww, &wh);
        r.Resize(ww, wh);
        if (!script.empty() && (scriptTimer -= dt) <= 0 && scriptPos < script.size()) {
            scriptTimer = 0.3f;
            Input in = ScriptKey(script[scriptPos++]);
            if (in != Input::None) inputs.push_back(in);
        }
        if (!alive) inputs.push_back(Input::Quit);

        double cpuStart = platform::Seconds();
        bool keep = app->Frame(inputs, dt);
        app->Draw();
        r.Flush();
        bool quit = !keep;
        if (!shotPath.empty() && app->clock >= shotTime) {
            platform::SaveScreenshot(shotPath, ww, wh);
            std::printf("screenshot %s\n", shotPath.c_str());
            quit = true;
        }
        app->perf.Add(realDt, static_cast<float>(platform::Seconds() - cpuStart));
        platform::Swap();
        if (quit) break;
    }
    platform::StopAudio();
    app.reset();
    platform::Shutdown();
    return 0;
}

} // namespace dd::app
