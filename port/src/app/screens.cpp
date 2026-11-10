// Drawing of the game's screens: the stage and its HUD, the title and new game menus, the stage start
// panel, pause, options, results, team management and the ending. Texts come from the game's own
// tables (data/loc/en.json); layouts are the port's, sized for 960x544.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
using namespace dd::gfx;
using ui::Align;
namespace col = ui::col;

namespace {

const uint32_t kBackground = col::Rgb(20, 12, 28);

std::string Upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

const char* RankLetter(int rank) {
    static const char* const r[] = {"-", "F", "C", "B", "A", "S"};
    return rank >= 0 && rank <= 5 ? r[rank] : "-";
}

std::string StripNewlines(std::string s) {
    for (char& c : s)
        if (c == '\n') c = ' ';
    return s;
}

std::string RankSprite(int rank) { return rank >= 1 && rank <= 5 ? std::string("Icons_Rank") + RankLetter(rank) : std::string(); }

} // namespace

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------
App::~App() {
    ClearPortraits();
    ClearHelpSprites();
}

const RuntimeSprite& App::Portrait(const HumanPlayer& hp) {
    auto it = portraits_.find(hp.playerNumber);
    if (it == portraits_.end()) {
        const HumanPlayerSeed& s = hp.seed;
        it = portraits_.emplace(hp.playerNumber, assets_.MakePlayerSprite(s.isMale, s.playerClassConfig->playerClass, s.bodyTexture,
                                                                           s.hairTexture, "Plain")).first;
    }
    return it->second;
}

void App::ClearPortraits() {
    for (auto& [k, s] : portraits_) r_.Free(s.tex);
    portraits_.clear();
}

void App::ClearHelpSprites() {
    for (auto& [k, s] : helpSprites_) r_.Free(s.tex);
    helpSprites_.clear();
}

std::string App::ModeName(GameMode m) const {
    switch (m) {
    case GameMode::DailyChallenge: return T("General/GameMode_Daily", "Daily Challenge");
    case GameMode::Boss: return T("General/GameMode_Boss", "Judgment Mode");
    case GameMode::Action: return T("General/GameMode_Action", "Action Mode");
    default: return T("General/GameMode_Normal", "Normal Mode");
    }
}

std::string App::TeamName(int team) const {
    if (team < 0 || team >= static_cast<int>(data_.teams.size())) return "";
    const TeamConfig* t = data_.teams[static_cast<size_t>(team)];
    return t->teamName.empty() ? t->name : t->teamName;
}

std::string App::PerkName(const PerkChain* chain) const {
    if (!chain) return T("General/NoneString", "None");
    return chain->chainName.empty() ? chain->name : chain->chainName;
}

std::string App::ClassName(const PlayerClassConfig* c) const {
    if (!c) return "";
    switch (c->playerClass) {
    case PlayerClass::Carrier: return T("General/Class_Carrier", "Carrier");
    case PlayerClass::Runner: return T("General/Class_Runner", "Runner");
    case PlayerClass::Brawler: return T("General/Class_Brawler", "Brawler");
    default: return c->className;
    }
}

std::string App::ButtonLabel(Input in) const { return platform::ButtonName(in); }

namespace {
// A row of button hints along the bottom: the game's button sprite, else the button's name.
void Hints(ui::Ui& u, const std::vector<std::pair<Input, std::string>>& hints) {
    float lh = u.LineHeight(ui::Small);
    float total = 0;
    for (const auto& [in, label] : hints) total += lh + 6 + u.Width(ui::Small, label) + 14;
    float x = std::floor((u.W() - total) * 0.5f), y = u.H() - lh - 8;
    u.Fill(0, y - 5, u.W(), lh + 10, PackColor(0, 0, 0, 0.6f));
    for (const auto& [in, label] : hints) {
        x += u.Prompt(x, y, platform::ButtonName(in), platform::ButtonSprite(in)) + 2;
        x += u.Text(ui::Small, x, y, col::White, label) + 14;
    }
}

// A popup with a header strip, the body and a column of buttons.
float PopupHeight(ui::Ui& u, const std::string& body, float w, size_t buttons) {
    float bh = body.empty() ? 0 : static_cast<float>(u.Wrap(ui::Small, body, w - 24).size()) * (u.LineHeight(ui::Small) + 2) + 10;
    return 34 + bh + static_cast<float>(buttons) * 26 + 10;
}
} // namespace

void App::DrawConfirm(const std::string& header, const std::string& body, const std::vector<std::string>& options, int sel) {
    ui_.Shade(0.55f);
    float w = std::min(360.0f, ui_.W() - 40), h = PopupHeight(ui_, body, w, options.size());
    float x = std::floor((ui_.W() - w) * 0.5f), y = std::floor((ui_.H() - h) * 0.5f);
    ui_.Panel(x, y, w, h);
    ui_.Fill(x + 1, y + 1, w - 2, 26, col::Rgb(115, 41, 48));
    ui_.Text(ui::Medium, x + w * 0.5f, y + 6, col::White, header, Align::Center);
    float cy = y + 34;
    if (!body.empty()) cy += ui_.Paragraph(ui::Small, x + 12, cy, w - 24, col::Grey, body, Align::Center) + 10;
    for (size_t i = 0; i < options.size(); i++, cy += 26)
        ui_.Button(x + 30, cy, w - 60, 22, options[i], static_cast<int>(i) == sel);
}

// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------
void App::Draw() {
    bool stageBehind = st_.view && screen_ != Screen::Title && screen_ != Screen::Modes && screen_ != Screen::Teams && screen_ != Screen::NewGame &&
                       screen_ != Screen::Achievements && screen_ != Screen::Unlocks && screen_ != Screen::Intro &&
                       screen_ != Screen::TrialIntro && screen_ != Screen::Credits &&
                       !(screen_ == Screen::HowToPlay && helpReturn_ == Screen::Title) &&
                       !(screen_ == Screen::Options && optionsReturn_ == Screen::Title);
    if (stageBehind) {
        DrawStage();
    } else {
        r_.Begin2D();
        r_.Fill(0, 0, static_cast<float>(r_.outW()), static_cast<float>(r_.outH()), kBackground);
    }
    ui_.Begin();
    switch (screen_) {
    case Screen::Title: DrawTitle(); break;
    case Screen::Modes: DrawModes(); break;
    case Screen::Teams: DrawTeams(); break;
    case Screen::NewGame:
        if (titleUi_.loaded() && DrawNewGameCanvas())
            Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
        else
            DrawModes();
        break;
    case Screen::StageStart: DrawHud(); DrawStageStart(); break;
    case Screen::Play: DrawHud(); break;
    case Screen::Pause: DrawHud(); DrawPause(); break;
    case Screen::PauseConfirm:
        DrawHud();
        if (gameUi_.loaded() && DrawPauseConfirmCanvas()) {
            Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("Gameplay/Pause_QuitConfirm_Stay", "Stay")}});
        } else if (sel2_ == 0)
            DrawConfirm(T("General/Quit_Q", "Quit?"), T("Gameplay/Pause_QuitConfirm_Desc", "If you quit you will lose all progress in this run."),
                        {T("General/Quit", "Quit"), T("Gameplay/Pause_QuitConfirm_Stay", "Stay")}, sel_);
        else
            DrawConfirm(T("Gameplay/PauseRestartStage_Q", "Restart Stage?"), T("Gameplay/PauseRestartStage_Desc", ""),
                        {T("Gameplay/Pause_RestartStage", "Restart Stage"), T("Gameplay/Pause_QuitConfirm_Stay", "Stay")}, sel_);
        break;
    case Screen::Options:
        if (stageBehind) DrawHud();
        DrawOptions();
        break;
    case Screen::Defeat: DrawHud(); DrawDefeat(); break;
    case Screen::Flash: DrawHud(); DrawFlash(); break;
    case Screen::Results: DrawResults(); break;
    case Screen::Manage: DrawManage(); break;
    case Screen::ManageConfirm:
        DrawManage();
        DrawConfirm(T("Gameplay/Upgrades_ContinueConfirm_Hdr", "More Upgrades Available"),
                    T("Gameplay/Upgrades_ContinueConfirm", "You have unspent Gold which can be used to upgrade or recruit players."),
                    {T("General/MenuNav_Continue", "Continue"), T("General/MenuNav_Back", "Back")}, sel_);
        break;
    case Screen::KeepGoing: DrawKeepGoing(); break;
    case Screen::Overwrite: DrawOverwrite(); break;
    case Screen::Ending: DrawEnding(); break;
    case Screen::HowToPlay: DrawHowToPlay(); break;
    case Screen::Achievements: DrawAchievements(); break;
    case Screen::SkullKeys: DrawSkullKeys(); break;
    case Screen::Unlocks:
        if (!titleUi_.loaded()) DrawTitle();
        DrawUnlocks();
        break;
    case Screen::Intro: DrawIntro(); break;
    case Screen::TrialIntro: DrawTrialIntro(); break;
    case Screen::Credits:
        if (!titleUi_.loaded() || !DrawTitleCanvas("MenuPage_Credits")) DrawTitle();
        break;
    }
    DrawToast();
    DrawDebug();
}

