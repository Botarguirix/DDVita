// PS Vita platform: vitaGL (fixed function on GXM), SceCtrl, SceAudioOut, data on ux0:.
//
// Controls: d-pad / left stick move the cursor, Cross does the tile action (and answers "yes"),
// Circle rests (and answers "no"), Square passes or takes the ball, Triangle shoots, Select undoes,
// L / R switch player, Start cycles the log / performance readout. Quit with the PS button.
//
// Data: ux0:data/ddvita if it holds gamedata.json, else app0:data (inside ddvita_full.vpk). The save
// always goes to ux0:data/ddvita.
//
// vitaGL compiles its fixed-function shaders at runtime: ur0:data/libshacccg.suprx must exist (the
// usual requirement of vitaGL homebrew).
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vitaGL.h>

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
    const Map map[] = {{SCE_CTRL_CROSS, Input::Confirm},  {SCE_CTRL_CIRCLE, Input::Rest},  {SCE_CTRL_SQUARE, Input::PassTake},
                       {SCE_CTRL_TRIANGLE, Input::Shoot}, {SCE_CTRL_SELECT, Input::Undo},  {SCE_CTRL_LTRIGGER, Input::Switch},
                       {SCE_CTRL_RTRIGGER, Input::Switch}, {SCE_CTRL_START, Input::Log}};
    for (const Map& m : map)
        if (pressed & m.button) inputs->push_back(m.input);
    g_prev = b;
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
    case Input::Confirm:
    case Input::Yes: return "Cross";
    case Input::Rest:
    case Input::No: return "Circle";
    case Input::PassTake: return "Square";
    case Input::Shoot: return "Triangle";
    case Input::Undo: return "Select";
    case Input::Switch: return "L/R";
    case Input::Quit: return "PS";
    default: return "?";
    }
}

} // namespace dd::app::platform

int main(int argc, char** argv) { return dd::app::RunGame(argc, argv); }
