// PS Vita platform: vitaGL (fixed function on GXM), SceCtrl, SceAudioOut, data on ux0:.
//
// Controls: d-pad / left stick move the cursor, Cross does the tile action (and answers "yes"),
// Circle rests (and answers "no"), Square passes or takes the ball, Triangle shoots, Select undoes,
// L switches player, R shows the info popup, Start pauses and Select + Start cycles the log /
// performance readout. Quit with the PS button.
//
// Data: ux0:data/ddvita if it holds gamedata.json, else app0:data (inside ddvita_full.vpk). The save
// always goes to ux0:data/ddvita. With the user's game copied to ux0:data/ddvita/game, the data is
// built there on the first start (install/installer.h). Missing or outdated data shows the system
// message dialog.
//
// vitaGL compiles its fixed-function shaders at runtime: ur0:data/libshacccg.suprx must exist (the
// usual requirement of vitaGL homebrew).
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/message_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vitaGL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "app/game_app.h"
#include "app/platform.h"

// newlib heap: game data, decoded sounds and CPU copies of the textures. vitaGL takes the rest.
int _newlib_heap_size_user = 160 * 1024 * 1024;

namespace dd::app::platform {

namespace {

constexpr const char* kDataDir = "ux0:data/ddvita";
constexpr int kAudioGrain = 1024;

uint32_t g_prev = 0;
bool g_selectCombo = false; // Start was pressed while Select was held
float g_repeat[4] = {0, 0, 0, 0}; // d-pad auto-repeat timers: up, down, left, right
double g_lastPoll = 0;

SceUID g_audioThread = -1;
volatile bool g_audioRun = false;
audio::Mixer* g_mixer = nullptr;

int AudioThread(SceSize, void*) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kAudioGrain, audio::Mixer::kRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return 0;
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, static_cast<SceAudioOutChannelFlag>(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    static int16_t buf[2][kAudioGrain * 2];
    int which = 0;
    while (g_audioRun) {
        g_mixer->Mix(buf[which], kAudioGrain);
        sceAudioOutOutput(port, buf[which]); // blocks until the previous buffer has played
        which ^= 1;
    }
    sceAudioOutOutput(port, nullptr);
    sceAudioOutReleasePort(port);
    return 0;
}

} // namespace

bool Init(int* width, int* height, std::string*) {
    // full speed: the default clocks (333 / 111 MHz) leave little room for the 3D scene
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    sceIoMkdir(kDataDir, 0777);
    *width = 960;
    *height = 544;
    return true;
}

void Shutdown() { sceKernelExitProcess(0); }

bool Poll(std::vector<Input>* inputs, int* width, int* height) {
    *width = 960;
    *height = 544;
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t b = pad.buttons;
    // left stick as a d-pad
    if (pad.ly < 40) b |= SCE_CTRL_UP;
    if (pad.ly > 215) b |= SCE_CTRL_DOWN;
    if (pad.lx < 40) b |= SCE_CTRL_LEFT;
    if (pad.lx > 215) b |= SCE_CTRL_RIGHT;
    uint32_t pressed = b & ~g_prev;
    double now = Seconds();
    float dt = g_lastPoll > 0 ? static_cast<float>(now - g_lastPoll) : 0;
    g_lastPoll = now;

    const uint32_t dirs[4] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
    const Input dirInputs[4] = {Input::Up, Input::Down, Input::Left, Input::Right};
    for (int i = 0; i < 4; i++) {
        if (pressed & dirs[i]) {
            inputs->push_back(dirInputs[i]);
            g_repeat[i] = 0.35f;
        } else if (b & dirs[i]) {
            g_repeat[i] -= dt;
            if (g_repeat[i] <= 0) {
                inputs->push_back(dirInputs[i]);
                g_repeat[i] = 0.1f;
            }
        }
    }
    struct Map {
        uint32_t button;
        Input input;
    };
    const Map map[] = {{SCE_CTRL_CROSS, Input::Confirm},  {SCE_CTRL_CIRCLE, Input::Rest},     {SCE_CTRL_SQUARE, Input::PassTake},
                       {SCE_CTRL_TRIANGLE, Input::Shoot}, {SCE_CTRL_LTRIGGER, Input::Switch}, {SCE_CTRL_RTRIGGER, Input::Info}};
    for (const Map& m : map)
        if (pressed & m.button) inputs->push_back(m.input);
    // Select alone (on release) = undo; Start = pause menu; Select + Start = log / performance readout
    if (pressed & SCE_CTRL_START) {
        if (b & SCE_CTRL_SELECT) {
            inputs->push_back(Input::Log);
            g_selectCombo = true;
        } else {
            inputs->push_back(Input::Menu);
        }
    }
    if ((g_prev & SCE_CTRL_SELECT) && !(b & SCE_CTRL_SELECT)) {
        if (!g_selectCombo) inputs->push_back(Input::Undo);
        g_selectCombo = false;
    }
    g_prev = b;
    // a tap on the rear touch pad (a new touch) toggles the health bars
    static bool touching = false;
    SceTouchData touch{};
    if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1) > 0) {
        bool now = touch.reportNum > 0;
        if (now && !touching) inputs->push_back(Input::Bars);
        touching = now;
    }
    return true;
}