// ---------------------------------------------------------------------------------------------
// The stage
// ---------------------------------------------------------------------------------------------
void App::DrawStage() {
    Game& g = *st_.game;
    bool idle = !st_.view->Busy();
    bool showCursor = screen_ == Screen::Play && idle && g.phase() == Phase::PlayerInput;
    st_.view->Draw(showCursor ? &st_.cursor : nullptr, st_.commands);
    r_.EndScene();
    st_.view->DrawOverlay();
}

void App::DrawHud() {
    if (!st_.game) return;
    Game& g = *st_.game;
    Run& run = *run_;
    const float W = ui_.W(), H = ui_.H();
    // (the stage start panel has the screen to itself: GameplayUI shows once the stage is played)
    // the stage end: StageEndUI's own canvas over the field, no HUD
    if ((screen_ == Screen::Flash || screen_ == Screen::Defeat) && gameUi_.loaded()) return;
    bool menus = screen_ == Screen::Pause || screen_ == Screen::PauseConfirm || screen_ == Screen::Options;
    bool canvas = screen_ == Screen::StageStart ? gameUi_.loaded() : (screen_ == Screen::Play || (menus && st_.started)) && DrawHudCanvas();
    if (canvas && menus) return;
    if (!canvas) {
    // top bar: stage, turn / par, gold
    ui_.Fill(0, 0, W, 22, PackColor(0, 0, 0, 0.55f));
    ui_.Text(ui::Medium, 8, 3, col::White, StageName());
    std::string turn = g.Turn() > g.TurnPar() ? T("Gameplay/Turns_OverPar", "Over Par")
                                               : ui::Format(T("Gameplay/Turns_CurrentOfPar", "Turn {0.turn} / Par {1.par}"),
                                                            {{"turn", ui::Int(std::max(1, g.Turn()))}, {"par", ui::Int(g.TurnPar())}});
    if (g.Turn() == g.TurnPar()) turn = T("Gameplay/Turns_LastTurn", "Last Turn");
    ui_.Text(ui::Medium, W * 0.5f, 3, g.Turn() >= g.TurnPar() ? col::Orange : col::White, turn, Align::Center);
    float gx = W - 8;
    gx -= ui_.Text(ui::Medium, gx, 3, col::Yellow, std::to_string(run.gold), Align::Right) + 4;
    ui_.Sprite("Icons_Gold_wOutline", gx - 16, 3, 16, 16);
    if (screen_ != Screen::Play && screen_ != Screen::StageStart) return;

    // the current player's panel (PlayerInfoPanel)
    if (Player* p = g.currentPlayer; p && screen_ == Screen::Play) {
        float x = 6, y = 28, w = 170, h = 64;
        ui_.Panel(x, y, w, h, col::Rgb(27, 38, 50, 0.85f));
        const RuntimeSprite& face = Portrait(*p->humanPlayer);
        ui_.Fill(x + 4, y + 4, 40, 40, col::Rgb(20, 12, 28));
        ui_.SpriteTex(face.tex, face.def, x + 4, y + 4, 40, 40);
        ui_.Text(ui::Medium, x + 50, y + 4, col::White, p->Name());
        ui_.Sprite(std::string("Icons_Class_") + ToString(p->humanPlayer->playerClassConfig->playerClass), x + w - 20, y + 4, 16, 16);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s %d", T("Gameplay/Health", "Health").c_str(), p->health);
        ui_.Text(ui::Small, x + 50, y + 22, col::Red, buf);
        std::snprintf(buf, sizeof buf, "%s %d/%d", T("Gameplay/Stamina", "Stamina").c_str(), p->stamina, p->maxStamina());
        ui_.Text(ui::Small, x + 50, y + 34, p->stamina <= 2 ? col::Orange : col::Green, buf);
        std::snprintf(buf, sizeof buf, "%s %d", T("Gameplay/ActionPointsShort", "AP").c_str(), p->actionPoints);
        ui_.Text(ui::Small, x + 4, y + 48, col::Blue, buf);
        if (p->isHoldingBall()) ui_.Text(ui::Small, x + 50, y + 48, col::Yellow, T("General/Ball", "Ball"));
    }

    }
    // command hints, with the cost of the command under the cursor
    if (screen_ == Screen::Play && !st_.view->Busy() && g.phase() == Phase::PlayerInput) {
        std::vector<std::pair<Input, std::string>> hints;
        auto add = [&](Input in, bool (*pred)(const PlayerActionConfig*), const char* key, const char* fb) {
            for (const Command& c : st_.commands)
                if (!c.onTile && pred(c.actionConfig)) {
                    std::string t = T(key, fb);
                    if (c.staminaCost) t += " (" + std::to_string(c.staminaCost) + ")";
                    if (!c.interactable || c.tooExpensive) t = "<color=#656d71>" + t + "</color>";
                    hints.emplace_back(in, t);
                    return;
                }
        };
        add(Input::PassTake, [](const PlayerActionConfig* a) { return a->action == ActorAction::Player_Pass; }, "Gameplay/Action_Pass", "Pass");
        add(Input::PassTake, [](const PlayerActionConfig* a) { return a->action == ActorAction::Player_TakeBall; }, "Gameplay/Action_Take", "Take");
        add(Input::Shoot, [](const PlayerActionConfig* a) { return a->action == ActorAction::Player_Shoot; }, "Gameplay/Action_Shoot", "Shoot");
        add(Input::Rest, [](const PlayerActionConfig* a) { return a->action == ActorAction::Char_Wait; }, "Gameplay/Action_Rest", "Rest");
        add(Input::Undo, [](const PlayerActionConfig* a) { return a->undo; }, "Gameplay/Action_Undo", "Undo");
        for (const Command& c : st_.commands)
            if (c.actionConfig->switchControl) {
                hints.emplace_back(Input::Switch, T("Gameplay/Action_SwitchPlayer", "Switch Player"));
                break;
            }
        hints.emplace_back(Input::Info, T("Gameplay/Help", "Help"));
        Hints(ui_, hints);
        for (const Command& c : st_.commands)
            if (c.onTile && c.tile == st_.cursor) {
                std::string what;
                switch (c.actionConfig->action) {
                case ActorAction::Char_Step: what = T("Gameplay/Action_Move", "Move"); break;
                case ActorAction::Char_Attack: what = T("Gameplay/Action_Attack", "Attack"); break;
                case ActorAction::Player_Pass: what = T("Gameplay/Action_Pass", "Pass"); break;
                case ActorAction::Player_Shoot: what = T("Gameplay/Action_Shoot", "Shoot"); break;
                case ActorAction::Player_TakeBall: what = T("Gameplay/Action_Take", "Take"); break;
                default: what = c.actionConfig->switchControl ? T("Gameplay/Action_SwitchPlayer", "Switch Player") : ""; break;
                }
                if (c.staminaCost) what += "  " + T("Gameplay/Stamina", "Stamina") + " " + std::to_string(c.staminaCost);
                float y = H - ui_.LineHeight(ui::Small) - 30;
                float x = ui_.Prompt(8, y, ButtonLabel(Input::Confirm), platform::ButtonSprite(Input::Confirm)) + 10;
                ui_.Text(ui::Small, x, y, c.interactable && !c.tooExpensive ? col::White : col::Grey, what);
            }
    }

    // SuggestUndo popup
    if (screen_ == Screen::Play && !st_.view->Busy() && g.phase() == Phase::SuggestUndo)
        DrawConfirm(T("Gameplay/SuggestUndo_Hdr", "Undo Actions?"), T("Gameplay/SuggestUndo_Body", ""),
                    {T("Gameplay/Action_Undo", "Undo"), T("Gameplay/SuggestUndo_Cancel", "Ignore")}, sel2_);

    // Halloween TRICK! / TREAT! (SeasonalEventGameplayPopup)
    if (trickTimer_ > 0 && screen_ == Screen::Play) {
        static const char* const keys[] = {"", "Halloween_Body_EnemiesHealedFully", "Halloween_Body_EnemiesHealedSmall",
                                           "Halloween_Body_EnemiesHurtSmall", "Halloween_Body_EnemiesStunned",
                                           "Halloween_Body_StaminaDrained", "Halloween_Body_StaminaDrainedAll",
                                           "Halloween_Body_StaminaReplenished", "Halloween_Body_StaminaReplenishedAll"};
        std::string head = trickIsTrick_ ? T("Gameplay/Halloween_Header_Trick", "TRICK!") : T("Gameplay/Halloween_Header_Treat", "TREAT!");
        std::string body = trickEvent_ > 0 && trickEvent_ < 9 ? T(std::string("Gameplay/") + keys[trickEvent_], "") : "";
        float w = 260, x = (W - w) * 0.5f, y = H * 0.22f;
        ui_.Panel(x, y, w, 50, col::Rgb(20, 12, 28, 0.9f), trickIsTrick_ ? col::Purple : col::Orange);
        ui_.Text(ui::Large, W * 0.5f, y + 4, trickIsTrick_ ? col::Purple : col::Orange, head, Align::Center);
        ui_.Text(ui::Small, W * 0.5f, y + 32, col::White, body, Align::Center);
    }
    if (showInfo_ && screen_ == Screen::Play) DrawInfoPopup();
}

