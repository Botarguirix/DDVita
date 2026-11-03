// The game loop shared by every platform: one stage on screen at a time (simulation + view +
// sound), the player's cursor and commands, the placeholder HUD and the run save.
//
//   ddgame [data_dir] [seed|daily|continue] [mode] [team] [--shot out.bmp seconds] [--auto] [--speed k]
//   (--auto plays by itself and --speed scales time: both for testing)
//
// Without a seed it continues the saved run, if there is one, or starts a random run.
#include "app/game_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "app/platform.h"
#include "app/save.h"
#include "audio/sound.h"
#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/session_generator.h"
#include "render/assets.h"
#include "render/gfx.h"
#include "render/stage_view.h"
#include "sim/sim.h"

namespace dd::app {

using namespace dd::sim;
using namespace dd::gfx;

namespace {

std::string ActionName(const PlayerActionConfig* c) {
    std::string n = c->name;
    if (n.rfind("PlayerActionConfig_", 0) == 0) n = n.substr(19);
    return n;
}

bool IsPassTake(const PlayerActionConfig* a) { return a->action == ActorAction::Player_Pass || a->action == ActorAction::Player_TakeBall; }
bool IsShoot(const PlayerActionConfig* a) { return a->action == ActorAction::Player_Shoot; }
bool IsRest(const PlayerActionConfig* a) { return a->action == ActorAction::Char_Wait; }
bool IsUndo(const PlayerActionConfig* a) { return a->undo; }

// One stage on screen: the simulation, its view and the player's cursor.
struct StageScreen {
    std::unique_ptr<Game> game;
    std::unique_ptr<StageView> view;
    V2 cursor;
    int cursorFor = -1; // uid of the player the cursor was placed for
    std::vector<Command> commands;
};

// Frame timing for the performance readout (F1 / Select+L on the Vita).
struct Perf {
    float acc = 0, worst = 0, cpuAcc = 0;
    int frames = 0;
    float fps = 0, avgMs = 0, worstMs = 0, cpuMs = 0;
    void Add(float frameSec, float cpuSec) {
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
};

} // namespace

int RunGame(int argc, char** argv) {
    std::string dataDir = platform::DataDir(), shotPath;
    float shotTime = 0, speed = 1;
    bool autoPlay = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 2 < argc) {
            shotPath = argv[++i];
            shotTime = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--speed" && i + 1 < argc) {
            speed = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--auto") {
            autoPlay = true;
        } else {
            pos.push_back(a);
        }
    }
    if (!pos.empty()) dataDir = pos[0];
    const std::string savePath = platform::SaveDir() + "/run.json";

