// PC platform: SDL2 window with a desktop OpenGL context, keyboard / game controller, SDL audio.
//
// Controls (keyboard / gamepad):
//   arrows / d-pad   move the tile cursor        Enter, Space / A (Cross)   do the action on that tile
//   Q / X (Square)   pass or take the ball        E / Y (Triangle)          shoot
//   R / B (Circle)   rest                         U / Back (Select)         undo
//   Tab / L, R       switch player                Y / N                     answer the undo suggestion
//   F1               log / performance readout    Esc / Start               pause menu
//   Backspace        back (menus)
#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "app/game_app.h"
#include "app/platform.h"

namespace dd::app::platform {

namespace {

SDL_Window* g_win = nullptr;
SDL_GLContext g_ctx = nullptr;
SDL_AudioDeviceID g_audio = 0;

Input FromKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return Input::Up;
    case SDLK_DOWN: return Input::Down;
    case SDLK_LEFT: return Input::Left;
    case SDLK_RIGHT: return Input::Right;
    case SDLK_RETURN:
    case SDLK_SPACE: return Input::Confirm;
    case SDLK_q: return Input::PassTake;
    case SDLK_e: return Input::Shoot;
    case SDLK_r: return Input::Rest;
    case SDLK_u: return Input::Undo;
    case SDLK_TAB: return Input::Switch;
    case SDLK_y: return Input::Yes;
    case SDLK_n: return Input::No;
    case SDLK_F1: return Input::Log;
    case SDLK_ESCAPE: return Input::Menu;
    case SDLK_BACKSPACE: return Input::Back;
    case SDLK_i: return Input::Info;
    default: return Input::None;
    }
}

Input FromButton(Uint8 b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return Input::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Input::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Input::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Input::Right;
    case SDL_CONTROLLER_BUTTON_A: return Input::Confirm;
    case SDL_CONTROLLER_BUTTON_X: return Input::PassTake;
    case SDL_CONTROLLER_BUTTON_Y: return Input::Shoot;
    case SDL_CONTROLLER_BUTTON_B: return Input::Rest;
    case SDL_CONTROLLER_BUTTON_BACK: return Input::Undo;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Input::Switch;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Input::Info;
    case SDL_CONTROLLER_BUTTON_START: return Input::Menu;
    default: return Input::None;
    }
}

void AudioCallback(void* user, Uint8* stream, int len) {
    static_cast<audio::Mixer*>(user)->Mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

} // namespace

bool Init(int* width, int* height, std::string* err) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        *err = SDL_GetError();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // DDVITA_WINDOW=960x544 previews the Vita's layout (480x272 virtual)
    int ww = 1280, wh = 720;
    if (const char* s = std::getenv("DDVITA_WINDOW")) {
        int w = 0, h = 0;
        if (std::sscanf(s, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 200) ww = w, wh = h;
    }
    g_win = SDL_CreateWindow("Dungeon Deathball (Vita port, PC preview)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_win || !(g_ctx = SDL_GL_CreateContext(g_win))) {
        *err = SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
    SDL_GL_GetDrawableSize(g_win, width, height);
    return true;
}

void Shutdown() {
    if (g_ctx) SDL_GL_DeleteContext(g_ctx);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

bool Poll(std::vector<Input>* inputs, int* width, int* height) {
    bool alive = true;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) alive = false;
        else if (ev.type == SDL_KEYDOWN) inputs->push_back(FromKey(ev.key.keysym.sym));
        else if (ev.type == SDL_CONTROLLERBUTTONDOWN) inputs->push_back(FromButton(ev.cbutton.button));
        else if (ev.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(ev.cdevice.which);
    }
    SDL_GL_GetDrawableSize(g_win, width, height);
    return alive;
}

void Swap() { SDL_GL_SwapWindow(g_win); }

double Seconds() { return static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency(); }

void StartAudio(audio::Mixer* mixer) {
    SDL_AudioSpec want{}, have{};
    want.freq = audio::Mixer::kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = AudioCallback;
    want.userdata = mixer;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!g_audio) {
        std::fprintf(stderr, "audio: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(g_audio, 0);
}

void StopAudio() {
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    g_audio = 0;
}

std::string DataDir() { return "data"; }
std::string SaveDir() { return "."; }

bool SaveScreenshot(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; y++)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + static_cast<size_t>(y) * s->pitch, &px[static_cast<size_t>(h - 1 - y) * w * 4],
                    static_cast<size_t>(w) * 4);
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

const char* ButtonName(Input in) {
    switch (in) {
    case Input::Up:
    case Input::Down: return "Arrows";
    case Input::Left:
    case Input::Right: return "Left/Right";
    case Input::Info: return "I";
    case Input::Confirm: return "Enter";
    case Input::PassTake: return "Q";
    case Input::Shoot: return "E";
    case Input::Rest: return "R";
    case Input::Undo: return "U";
    case Input::Switch: return "Tab";
    case Input::Yes: return "Y";
    case Input::No: return "N";
    case Input::Menu: return "Esc";
    case Input::Back: return "Backspace";
    default: return "?";
    }
}

const char* ButtonSprite(Input in) {
    switch (in) {
    case Input::Confirm: return "ButtonIcons_PC_Space";
    case Input::PassTake: return "ButtonIcons_PC_AlphaQ";
    case Input::Shoot: return "ButtonIcons_PC_AlphaE";
    case Input::Rest: return "ButtonIcons_PC_AlphaR";
    case Input::Undo: return "ButtonIcons_PC_AlphaU";
    case Input::Switch: return "ButtonIcons_PC_Tab";
    case Input::Menu: return "ButtonIcons_PC_Escape";
    case Input::Info: return "ButtonIcons_PC_AlphaI";
    default: return nullptr;
    }
}

bool CanQuit() { return true; }

void ShowError(const std::string& msg) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dungeon Deathball", msg.c_str(), g_win); }

// on PC the data comes from tools/prepare_data.py (instalar.bat)
std::string GameCopyDir() { return std::string(); }
std::string FontDir() { return std::string(); }
void ShowProgress(const std::string&, float) {}
void EndProgress() {}

} // namespace dd::app::platform

int main(int argc, char** argv) { return dd::app::RunGame(argc, argv); }