// StageStartPanel: header, ball choice, the team and Play Deathball
void App::DrawStageStart() {
    if (gameUi_.loaded() && DrawStageStartCanvas()) {
        std::vector<std::pair<Input, std::string>> hints = {{Input::Confirm, T("General/MenuNav_Select", "Select")}};
        hints.emplace_back(Input::Switch, T("Gameplay/StageStart_SwapPlayers", "Swap Positions"));
        if (run_->activePlayers.size() == 2) hints.emplace_back(Input::PassTake, T("Gameplay/StageStart_SwapBall", "Swap Ball"));
        hints.emplace_back(Input::Menu, T("Gameplay/Pause", "Pause"));
        Hints(ui_, hints);
        return;
    }
    Run& run = *run_;
    const float W = ui_.W(), H = ui_.H();
    float w = std::min(420.0f, W - 40), h = 230, x = std::floor((W - w) * 0.5f), y = std::floor((H - h) * 0.5f);
    ui_.Panel(x, y, w, h);
    ui_.Fill(x + 1, y + 1, w - 2, 26, col::Rgb(115, 41, 48));
    std::string header = ui::Format(T("Gameplay/StartStage_Header", "{0.stageNumber} - {1.gameMode}"),
                                    {{"stageNumber", StageName()}, {"gameMode", ModeName(rec_.mode)}});
    ui_.Text(ui::Medium, x + w * 0.5f, y + 6, col::White, header, Align::Center);
    ui_.Text(ui::Small, x + w * 0.5f, y + 32, col::Grey,
             ui::Format(T("Gameplay/TrialCount", "Trial {0.value} of {1.maxValue}"), {{"value", ui::Int(run.currentTrial + 1)}, {"maxValue", "4"}}),
             Align::Center);

    // ball (BallMods): Easy / Normal / Hard
    float by = y + 52;
    const BallModifierConfig* bm = run.BallModConfig();
    bool canSwap = !run.team->traitBlockBallMods();
    ui_.Text(ui::Small, x + 14, by, col::Yellow, T("Gameplay/SelectBall", "Select Ball"));
    const char* diff[3] = {"Gameplay/BallMod_Diff_Easy", "Gameplay/BallMod_Diff_Normal", "Gameplay/BallMod_Diff_Hard"};
    const char* diffFb[3] = {"Easy", "Normal", "Hard"};
    int cur = run.ballEffect == DifficultyEffect::Easier ? 0 : run.ballEffect == DifficultyEffect::Neutral ? 1 : 2;
    for (int i = 0; i < 3; i++) {
        float bx = x + 14 + i * ((w - 28) / 3);
        bool on = i == cur;
        ui_.Panel(bx, by + 14, (w - 28) / 3 - 6, 20, on ? col::Rgb(190, 38, 51, 0.95f) : col::Rgb(27, 38, 50, 0.9f),
                  on ? col::White : col::DarkGrey);
        ui_.Text(ui::Small, bx + ((w - 28) / 3 - 6) * 0.5f, by + 18, canSwap || on ? col::White : col::DarkGrey, T(diff[i], diffFb[i]), Align::Center);
    }
    if (bm) {
        ui_.Sprite(bm->ballSprite, x + 14, by + 42, 24, 24);
        std::string name = ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", bm->friendlyName}});
        ui_.Text(ui::Medium, x + 44, by + 42, col::White, name);
        std::string desc = bm->description;
        if (desc.find('{') != std::string::npos) desc = ui::Format(desc, {{"value", ui::Int(bm->value)}});
        ui_.Paragraph(ui::Small, x + 44, by + 60, w - 58, col::Grey, desc);
    }
    // the team
    float py = y + 140;
    float cw = (w - 28) / 2;
    for (size_t i = 0; i < run.activePlayers.size() && i < 2; i++) {
        const HumanPlayer& hp = *run.activePlayers[i];
        float px = x + 14 + static_cast<float>(i) * cw;
        const RuntimeSprite& face = Portrait(hp);
        ui_.Fill(px, py, 32, 32, col::Rgb(20, 12, 28));
        ui_.SpriteTex(face.tex, face.def, px, py, 32, 32);
        ui_.Text(ui::Small, px + 36, py + 2, col::White, hp.playerName);
        ui_.Text(ui::Small, px + 36, py + 16, col::Grey, ClassName(hp.playerClassConfig) + " - " + PerkName(hp.perkChain));
        if (static_cast<int>(i) == run.ballCarrierIndex) ui_.Sprite(bm ? bm->ballSprite : "", px + 20, py + 18, 14, 14);
    }
    ui_.Button(x + w * 0.5f - 90, y + h - 36, 180, 24, T("Gameplay/StageStart_Play", "Play Deathball"), true);
    std::vector<std::pair<Input, std::string>> hints = {{Input::Confirm, T("Gameplay/StageStart_Play", "Play Deathball")}};
    if (canSwap) hints.emplace_back(Input::Right, T("Gameplay/StageStart_SwapBall", "Swap Ball"));
    hints.emplace_back(Input::Switch, T("Gameplay/StageStart_SwapPlayers", "Swap Positions"));
    if (run.activePlayers.size() == 2) hints.emplace_back(Input::PassTake, T("General/Ball", "Ball"));
    hints.emplace_back(Input::Menu, T("Gameplay/Pause", "Pause"));
    Hints(ui_, hints);
}

void App::DrawPause() {
    if (gameUi_.loaded() && DrawPauseCanvas())
        return Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/Resume", "Resume")}});
    ui_.Shade(0.5f);
    const float W = ui_.W(), H = ui_.H();
    float w = 240, h = 5 * 28 + 50, x = std::floor((W - w) * 0.5f), y = std::floor((H - h) * 0.5f);
    ui_.Panel(x, y, w, h);
    ui_.Text(ui::Large, W * 0.5f, y + 8, col::White, T("Gameplay/Paused", "Paused"), Align::Center);
    std::string items[4] = {T("General/Resume", "Resume"), T("General/Options", "Options"), T("Gameplay/Pause_RestartStage", "Restart Stage"),
                            T("General/Quit", "Quit")};
    for (int i = 0; i < 4; i++) {
        bool enabled = i != 2 || (RestartAllowed() && st_.started);
        ui_.Button(x + 20, y + 44 + i * 28, w - 40, 24, items[i], sel_ == i, enabled);
    }
    Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/Resume", "Resume")}});
}