    int ww = 0, wh = 0;
    std::string err;
    if (!platform::Init(&ww, &wh, &err)) {
        std::fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    Renderer r;
    r.Init(ww, wh);

    GameData data;
    Assets assets;
    if (!data.Load(dataDir + "/gamedata.json", &err) || !assets.Load(dataDir + "/render", &r, &err)) {
        std::fprintf(stderr, "data: %s\n(extract it from your copy of the game: see README)\n", err.c_str());
        platform::Shutdown();
        return 1;
    }
    const Font& font = assets.font();

    audio::Mixer mixer;
    audio::Sound sound;
    if (!sound.Load(dataDir + "/audio", &mixer, &err)) std::fprintf(stderr, "no audio: %s\n", err.c_str());
    else platform::StartAudio(&mixer);

    // ---- the run: saved, or new ----
    RunRecord rec;
    bool resumed = false;
    std::string mode = pos.size() > 1 ? pos[1] : "continue";
    if (mode == "continue" && LoadRecord(savePath, &rec)) {
        resumed = true;
    } else {
        rec = RunRecord{};
        rec.date = Date::TodayUtc();
        if (mode == "daily") {
            rec.mode = GameMode::DailyChallenge;
            rec.seed = DailyChallengeSeed(data.platform, rec.date);
            rec.team = pos.size() > 2 ? std::atoi(pos[2].c_str()) : 0;
        } else {
            rec.seed = mode != "continue" ? std::atoi(mode.c_str()) : static_cast<int>(static_cast<long long>(platform::Seconds() * 1e6) & 0x7fffffff);
            if (pos.size() > 2) rec.mode = static_cast<GameMode>(std::atoi(pos[2].c_str()));
            rec.team = pos.size() > 3 ? std::atoi(pos[3].c_str()) : 0;
        }
    }
    SessionRequest req = RequestFor(data, rec);
    auto run = std::make_unique<Run>(data, GenerateSession(data, req), req.team, req.mode);
    sound.Shuffle(1); // music packs B-D unlock with progress (meta milestone)
    if (resumed) {
        if (ReplayStages(*run, rec)) {
            for (size_t i = 0; i < rec.stages.size(); i++) sound.IncrementStageMusicIndex();
        } else { // saved with other game data: start that run again from its first stage
            std::fprintf(stderr, "saved run does not replay; restarting it\n");
            rec.stages.clear();
            run = std::make_unique<Run>(data, GenerateSession(data, req), req.team, req.mode);
        }
    }
    std::printf("seed %d mode %d stage %zu%s\n", rec.seed, static_cast<int>(rec.mode), rec.stages.size() + 1, resumed ? " (continued)" : "");
    std::fflush(stdout);
    if (shotPath.empty() && !SaveRecord(savePath, rec)) std::fprintf(stderr, "cannot write %s\n", savePath.c_str());

    StageScreen st;
    auto startStage = [&]() {
        st.view.reset();
        st.game = std::make_unique<Game>(*run);
        st.game->Start();
        st.view = std::make_unique<StageView>(assets, r, *st.game);
        Game* g = st.game.get();
        st.view->onEvent = [&sound, g](const Event& e) { sound.OnEvent(e, *g); };
        st.cursorFor = -1;
        rec.current.clear();
        sound.StageStarted(*st.game);
    };
    startStage();

    bool showLog = false, showPerf = false, quit = false, runOver = false;
    Perf perf;
    double last = platform::Seconds();
    float clock = 0, autoDelay = 0;
    while (!quit) {
        double now = platform::Seconds();
        float realDt = static_cast<float>(now - last);
        last = now;
        float dt = std::min(realDt, 0.1f) * speed;
        clock += dt;

        Game& g = *st.game;
        bool idle = !st.view->Busy();
        if (idle && g.phase() == Phase::PlayerInput) {
            st.commands = g.GetCommands();
            if (g.currentPlayer && g.currentPlayer->uid != st.cursorFor) {
                st.cursorFor = g.currentPlayer->uid;
                st.cursor = g.currentPlayer->gridPos;
            }
        } else {
            st.commands.clear();
        }

        std::vector<Input> inputs;
        if (!platform::Poll(&inputs, &ww, &wh)) quit = true;
        r.Resize(ww, wh);
        // --auto: plays itself (furthest affordable tile command, else rest) to exercise the game.
        if (autoPlay && idle) {
            autoDelay -= dt;
            if (autoDelay <= 0) {
                autoDelay = 0.4f;
                if (g.phase() == Phase::SuggestUndo) inputs.push_back(Input::No);
                else if (g.phase() == Phase::StageEnded) inputs.push_back(Input::Confirm);
                else if (g.phase() == Phase::PlayerInput) {
                    const Command* pick = nullptr;
                    for (const Command& c : st.commands)
                        if (c.onTile && c.interactable && !c.tooExpensive && !c.actionConfig->switchControl && (!pick || c.tile.x > pick->tile.x))
                            pick = &c;
                    if (pick) {
                        st.cursor = pick->tile;
                        inputs.push_back(Input::Confirm);
                    } else {
                        inputs.push_back(Input::Rest);
                    }
                }
            }
        }

        // every command goes through here so the save can replay it
        auto select = [&](const Command& c) {
            if (c.tooExpensive || !c.interactable) {
                sound.CommandPressed(true);
                return;
            }
            int index = static_cast<int>(&c - st.commands.data());
            sound.CommandPressed(false);
            if (c.actionConfig->switchControl) sound.PlayerSwitched();
            if (g.Select(c)) rec.current.push_back(index); // a refused command changes nothing
        };
        auto selectButton = [&](bool (*pred)(const PlayerActionConfig*)) {
            for (const Command& c : st.commands)
                if (!c.onTile && pred(c.actionConfig)) {
                    select(c);
                    return;
                }
        };
        bool restarted = false;
        for (Input in : inputs) {
            if (in == Input::Quit) quit = true;
            if (in == Input::Log) {
                // F1 cycles: nothing -> event log -> log + performance
                if (!showLog) showLog = true;
                else if (!showPerf) showPerf = true;
                else showLog = showPerf = false;
            }
            if (!idle) continue;
            if (g.phase() == Phase::SuggestUndo) {
                bool yes = in == Input::Yes || in == Input::Confirm, no = in == Input::No || in == Input::Rest;
                if (yes || no) {
                    rec.current.push_back(yes ? RunRecord::kUndoYes : RunRecord::kUndoNo);
                    g.RespondToUndoSuggestion(yes);
                    break;
                }
                continue;
            }
            if (g.phase() == Phase::StageEnded) {
                if (in == Input::Confirm && !runOver) {
                    if (g.gameEndedInDefeat || !run->AdvanceStage()) {
                        runOver = true;
                        if (shotPath.empty()) DeleteRecord(savePath);
                    } else {
                        rec.stages.push_back(rec.current);
                        if (shotPath.empty()) SaveRecord(savePath, rec);
                        sound.IncrementStageMusicIndex();
                        restarted = true;
                    }
                    break;
                }
                continue;
            }
            if (g.phase() != Phase::PlayerInput) continue;
            V2& c = st.cursor;
            switch (in) {
            case Input::Up: c.y = std::min(c.y + 1, g.fieldY - 1); break;
            case Input::Down: c.y = std::max(c.y - 1, 0); break;
            case Input::Left: c.x = std::max(c.x - 1, 0); break;
            case Input::Right: c.x = std::min(c.x + 1, g.fieldX - 1); break;
            case Input::Confirm:
                for (const Command& cmd : st.commands)
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
                for (const Command& cmd : st.commands)
                    if (cmd.actionConfig->switchControl) {
                        select(cmd);
                        break;
                    }
                break;
            default: break;
            }
            if (st.view->Busy()) break; // the action started: wait for it to play out
        }
        if (restarted) {
            startStage();
            continue;
        }

        double cpuStart = platform::Seconds();
        sound.Update(dt);
        st.view->Update(dt);
        idle = !st.view->Busy();
        bool showCursor = idle && g.phase() == Phase::PlayerInput;
        st.view->Draw(showCursor ? &st.cursor : nullptr, st.commands);
        r.EndScene();
        st.view->DrawOverlay();

        // ---- HUD (placeholder font until the UI milestone) ----
        const float s = std::max(1.0f, std::floor(wh / 360.0f));
        const float lh = (font.height + 2) * s;
        auto text = [&](float x, float y, uint32_t col, const std::string& t) {
            r.Fill(x - 3, y - 1, font.Width(t, s) + 6, font.height * s + 2, PackColor(0, 0, 0, 0.55f));
            font.Draw(r, x, y, s, col, t);
        };
        auto key = [](Input in) { return std::string("[") + platform::ButtonName(in) + "]"; };
        const uint32_t white = kWhite, grey = PackColor(0.55f, 0.55f, 0.55f, 1), gold = PackColor(1, 0.85f, 0.3f, 1);
        char buf[256];
        std::snprintf(buf, sizeof buf, "Trial %d  Stage %d   Turn %d / par %d   Gold %d", run->currentTrial + 1, run->currentStage + 1, g.Turn(),
                      g.TurnPar(), run->gold);
        text(8 * s, 6 * s, gold, buf);
        if (Player* p = g.currentPlayer) {
            std::snprintf(buf, sizeof buf, "%s (%s)  HP %d  ST %d/%d  AP %d%s", p->Name().c_str(),
                          ToString(p->humanPlayer->playerClassConfig->playerClass), p->health, p->stamina, p->maxStamina(), p->actionPoints,
                          p->isHoldingBall() ? "  [ball]" : "");
            text(8 * s, 6 * s + lh, white, buf);
        }
        if (showCursor) {
            float x = 8 * s, y = wh - 8 * s - lh;
            auto button = [&](Input in, bool (*pred)(const PlayerActionConfig*)) {
                for (const Command& c : st.commands)
                    if (!c.onTile && pred(c.actionConfig)) {
                        std::string t = key(in) + " " + ActionName(c.actionConfig);
                        if (c.staminaCost) t += " (" + std::to_string(c.staminaCost) + ")";
                        text(x, y, c.interactable && !c.tooExpensive ? white : grey, t);
                        x += font.Width(t, s) + 14 * s;
                        return;
                    }
            };
            button(Input::PassTake, IsPassTake);
            button(Input::Shoot, IsShoot);
            button(Input::Rest, IsRest);
            button(Input::Undo, IsUndo);
            for (const Command& c : st.commands)
                if (c.actionConfig->switchControl) {
                    text(x, y, white, key(Input::Switch) + " Switch");
                    break;
                }
            for (const Command& c : st.commands)
                if (c.onTile && c.tile == st.cursor) {
                    std::string t = key(Input::Confirm) + " " + ActionName(c.actionConfig);
                    if (c.staminaCost) t += "  stamina " + std::to_string(c.staminaCost);
                    text(8 * s, y - lh, c.interactable && !c.tooExpensive ? white : grey, t);
                }
        }
        if (g.phase() == Phase::SuggestUndo && idle) {
            std::string t = "A player is in danger. Undo?  " + key(Input::Yes) + " yes  " + key(Input::No) + " no";
            text((ww - font.Width(t, s)) * 0.5f, wh * 0.4f, gold, t);
        }
        if (g.phase() == Phase::StageEnded && idle) {
            const StageResults& res = g.results();
            std::snprintf(buf, sizeof buf, "Stage %s   score %d   rank %d   par %+d   gold +%d", res.complete ? "complete" : "failed", res.score,
                          res.rank, res.parDifference, g.goldEarned());
            text((ww - font.Width(buf, s)) * 0.5f, wh * 0.4f, gold, buf);
            std::string t = runOver ? "Run over - final score " + std::to_string(run->GetFinalScoreTotal()) + "   " + key(Input::Quit) + " quit"
                                    : key(Input::Confirm) + " continue";
            text((ww - font.Width(t, s)) * 0.5f, wh * 0.4f + lh, white, t);
        }
        if (showLog) {
            float y = 6 * s + lh * 2.5f;
            for (const std::string& l : st.view->log()) {
                text(8 * s, y, PackColor(0.8f, 0.8f, 0.9f, 1), l);
                y += lh;
            }
        }
        if (showPerf) {
            std::snprintf(buf, sizeof buf, "%.0f fps  %.1f ms (worst %.1f)  cpu %.1f ms", perf.fps, perf.avgMs, perf.worstMs, perf.cpuMs);
            text(ww - font.Width(buf, s) - 8 * s, 6 * s, PackColor(0.6f, 1, 0.6f, 1), buf);
        }

        if (!shotPath.empty() && clock >= shotTime) {
            platform::SaveScreenshot(shotPath, ww, wh);
            std::printf("screenshot %s\n", shotPath.c_str());
            quit = true;
        }
        perf.Add(realDt, static_cast<float>(platform::Seconds() - cpuStart));
        platform::Swap();
    }
    platform::StopAudio();
    st.view.reset();
    st.game.reset();
    platform::Shutdown();
    return 0;
}

} // namespace dd::app