void Swap() { vglSwapBuffers(GL_FALSE); }

double Seconds() { return static_cast<double>(sceKernelGetProcessTimeWide()) / 1e6; }

void StartAudio(audio::Mixer* mixer) {
    g_mixer = mixer;
    g_audioRun = true;
    g_audioThread = sceKernelCreateThread("ddvita_audio", AudioThread, 0x10000100, 0x10000, 0, 0, nullptr);
    if (g_audioThread >= 0) sceKernelStartThread(g_audioThread, 0, nullptr);
}

void StopAudio() {
    if (g_audioThread < 0) return;
    g_audioRun = false;
    sceKernelWaitThreadEnd(g_audioThread, nullptr, nullptr);
    sceKernelDeleteThread(g_audioThread);
    g_audioThread = -1;
}

// ux0:data/ddvita first (data copied by hand), else the data packed in the VPK (ddvita_full.vpk)
std::string DataDir() {
    SceIoStat st;
    if (sceIoGetstat((std::string(kDataDir) + "/gamedata.json").c_str(), &st) >= 0) return kDataDir;
    return "app0:data";
}
std::string SaveDir() { return kDataDir; }

bool SaveScreenshot(const std::string&, int, int) { return false; }

const char* ButtonName(Input in) {
    switch (in) {
    case Input::Up:
    case Input::Down: return "D-pad";
    case Input::Left:
    case Input::Right: return "Left/Right";
    case Input::Info: return "R";
    case Input::Confirm:
    case Input::Yes: return "Cross";
    case Input::Rest:
    case Input::No: return "Circle";
    case Input::PassTake: return "Square";
    case Input::Shoot: return "Triangle";
    case Input::Undo: return "Select";
    case Input::Switch: return "L";
    case Input::Menu: return "Start";
    case Input::Back: return "Circle";
    default: return "?";
    }
}

const char* ButtonSprite(Input in) {
    switch (in) {
    case Input::Confirm:
    case Input::Yes: return "ButtonIcons_PlayStation_Cross";
    case Input::Rest:
    case Input::No:
    case Input::Back: return "ButtonIcons_PlayStation_Circle";
    case Input::PassTake: return "ButtonIcons_PlayStation_Square";
    case Input::Shoot: return "ButtonIcons_PlayStation_Triangle";
    case Input::Undo: return "ButtonIcons_PlayStation3_Select";
    case Input::Menu: return "ButtonIcons_PlayStation3_Start";
    case Input::Switch: return "ButtonIcons_PlayStation_L1";
    case Input::Info: return "ButtonIcons_PlayStation_R1";
    default: return nullptr;
    }
}

bool CanQuit() { return false; }

// the system message dialog over a blank screen, until OK is pressed
void ShowError(const std::string& msg) {
    SceMsgDialogUserMessageParam user{};
    user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg = reinterpret_cast<const SceChar8*>(msg.c_str());
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user;
    if (sceMsgDialogInit(&param) < 0) return;
    while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        glClearColor(0.105f, 0.086f, 0.149f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_TRUE);
    }
    sceMsgDialogTerm();
}

// The user's own copy of the game: "Dungeon Deathball_Data" and UnityPlayer.dll from the PC folder
std::string GameCopyDir() { return std::string(kDataDir) + "/game"; }
std::string FontDir() { return "app0:assets"; }

namespace {
bool g_progressOpen = false;
double g_progressShown = 0;
std::string g_progressMsg;
} // namespace

// the system progress dialog; redrawn at most every 1/10 s (each redraw waits for vblank)
void ShowProgress(const std::string& msg, float fraction) {
    double now = Seconds();
    if (g_progressOpen && msg == g_progressMsg && now - g_progressShown < 0.1) return;
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); // no auto-suspend: nobody touches the console meanwhile
    if (!g_progressOpen) {
        static SceMsgDialogProgressBarParam bar;
        bar = SceMsgDialogProgressBarParam{};
        bar.barType = SCE_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;
        bar.msg = reinterpret_cast<const SceChar8*>("Installing the game data from your copy of Dungeon Deathball.\n"
                                                    "This takes a few minutes, only once.");
        SceMsgDialogParam param;
        sceMsgDialogParamInit(&param);
        param.mode = SCE_MSG_DIALOG_MODE_PROGRESS_BAR;
        param.progBarParam = &bar;
        if (sceMsgDialogInit(&param) < 0) return;
        g_progressOpen = true;
    }
    if (msg != g_progressMsg) {
        g_progressMsg = msg;
        sceMsgDialogProgressBarSetMsg(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, reinterpret_cast<const SceChar8*>(g_progressMsg.c_str()));
    }
    sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT,
                                    static_cast<SceUInt32>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * 100)));
    g_progressShown = now;
    glClearColor(0.105f, 0.086f, 0.149f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_TRUE);
}

void EndProgress() {
    if (!g_progressOpen) return;
    sceMsgDialogClose();
    while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_TRUE);
    }
    sceMsgDialogTerm();
    g_progressOpen = false;
    g_progressMsg.clear();
}

} // namespace dd::app::platform

int main(int argc, char** argv) { return dd::app::RunGame(argc, argv); }