void App::DrawOptions() {
    if (DrawOptionsCanvas()) {
        if (optionsPage_ == 2)
            return Hints(ui_, {{Input::Right, "-/+"}, {Input::Back, T("General/MenuNav_Back", "Back")}});
        return Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
    }
    constexpr int kOptionRows = 8;
    ui_.Shade(0.6f);
    const float W = ui_.W(), H = ui_.H();
    float w = 380, h = kOptionRows * 28 + 56, x = std::floor((W - w) * 0.5f), y = std::floor((H - h) * 0.5f);
    ui_.Panel(x, y, w, h);
    ui_.Text(ui::Large, W * 0.5f, y + 8, col::White, T("General/Options", "Options"), Align::Center);
    const char* keys[5] = {"General/Setting_Volume_Master", "General/Setting_Volume_Music", "General/Setting_Volume_Jingles",
                           "General/Setting_Volume_Sounds", "General/Setting_Volume_Voices"};
    const char* fb[5] = {"Master", "Music", "Jingles", "Sounds", "Voices"};
    for (int i = 0; i < kOptionRows; i++) {
        float ry = y + 46 + i * 28;
        bool on = sel_ == i;
        ui_.Panel(x + 14, ry, w - 28, 24, on ? col::Rgb(190, 38, 51, 0.95f) : col::Rgb(27, 38, 50, 0.9f), on ? col::White : col::DarkGrey);
        if (i < 5) {
            ui_.Text(ui::Medium, x + 24, ry + 4, col::White, T(keys[i], fb[i]));
            float v = *Volume(i);
            int steps = static_cast<int>(std::round(v * 10));
            for (int k = 0; k < 10; k++) ui_.Fill(x + w - 150 + k * 12, ry + 7, 9, 10, k < steps ? col::Yellow : col::DarkGrey);
        } else if (i == 5 || i == 6) {
            bool v = i == 5 ? settings_.suggestUndo : settings_.autoSkipCinematics;
            ui_.Text(ui::Medium, x + 24, ry + 4, col::White,
                     i == 5 ? T("General/Setting_SuggestUndo", "Suggest Undo") : T("General/Setting_AutoSkipCinematics", "Auto Skip Cinematics"));
            ui_.Text(ui::Medium, x + w - 30, ry + 4, v ? col::Green : col::Grey, v ? "ON" : "OFF", Align::Right);
        } else {
            ui_.Text(ui::Medium, x + w * 0.5f, ry + 4, col::White, T("General/Setting_RestoreDefaults", "Restore Defaults"), Align::Center);
        }
    }
    Hints(ui_, {{Input::Right, "-/+"}, {Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
}

void App::DrawDefeat() {
    if (gameUi_.loaded() && DrawDefeatCanvas()) return Hints(ui_, {{Input::Confirm, T("General/MenuNav_Select", "Select")}});
    DrawConfirm(T("Gameplay/Defeat_YouLose", "You Lose"), T("Gameplay/StageEnd_Defeat_RestartInfo", "You can restart each stage once in this mode."),
                {T("Gameplay/Pause_RestartStage", "Restart Stage"), T("Gameplay/StageEnd_Defeat_GiveUp", "Give Up")}, sel_);
}

// StageEndUI: "Touchdown!" / "Made It!" / "You Lose" over the field
void App::DrawFlash() {
    if (gameUi_.loaded() && DrawFlashCanvas()) return;
    const float W = ui_.W(), H = ui_.H();
    float k = std::min(1.0f, timer_ / 0.25f);
    ui_.Fill(0, H * 0.38f, W, 54 * k, PackColor(0, 0, 0, 0.6f));
    if (k >= 1) ui_.Text(ui::Large, W * 0.5f, H * 0.38f + 14, defeat_ ? col::Red : col::Yellow, Upper(flashText_), Align::Center);
}

// StageResultsScreen: the stage's goals one row at a time, then the rank
void App::DrawResults() {
    if (st_.view) DrawStage(), ui_.Begin();
    if (gameUi_.loaded()) DrawBlackFade();
    if (gameUi_.loaded() && DrawResultsCanvas()) {
        Hints(ui_, {{Input::Confirm, T("General/MenuNav_Continue", "Continue")}});
        return;
    }
    ui_.Shade(0.55f);
    Game& g = *st_.game;
    Run& run = *run_;
    const StageResults& res = g.results();
    const ScoringConfig& sc = data_.scoring;
    const float W = ui_.W(), H = ui_.H();
    float w = std::min(440.0f, W - 40), h = 300, x = std::floor((W - w) * 0.5f), y = std::floor((H - h) * 0.5f) - 8;
    ui_.Panel(x, y, w, h);
    ui_.Fill(x + 1, y + 1, w - 2, 26, col::Rgb(115, 41, 48));
    ui_.Text(ui::Medium, x + w * 0.5f, y + 6, col::White,
             ui::Format(T("Gameplay/Results_Header", "Stage {0.trial}-{1.stage} Results"),
                        {{"trial", ui::Int(run.currentTrial + 1)}, {"stage", ui::Int(run.currentStage + 1)}}),
             Align::Center);
    FieldSize size = run.CurrentStage().fieldSize;
    int speedTarget = size == FieldSize::Small ? sc.parBonusRankTargetSmall : size == FieldSize::Medium ? sc.parBonusRankTargetMedium : sc.parBonusRankTargetLarge;
    struct Row {
        std::string label, value;
        bool ok;
    };
    std::vector<Row> rows = {
        {T("Gameplay/Results_StageComplete", "Stage Complete"), "+" + std::to_string(sc.pointsStageComplete), res.complete},
        {T("Gameplay/Results_Touchdown", "Touchdown"), res.touchdown ? "+" + std::to_string(sc.pointsTouchdown) : "-", res.touchdown},
        {T("Gameplay/Results_WithinPar", "Within Par"), res.withinPar() ? "+" + std::to_string(sc.pointsWithinPar) : "-", res.withinPar()},
        {T("Gameplay/Results_SpeedBonus", "Speed Bonus"),
         ui::Format(T("Gameplay/Results_SpeedBonusTarget", "{0.value} Below Par (target {1.value})"),
                    {{"value", ui::Int(std::max(0, -res.parDifference))}, {"value2", ui::Int(speedTarget)}}),
         res.speedTarget},
        {T("Gameplay/Results_GemBonus", "Gem Bonus"), std::to_string(g.gems), res.gemTarget},
        {T("Gameplay/BallMultiplier", "Ball Multiplier"), run.ballEffect == DifficultyEffect::Harder ? T("Gameplay/BallMod_Diff_Hard", "Hard")
                                                          : run.ballEffect == DifficultyEffect::Easier ? T("Gameplay/BallMod_Diff_Easy", "Easy")
                                                                                                       : T("Gameplay/BallMod_Diff_Normal", "Normal"),
         true},
        {T("Gameplay/Results_StageScore", "Stage Score"), std::to_string(res.score), true},
        {T("Gameplay/Results_StageGold", "Stage Gold"), "+" + std::to_string(stageGold_), true},
    };
    float ry = y + 36;
    for (int i = 0; i < static_cast<int>(rows.size()) && i < revealed_ - 3; i++, ry += 22) {
        const Row& r = rows[static_cast<size_t>(i)];
        ui_.Text(ui::Medium, x + 18, ry, r.ok ? col::White : col::DarkGrey, r.label);
        ui_.Text(ui::Medium, x + w - 18, ry, r.ok ? (i == 7 ? col::Yellow : col::Green) : col::DarkGrey, r.value, Align::Right);
    }
    if (revealed_ >= 10) {
        ui_.Text(ui::Medium, x + 18, y + h - 54, col::White, T("Gameplay/Results_Rank", "Rank"));
        if (!ui_.Sprite(RankSprite(res.rank), x + w - 70, y + h - 64, 48, 48))
            ui_.Text(ui::Large, x + w - 40, y + h - 58, col::Yellow, RankLetter(res.rank), Align::Center);
        if (run.currentStageIsLastInCurrentTrial())
            ui_.Text(ui::Small, x + w * 0.5f, y + h - 20, col::Yellow,
                     ui::Format(T("Gameplay/Results_TrialComplete", "Trial {0.value} of {1.maxValue} Complete!"),
                                {{"value", ui::Int(run.currentTrial + 1)}, {"maxValue", "4"}}),
                     Align::Center);
    }
    Hints(ui_, {{Input::Confirm, T("General/MenuNav_Continue", "Continue")}});
}

// One active player's column on the Manage Team screen (PlayerLevelUpPanel)
void App::DrawPlayerCard(const HumanPlayer& hp, float x, float y, float w, bool selected, int selRow) {
    float h = 200;
    ui_.Panel(x, y, w, h, col::Rgb(27, 38, 50, 0.92f), selected ? col::White : col::DarkGrey);
    const RuntimeSprite& face = Portrait(hp);
    ui_.Fill(x + 6, y + 6, 40, 40, col::Rgb(20, 12, 28));
    ui_.SpriteTex(face.tex, face.def, x + 6, y + 6, 40, 40);
    ui_.Text(ui::Medium, x + 52, y + 8, col::White, hp.playerName);
    ui_.Text(ui::Small, x + 52, y + 26, col::Grey, ClassName(hp.playerClassConfig));
    PlayerStat stats[3] = {PlayerStat::Stamina, PlayerStat::Strength, PlayerStat::Perk};
    std::string labels[3] = {T("Gameplay/Stamina", "Stamina"), T("Gameplay/Strength", "Strength"), PerkName(hp.perkChain)};
    uint32_t nodeCol[3] = {col::Green, col::Rgb(236, 71, 0), col::Yellow}; // PlayerStatUpgrader.GetNodeColour
    for (int i = 0; i < 3; i++) {
        float ry = y + 54 + i * 44;
        bool on = selected && selRow == i;
        ui_.Panel(x + 6, ry, w - 12, 40, on ? col::Rgb(190, 38, 51, 0.9f) : col::Rgb(20, 12, 28, 0.8f), on ? col::White : col::DarkGrey);
        bool max = hp.StatIsMaxLevel(stats[i]);
        int price = max ? 0 : hp.GetNextUpgradePrice(stats[i]);
        bool tooExpensive = !max && run_->gold < price;
        ui_.Text(ui::Small, x + 12, ry + 4, tooExpensive ? col::Red : col::White, labels[i]);
        if (stats[i] != PlayerStat::Perk || hp.perkLevel > 0) {
            std::string v = std::to_string(hp.GetStatValue(stats[i]));
            ui_.Text(ui::Small, x + w - 12, ry + 4, col::Grey, v, Align::Right);
        }
        int cap = hp.GetStatLevelCap(stats[i]), lv = hp.GetStatLevel(stats[i]);
        for (int k = 0; k < cap; k++) ui_.Fill(x + 12 + k * 12, ry + 22, 9, 9, k < lv ? nodeCol[i] : col::DarkGrey);
        if (!max) {
            float px = x + w - 12;
            px -= ui_.Text(ui::Small, px, ry + 21, tooExpensive ? col::Red : col::Yellow, std::to_string(price), Align::Right) + 2;
            ui_.Sprite("Icons_Gold", px - 12, ry + 21, 11, 11);
        }
    }
}

void App::DrawManage() {
    if (st_.view) DrawStage(), ui_.Begin();
    if (gameUi_.loaded()) DrawBlackFade();
    if (gameUi_.loaded() && DrawManageCanvas()) {
        Hints(ui_, {{Input::Confirm, T("Gameplay/Upgrades_Upgrade", "Upgrade")}, {Input::Back, T("General/MenuNav_Continue", "Continue")}});
        return;
    }
    ui_.Shade(0.65f);
    Run& run = *run_;
    const float W = ui_.W(), H = ui_.H();
    ui_.Text(ui::Large, W * 0.5f, 10, col::White, T("Gameplay/Upgrades_Header", "Manage Team"), Align::Center);
    float gx = W - 14;
    gx -= ui_.Text(ui::Medium, gx, 14, col::Yellow, std::to_string(run.gold), Align::Right) + 4;
    ui_.Sprite("Icons_Gold_wOutline", gx - 16, 14, 16, 16);
    int players = static_cast<int>(run.activePlayers.size());
    bool recruits = !run.teamIsAtMaximumCapacity() && !run.reservePlayers.empty();
    int cols = players + (recruits ? 1 : 0);
    float cw = std::min(220.0f, (W - 40) / std::max(1, cols) - 10);
    float total = cols * (cw + 10) - 10, x0 = std::floor((W - total) * 0.5f), y0 = 44;
    int rowsHere = sel2_ < players ? 3 : std::min<int>(3, static_cast<int>(run.reservePlayers.size()));
    bool onDone = sel_ >= rowsHere;
    for (int p = 0; p < players; p++)
        DrawPlayerCard(*run.activePlayers[static_cast<size_t>(p)], x0 + p * (cw + 10), y0, cw, !onDone && sel2_ == p, sel_);
    if (recruits) {
        // the recruit column (NewRecruitButton x3)
        float x = x0 + players * (cw + 10);
        ui_.Panel(x, y0, cw, 200, col::Rgb(27, 38, 50, 0.92f), !onDone && sel2_ == players ? col::White : col::DarkGrey);
        ui_.Text(ui::Medium, x + cw * 0.5f, y0 + 6, col::White, T("Gameplay/Upgrades_Recruit_Hdr", "Recruit"), Align::Center);
        int price = data_.levelUp.newRecruitPrice;
        for (int i = 0; i < 3 && i < static_cast<int>(run.reservePlayers.size()); i++) {
            const HumanPlayer& hp = *run.reservePlayers[static_cast<size_t>(i)];
            float ry = y0 + 28 + i * 56;
            bool on = !onDone && sel2_ == players && sel_ == i;
            ui_.Panel(x + 6, ry, cw - 12, 52, on ? col::Rgb(190, 38, 51, 0.9f) : col::Rgb(20, 12, 28, 0.8f), on ? col::White : col::DarkGrey);
            const RuntimeSprite& face = Portrait(hp);
            ui_.SpriteTex(face.tex, face.def, x + 10, ry + 4, 32, 32);
            ui_.Text(ui::Small, x + 46, ry + 4, col::White, hp.playerName);
            ui_.Text(ui::Small, x + 46, ry + 18, col::Grey, ClassName(hp.playerClassConfig));
            ui_.Text(ui::Small, x + 46, ry + 32, col::Yellow, PerkName(hp.perkChain));
            ui_.Text(ui::Small, x + cw - 12, ry + 4, run.gold < price ? col::Red : col::Yellow, std::to_string(price), Align::Right);
        }
    }
    // the selected upgrade's description (upgradeStatPopup)
    float iy = y0 + 210;
    if (!onDone) {
        std::string info;
        if (sel2_ < players) {
            const HumanPlayer& hp = *run.activePlayers[static_cast<size_t>(sel2_)];
            info = sel_ == 0 ? T("Gameplay/Upgrades_Stamina", "Increase Maximum Stamina.") + " " + T("Gameplay/Upgrades_Stamina_Desc", "")
                   : sel_ == 1 ? T("Gameplay/Upgrades_Strength", "Increase Strength.") + " " + T("Gameplay/Upgrades_Strength_Desc", "")
                               : T("Gameplay/Upgrades_Perk", "Increase Perk Level.") + " " + PerkName(hp.perkChain);
            if (hp.StatIsMaxLevel(sel_ == 0 ? PlayerStat::Stamina : sel_ == 1 ? PlayerStat::Strength : PlayerStat::Perk)) info += " (MAX)";
        } else {
            info = T("Gameplay/Upgrades_Recruit", "You can fill this empty slot with a new player.");
        }
        ui_.Paragraph(ui::Small, 30, iy, W - 60, col::Grey, info, Align::Center);
    }
    ui_.Button(W * 0.5f - 70, H - 58, 140, 22, T("General/MenuNav_Done", "Done"), onDone);
    Hints(ui_, {{Input::Confirm, T("Gameplay/Upgrades_Upgrade", "Upgrade")}, {Input::Back, T("General/MenuNav_Done", "Done")}});
}

void App::DrawKeepGoing() {
    if (st_.view) DrawStage(), ui_.Begin();
    if (gameUi_.loaded()) DrawBlackFade();
    if (gameUi_.loaded() && DrawKeepGoingCanvas()) return Hints(ui_, {{Input::Confirm, T("General/MenuNav_Select", "Select")}});
    DrawConfirm(T("Gameplay/Upgrades_Continue_Hdr", "Keep Going?"), T("Gameplay/Upgrades_Continue", "You can stop here and resume another time."),
                {T("Gameplay/StageEnd_SaveAndQuit_ContinuePlaying", "Continue Playing"), T("Gameplay/StageEnd_SaveAndQuit", "Save & Quit")}, sel_);
}

void App::DrawOverwrite() {
    if (st_.view) DrawStage(), ui_.Begin();
    if (gameUi_.loaded()) DrawBlackFade();
    if (gameUi_.loaded() && DrawOverwriteCanvas())
        return Hints(ui_, {{Input::Confirm, T("General/MenuNav_Select", "Select")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
    DrawConfirm(T("Gameplay/Upgrades_QuitConfirm_Hdr", "Overwrite Existing Save Data?"), T("Gameplay/Upgrades_QuitConfirm", ""),
                {T("Gameplay/StageEnd_SaveAndQuit_Overwrite", "Overwrite Old Data"),
                 T("Gameplay/StageEnd_SaveAndQuit_QuitWithoutSaving", "Quit Without Saving"),
                 T("Gameplay/StageEnd_SaveAndQuit_ContinuePlaying", "Continue Playing")},
                sel_);
}

// EndingScreen: final results of the run
void App::DrawEnding() {
    Run& run = *run_;
    const float W = ui_.W(), H = ui_.H();
    ui_.Fill(0, 0, W, H, kBackground);
    ui_.Text(ui::Large, W * 0.5f, 14, col::Yellow, T("Gameplay/Ending_FinalResults", "FINAL RESULTS"), Align::Center);
    float y = 50;
    if (!defeat_) {
        std::vector<const HumanPlayer*> alive(run.activePlayers.begin(), run.activePlayers.end());
        std::string a = alive.size() >= 2 ? ui::Format(T("Gameplay/Ending_Survived_1_Plural", "{0.name} and {1.name} survived"),
                                                       {{"name", alive[0]->playerName}, {"name2", alive[1]->playerName}})
                        : !alive.empty() ? ui::Format(T("Gameplay/Ending_Survived_1_Singular", "{name} survived"), {{"name", alive[0]->playerName}})
                                         : "";
        std::string b = alive.size() >= 2 ? T("Gameplay/Ending_Survived_2_Plural", "and won their freedom!")
                        : !alive.empty() && alive[0]->seed.isMale ? T("Gameplay/Ending_Survived_2_Male", "and won his freedom!")
                                                                  : T("Gameplay/Ending_Survived_2_Female", "and won her freedom!");
        ui_.Text(ui::Medium, W * 0.5f, y, col::White, a, Align::Center);
        ui_.Text(ui::Medium, W * 0.5f, y + 20, col::Green, b, Align::Center);
        for (size_t i = 0; i < alive.size(); i++) {
            const RuntimeSprite& face = Portrait(*alive[i]);
            ui_.SpriteTex(face.tex, face.def, W * 0.5f - 60 + static_cast<float>(i) * 72, y + 44, 48, 48);
        }
    } else {
        ui_.Text(ui::Large, W * 0.5f, y, col::Red,
                 T("Gameplay/Ending_GameOver1", "In") + " Dungeon Deathball " + StripNewlines(T("Gameplay/Ending_GameOver2", "there are no winners")),
                 Align::Center);
    }
    // the twelve stages' ranks
    y = 160;
    ui_.Text(ui::Medium, W * 0.5f, y, col::White, T("Gameplay/Ending_Ranks", "Ranks"), Align::Center);
    float rx = std::floor(W * 0.5f - 12 * 26 * 0.5f);
    for (int i = 0; i < 12; i++) {
        int rank = i < run.stagesPlayed ? run.stageResults[i].rank : 0;
        if (!ui_.Sprite(RankSprite(rank), rx + i * 26, y + 22, 22, 22))
            ui_.Text(ui::Medium, rx + i * 26 + 11, y + 24, col::DarkGrey, RankLetter(rank), Align::Center);
    }
    int score = run.GetFinalScoreTotal();
    ui_.Text(ui::Medium, W * 0.5f, y + 60, col::White, T("General/FinalScore", "Final Score"), Align::Center);
    ui_.Text(ui::Large, W * 0.5f, y + 78, col::Yellow, std::to_string(score), Align::Center);
    int best = meta_.BestScore(rec_.team, rec_.mode);
    if (meta_.newHighScore) ui_.Text(ui::Medium, W * 0.5f, y + 112, col::Green, T("General/NewRecord", "NEW RECORD"), Align::Center);
    else ui_.Text(ui::Small, W * 0.5f, y + 112, col::Grey, T("General/HighScore", "High Score") + ": " + std::to_string(best), Align::Center);
    ui_.Text(ui::Small, W * 0.5f, y + 132, col::Grey,
             ui::Format(T("Gameplay/BallModsUsed", "USED: {0.easyValue} Easy, {1.hardValue} Hard"),
                        {{"easyValue", ui::Int(run.easyBallModsUsed)}, {"hardValue", ui::Int(run.hardBallModsUsed)}}),
             Align::Center);
    Hints(ui_, {{Input::Confirm, T("General/MenuNav_Continue", "Continue")}});
}

// ---------------------------------------------------------------------------------------------
// Title and new game
// ---------------------------------------------------------------------------------------------
// TitleScreen: the original's Canvas_TitleScreen with MenuPage_MainMenu showing
bool App::DrawTitleCanvas(const char* page) {
    ui::Canvas& c = titleUi_;
    int root = c.Find("Canvas_TitleScreen");
    if (root < 0) return false;
    TitlePage(page); // also removes what the original hides on this platform (web links, leaderboards)
    c.SetActive(c.Find("MenuPages/MenuPage_MainMenu/WebLinks", root), false);
    // EggTools.Credits: the text scrolls up at autoScrollSpeed (25) and starts over past maxY
    if (int content = c.Find("MenuPages/MenuPage_Credits/PageArea/Credits/Content", root); content >= 0) {
        float maxY = c.node(content).size[1] + 360.0f; // + AppConfig.lowResolution.y
        c.node(content).pos[1] = screen_ == Screen::Credits ? std::fmod(timer_ * 25.0f, maxY) : 0.0f;
    }
    // the texts the original's scripts fill in (VersionTextLocalized, the spawned Options page button)
    std::string version = std::to_string(data_.buildVersion.major) + "." + std::to_string(data_.buildVersion.minor) + "." +
                          std::to_string(data_.buildVersion.patch);
    c.SetText(c.Find("TitleScreenAnimator/Text_S_VersionText", root), ui::Format(T("FrontEnd/VersionNumber", "Version {value}"), {{"value", version}}));
    // SeasonalEventImageSwapper: the season's logo
    const char* logo = season_ == SeasonalEvent::Halloween ? "LogoParts_Full_Halloween" : season_ == SeasonalEvent::Easter ? "LogoParts_Full_Easter" : "LogoParts_Full";
    c.SetSprite(c.Find("TitleScreenAnimator/TitleStrip/Logo", root), logo);
    // the selected button's outline (SelectedOutline): rows Continue, New Game, How To Play, Stats, Options, Credits
    int buttons = c.Find("MenuPages/MenuPage_MainMenu/PageArea/Buttons", root);
    if (buttons >= 0) {
        const auto& kids = c.node(buttons).kids;
        for (size_t b = 0; b < kids.size(); b++) {
            int outline = c.FindDeep("SelectedOutline", kids[b]);
            c.SetActive(outline, static_cast<int>(b) == sel_);
            int label = c.FindDeep("Label", kids[b]);
            if (c.node(kids[b]).name == "Button (Options)") c.SetText(label, T("General/Options", "Options"));
            // MenuObjectLocker: Continue is locked without a saved run
            if (c.node(kids[b]).name == "Button (Continue)" && label >= 0) {
                uint32_t base = c.node(label).baseTextColor;
                c.SetTextColor(label, hasSave_ ? base : (base & 0x00ffffffu) | 0x60000000u);
            }
        }
    }
    c.Draw(ui_, root, loc_);
    return true;
}

void App::DrawTitle() {
    const float W = ui_.W(), H = ui_.H();
    if (titleUi_.loaded() && DrawTitleCanvas()) return;
    // TitleScreen: the logo (the season's own when it has one)
    const SeasonalEventConfig* sc = data_.SeasonalConfig(season_);
    std::string logo = season_ == SeasonalEvent::Halloween ? "LogoParts_Full_Halloween" : season_ == SeasonalEvent::Easter ? "LogoParts_Full_Easter" : "LogoParts_Full";
    if (sc && sc->titleLogo && !sc->titleLogoSprite.empty()) logo = sc->titleLogoSprite;
    if (!ui_.Sprite(logo, W * 0.5f - 160, 20, 320, 120)) ui_.Text(ui::Large, W * 0.5f, 60, col::Red, "DUNGEON DEATHBALL", Align::Center);

    std::vector<std::pair<int, std::string>> rows;
    if (hasSave_) rows.emplace_back(0, T("General/MenuNav_Continue", "Continue"));
    rows.emplace_back(1, T("FrontEnd/NewGame", "New Game"));
    rows.emplace_back(4, T("FrontEnd/HowToPlay", "How to Play"));
    rows.emplace_back(5, T("FrontEnd/StatsAndAchievements", "Stats & Achievements"));
    rows.emplace_back(2, T("General/Options", "Options"));
    if (platform::CanQuit()) rows.emplace_back(3, T("General/QuitGame", "Quit Game"));
    float bw = 220, x = std::floor((W - bw) * 0.5f), y = 160;
    for (size_t i = 0; i < rows.size(); i++) ui_.Button(x, y + static_cast<float>(i) * 28, bw, 24, rows[i].second, static_cast<int>(i) == sel_);
    // the saved run, under the buttons when Continue is selected
    if (hasSave_ && sel_ == 0) {
        float iy = y + static_cast<float>(rows.size()) * 28 + 6;
        std::string prog = ui::Format(T("FrontEnd/Continue_Progress", "{0.gameMode} - Stage {1.trial}-{2.stage}"),
                                      {{"gameMode", ModeName(saved_.mode)},
                                       {"trial", ui::Int(static_cast<int>(saved_.stages.size()) / 3 + 1)},
                                       {"stage", ui::Int(static_cast<int>(saved_.stages.size()) % 3 + 1)}});
        ui_.Text(ui::Small, W * 0.5f, iy, col::Yellow, prog, Align::Center);
        ui_.Paragraph(ui::Small, W * 0.5f - 200, iy + 16, 400, col::Grey, T("FrontEnd/Continue_Warning", ""), Align::Center);
    }
    // Daily Challenge reminder (DCPopout) when today's has not been played
    if (meta_.ModeUnlocked(data_.GetGameModeConfig(GameMode::DailyChallenge)) && !PlayedDailyToday()) {
        bool hall = season_ == SeasonalEvent::Halloween, easter = season_ == SeasonalEvent::Easter;
        std::string hdr = hall ? T("FrontEnd/DCPopout_Halloween_Hdr", "Halloween Special Event!")
                          : easter ? T("FrontEnd/DCPopout_Easter_Hdr", "Egg Hunt - Special Event!")
                                   : T("FrontEnd/DCPopout_NewDaily_Hdr", "A new dungeon awaits!");
        float pw = 190, px = W - pw - 10, py = H - 96;
        ui_.Panel(px, py, pw, 60, col::Rgb(27, 38, 50, 0.9f), hall ? col::Orange : easter ? col::Rgb(203, 67, 167) : col::Blue);
        ui_.Text(ui::Small, px + pw * 0.5f, py + 6, hall ? col::Orange : col::White, hdr, Align::Center);
        ui_.Text(ui::Small, px + pw * 0.5f, py + 24, col::Grey, T("General/GameMode_Daily", "Daily Challenge"), Align::Center);
    }
    ui_.Text(ui::Small, 8, H - 18, col::DarkGrey,
             ui::Format(T("FrontEnd/VersionNumber", "Version {value}"),
                        {{"value", std::to_string(data_.buildVersion.major) + "." + std::to_string(data_.buildVersion.minor) + "." +
                                       std::to_string(data_.buildVersion.patch) + " (Vita port)"}}));
    if (screen_ == Screen::Title) Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}});
}

void App::DrawModes() {
    if (screen_ == Screen::Modes && titleUi_.loaded() && DrawModesCanvas())
        return Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
    const float W = ui_.W(), H = ui_.H();
    ui_.Text(ui::Large, W * 0.5f, 14, col::White, T("FrontEnd/SelectMode", "Select Mode"), Align::Center);
    static const GameMode modes[4] = {GameMode::Normal, GameMode::DailyChallenge, GameMode::Boss, GameMode::Action};
    static const char* const desc[4][2] = {{"FrontEnd/GameMode_Normal_Desc1", "FrontEnd/GameMode_Normal_Desc2"},
                                           {"FrontEnd/GameMode_Daily_Desc1", "FrontEnd/GameMode_Daily_Desc2"},
                                           {"FrontEnd/GameMode_Boss_Desc1", "FrontEnd/GameMode_Boss_Desc2"},
                                           {"FrontEnd/GameMode_Action_Desc1", "FrontEnd/GameMode_Action_Desc2"}};
    float bw = 200, x = 30, y = 60;
    for (int i = 0; i < 4; i++) {
        bool unlocked = meta_.ModeUnlocked(data_.GetGameModeConfig(modes[i]));
        ui_.Button(x, y + i * 30, bw, 26, unlocked ? ModeName(modes[i]) : T("General/Locked", "Locked"), sel_ == i, unlocked);
    }
    // the selected mode's panel
    GameMode m = modes[sel_];
    const GameModeConfig* c = data_.GetGameModeConfig(m);
    bool unlocked = meta_.ModeUnlocked(c);
    float px = x + bw + 20, pw = W - px - 30, py = 60;
    ui_.Panel(px, py, pw, 220);
    ui_.Text(ui::Medium, px + 12, py + 10, col::Yellow, ModeName(m));
    float ty = py + 32;
    if (unlocked) {
        ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, col::White, T(desc[sel_][0], "")) + 4;
        ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, col::Grey, T(desc[sel_][1], "")) + 8;
        if (c && (!c->allowRestartStage || !c->allowSaveAndQuit))
            ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, col::Orange, T("FrontEnd/ModeInfo_NotAllowedDC", "")) + 6;
        if (m == GameMode::DailyChallenge && PlayedDailyToday())
            ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, col::Grey, T("FrontEnd/DCWinners_Today", "Leading Today") + ": " +
                                                                             std::to_string(meta_.highScoreDaily)) + 6;
        int best = meta_.BestScore(m == GameMode::DailyChallenge ? 0 : 0, m);
        ui_.Text(ui::Small, px + 12, py + 196, col::Grey, T("General/HighScore", "High Score") + ": " + std::to_string(best));
    } else {
        ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, col::White, T("FrontEnd/UnlockMode_Help", "Unlock achievements to play this mode.")) + 6;
        int need = c ? c->unlockCriteriaAchievementCount - meta_.UnlockedCount() : 0;
        std::string count = need > 0 ? ui::Format(T("FrontEnd/UnlockMode_Counter_Incomplete", "Earn {value} more achievements."), {{"value", ui::Int(need)}})
                                     : ui::Format(T("FrontEnd/UnlockMode_Counter_Complete", "{value} achievements unlocked."),
                                                  {{"value", ui::Int(meta_.UnlockedCount())}});
        ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, need > 0 ? col::Orange : col::Green, count) + 6;
        if (c)
            for (int a : c->unlockCriteriaAchievements)
                if (const AchievementDef* d = meta_.Def(static_cast<Achievement>(a))) {
                    bool got = meta_.Unlocked(d->id);
                    ty += ui_.Paragraph(ui::Small, px + 12, ty, pw - 24, got ? col::Green : col::Grey, (got ? "+ " : "- ") + T(d->name, d->nameFallback)) + 2;
                }
    }
    Hints(ui_, {{Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")}, {Input::Back, T("General/MenuNav_Back", "Back")}});
    (void)H;
}

void App::DrawTeams() {
    if (titleUi_.loaded() && DrawTeamsCanvas()) {
        bool un = meta_.teams[static_cast<size_t>(sel_)].unlocked;
        return Hints(ui_, {{Input::Confirm, un ? T("FrontEnd/Teams_SelectTeam", "Select Team") : T("FrontEnd/Teams_UnlockTeam", "Unlock Team")},
                           {Input::Back, T("General/MenuNav_Back", "Back")}});
    }
    const float W = ui_.W(), H = ui_.H();
    ui_.Text(ui::Large, W * 0.5f, 10, col::White, T("FrontEnd/Teams_SelectTeam", "Select Team"), Align::Center);
    // keys owned
    float kx = W - 14;
    kx -= ui_.Text(ui::Medium, kx, 14, col::White, std::to_string(meta_.keys), Align::Right) + 4;
    ui_.Sprite("Icons_SkullKey_wOutline", kx - 18, 12, 18, 18);
    int n = static_cast<int>(data_.teams.size());
    // the list, scrolled to keep the selection visible
    float lx = 20, ly = 44, rowH = 20;
    int visible = static_cast<int>((H - ly - 40) / rowH);
    int first = std::clamp(sel_ - visible / 2, 0, std::max(0, n - visible));
    for (int i = first; i < n && i < first + visible; i++) {
        const TeamProgress& tp = meta_.teams[static_cast<size_t>(i)];
        float y = ly + (i - first) * rowH;
        bool on = i == sel_;
        ui_.Panel(lx, y, 180, rowH - 2, on ? col::Rgb(190, 38, 51, 0.95f) : col::Rgb(27, 38, 50, 0.9f), on ? col::White : col::DarkGrey);
        ui_.Text(ui::Small, lx + 8, y + 3, tp.unlocked ? col::White : col::DarkGrey, TeamName(i));
        if (!tp.unlocked) ui_.Sprite("IconSheet16_Lock", lx + 160, y + 1, 14, 14);
        else if (tp.stagesCompleted >= 12) ui_.Sprite("Icons_StarFilled", lx + 160, y + 1, 14, 14);
    }
    // the selected team
    const TeamConfig* t = data_.teams[static_cast<size_t>(sel_)];
    const TeamProgress& tp = meta_.teams[static_cast<size_t>(sel_)];
    float px = 216, pw = W - px - 20, py = 44;
    ui_.Panel(px, py, pw, H - py - 40);
    ui_.Text(ui::Medium, px + 12, py + 8, col::Yellow, ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(sel_)}}));
    float y = py + 30;
    y += ui_.Paragraph(ui::Small, px + 12, y, pw - 24, col::White, t->description) + 6;
    if (t->positiveTrait && !t->positiveTrait->description.empty())
        y += ui_.Paragraph(ui::Small, px + 12, y, pw - 24, col::Green, "+ " + ui::Format(t->positiveTrait->description, {{"value", ui::Int(t->positiveTrait->valueInt)}})) + 2;
    if (t->negativeTrait && !t->negativeTrait->description.empty())
        y += ui_.Paragraph(ui::Small, px + 12, y, pw - 24, col::Red, "- " + ui::Format(t->negativeTrait->description, {{"value", ui::Int(t->negativeTrait->valueInt)}})) + 2;
    y += 6;
    // the two players' setup
    for (size_t i = 0; i < t->players.size() && i < 2; i++) {
        const auto& ps = t->players[i];
        std::string cls = ps.playerClassConfig ? ClassName(ps.playerClassConfig) : T("General/Random", "Random");
        std::string perk = ps.playerPerk ? PerkName(ps.playerPerk) : T("General/Random", "Random");
        ui_.Sprite(ps.playerClassConfig ? std::string("Icons_Class_") + ToString(ps.playerClassConfig->playerClass) : "Icons_Class_Random", px + 12, y, 16, 16);
        ui_.Text(ui::Small, px + 32, y + 2, col::White,
                 ui::Format(T("FrontEnd/Teams_PlayerIndex", "Player {value}"), {{"value", ui::Int(static_cast<int>(i) + 1)}}) + ": " + cls + " / " + perk);
        y += 20;
    }
    y += 6;
    if (tp.unlocked) {
        ui_.Text(ui::Small, px + 12, y, col::Grey, T("FrontEnd/Teams_Stages", "Stages Complete") + ": " + std::to_string(tp.stagesCompleted) + "/12");
        ui_.Text(ui::Small, px + 12, y + 16, col::Grey, T("General/HighScore", "High Score") + ": " + std::to_string(meta_.BestScore(sel_, newMode_)));
    } else {
        ui_.Text(ui::Small, px + 12, y, col::Orange, T("FrontEnd/Teams_ToUnlock", "Required to Unlock this Team"));
        y += 16;
        bool enough = meta_.keys >= t->unlockCost;
        ui_.Text(ui::Small, px + 12, y, enough ? col::Green : col::Red,
                 ui::Format(T("FrontEnd/Teams_Req_SkullKeys", "{value} Skull Keys"), {{"value", ui::Int(t->unlockCost)}}));
        if (!enough)
            ui_.Text(ui::Small, px + 140, y, col::Grey,
                     ui::Format(T("FrontEnd/Teams_SkullKeysDiff", "({value} more required)"), {{"value", ui::Int(t->unlockCost - meta_.keys)}}));
        y += 16;
        if (t->requiredAchievement > 0)
            if (const AchievementDef* d = meta_.Def(static_cast<Achievement>(t->requiredAchievement))) {
                bool got = meta_.Unlocked(d->id);
                ui_.Text(ui::Small, px + 12, y, got ? col::Green : col::Red, T("FrontEnd/Teams_Req_Achievement", "Achievement:") + " " + T(d->name, d->nameFallback));
                y += 16;
            }
        ui_.Paragraph(ui::Small, px + 12, y + 4, pw - 24, col::Grey, T("FrontEnd/Teams_SkullKeysHelp", "Complete more Trials using different Teams."));
    }
    Hints(ui_, {{Input::Confirm, tp.unlocked ? T("FrontEnd/Teams_SelectTeam", "Select Team") : T("FrontEnd/Teams_UnlockTeam", "Unlock Team")},
                {Input::Back, T("General/MenuNav_Back", "Back")}});
}

// ---------------------------------------------------------------------------------------------
// Cinematics
// ---------------------------------------------------------------------------------------------
// The "Intro" scene: the Judgment under a spotlight, one line of his speech after another
void App::DrawIntro() {
    const float W = ui_.W(), H = ui_.H();
    ui_.Fill(0, 0, W, H, col::Black);
    const float lineTime = 3.4f;
    int line = std::min(4, static_cast<int>(timer_ / lineTime));
    float t = timer_ - line * lineTime;
    float fade = std::min(1.0f, timer_ / 1.0f);
    uint32_t tint = PackColor(fade, fade, fade, 1);
    ui_.Sprite("Spotlight", W * 0.5f - 140, 0, 280, H * 0.8f, PackColor(1, 1, 1, 0.35f * fade));
    float bx = W * 0.5f - 90, by = H * 0.12f;
    ui_.Sprite("SkullLord_Intro_Body_0", bx, by + 40, 180, 180, tint);
    // the mouth moves while the line is being typed
    size_t chars = static_cast<size_t>(std::max(0.0f, t - 0.3f) * 32);
    std::string full = T("FrontEnd/IntroDialogue_" + std::to_string(line + 1), "");
    bool talking = chars < full.size();
    const char* head = !talking ? "SkullLord_Intro_Head_Closed"
                       : (static_cast<int>(timer_ * 8) % 3 == 0 ? "SkullLord_Intro_Head_Open"
                          : static_cast<int>(timer_ * 8) % 3 == 1 ? "SkullLord_Intro_Head_Mid"
                                                                    : "SkullLord_Intro_Head_Closed");
    ui_.Sprite(head, bx + 30, by, 120, 120, tint);
    if (line == 2) ui_.Sprite("SkullLord_Intro_BallHand", bx + 120, by + 110, 80, 80, tint);
    // the line, typed out (UTF-8 safe: cut at a character boundary)
    std::string shown = full.substr(0, std::min(chars, full.size()));
    while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xc0) == 0x80) shown.pop_back();
    float ty = H * 0.78f;
    for (const std::string& l : ui_.Wrap(ui::Medium, shown, W - 80)) {
        ui_.Text(ui::Medium, W * 0.5f, ty, col::White, l, Align::Center);
        ty += ui_.LineHeight(ui::Medium) + 4;
    }
    float lh = ui_.LineHeight(ui::Small);
    float hx = W - 90, hy = H - lh - 8;
    hx += ui_.Prompt(hx, hy, platform::ButtonName(Input::Confirm), platform::ButtonSprite(Input::Confirm)) + 2;
    ui_.Text(ui::Small, hx, hy, col::Grey, "Skip");
}

// TrialIntro: the trial's monsters in silhouette, then revealed under spotlights
void App::DrawTrialIntro() {
    const float W = ui_.W(), H = ui_.H();
    ui_.Fill(0, 0, W, H, col::Black);
    const float revealAt = 1.4f;
    bool revealed = timer_ >= revealAt;
    static float lastReveal = -1;
    if (revealed && lastReveal < revealAt && timer_ - revealAt < 0.1f) sound_.PlayJingle("jingle.enemiesAppear");
    lastReveal = timer_;
    if (revealed) {
        const TrialData* td = run_->currentTrial < static_cast<int>(data_.session.trials.size())
                                  ? &data_.session.trials[static_cast<size_t>(run_->currentTrial)] : nullptr;
        float shake = timer_ - revealAt < 0.3f ? std::sin(timer_ * 90.0f) * 3.0f : 0.0f; // CameraShake
        ui_.Text(ui::Large, W * 0.5f + shake, 30, col::Yellow,
                 ui::Format(T("Gameplay/TrialCount", "Trial {0.value} of {1.maxValue}"), {{"value", ui::Int(run_->currentTrial + 1)}, {"maxValue", "4"}}),
                 Align::Center);
        if (td && !td->trialName.empty()) ui_.Text(ui::Medium, W * 0.5f, 62, col::White, td->trialName, Align::Center);
    }
    size_t n = trialLineup_.size();
    float cell = std::min(150.0f, (W - 40) / std::max<size_t>(1, n));
    float x0 = std::floor((W - cell * n) * 0.5f), y = H * 0.36f;
    for (size_t i = 0; i < n; i++) {
        const EnemyConfig* e = trialLineup_[i];
        float x = x0 + static_cast<float>(i) * cell;
        if (revealed) ui_.Sprite("Spotlight", x + cell * 0.1f, y - 60, cell * 0.8f, 200, PackColor(1, 1, 1, 0.3f));
        // TrialIntroNewEnemy: they take turns to attack once revealed
        float bob = revealed && static_cast<int>((timer_ - revealAt) * 2 + i) % 4 == 0 ? -4.0f : 0.0f;
        ui_.Sprite(e->sprite, x + 8, y + bob, cell - 16, cell - 16, revealed ? col::White : PackColor(0.06f, 0.06f, 0.06f, 1));
        if (revealed) ui_.Text(ui::Small, x + cell * 0.5f, y + cell - 8, col::Grey, e->actorName, Align::Center);
    }
}

// ---------------------------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------------------------
void App::DrawToast() {
    if (meta_.newlyUnlocked.empty()) return;
    const AchievementDef* d = meta_.Def(meta_.newlyUnlocked.front());
    float k = std::min({1.0f, toastTimer_ / 0.25f, (kToastTime - toastTimer_) / 0.25f});
    float w = 230, h = 40, x = ui_.W() - w - 8, y = ui_.H() - (h + 8) * std::max(0.0f, k) - 30;
    ui_.Panel(x, y, w, h, col::Rgb(20, 12, 28, 0.95f), col::Yellow);
    if (d) ui_.Sprite(d->icon, x + 4, y + 4, 32, 32);
    ui_.Text(ui::Small, x + 42, y + 5, col::Yellow, T("General/AchievementUnlocked", "Achievement Unlocked!"));
    ui_.Text(ui::Small, x + 42, y + 21, col::White, d ? T(d->name, d->nameFallback) : "");
}

void App::DrawDebug() {
    if (showLog_ && st_.view) {
        float y = 100;
        for (const std::string& l : st_.view->log()) {
            ui_.Text(ui::Small, 8, y, col::Rgb(200, 200, 230), l);
            y += 12;
        }
    }
    if (showPerf_) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%.0f fps  %.1f ms (worst %.1f)  cpu %.1f ms", static_cast<double>(perf.fps), static_cast<double>(perf.avgMs),
                      static_cast<double>(perf.worstMs), static_cast<double>(perf.cpuMs));
        ui_.Text(ui::Small, ui_.W() - 8, 26, col::Green, buf, Align::Right);
    }
}

} // namespace dd::app
