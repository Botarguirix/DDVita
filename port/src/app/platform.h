// What the game needs from the system: a GL context, input, a clock, audio output and paths.
// platform_sdl.cpp (PC) and platform_vita.cpp (PS Vita: vitaGL, SceCtrl, SceAudio) implement it.
#pragma once
#include <string>
#include <vector>

#include "audio/mixer.h"

namespace dd::app {

// Menu: Start / Esc (pause, open and close menus). Back: Circle / Backspace in menus (Rest also
// backs out of menus, as Circle does on the Vita). Info: R / I, the info popup of the tile under
// the cursor.
enum class Input { None, Up, Down, Left, Right, Confirm, PassTake, Shoot, Rest, Undo, Switch, Yes, No, Log, Quit, Menu, Back, Info, Bars };

namespace platform {

bool Init(int* width, int* height, std::string* err); // window + GL context
void Shutdown();
// Appends this frame's inputs; false when the app must close. Updates the drawable size.
bool Poll(std::vector<Input>* inputs, int* width, int* height);
void Swap();
double Seconds();                       // monotonic clock
void StartAudio(audio::Mixer* mixer);   // the platform's audio thread pulls from the mixer
void StopAudio();
std::string DataDir();                  // gamedata.json, render/, audio/
std::string SaveDir();                  // run.json
bool SaveScreenshot(const std::string& path, int w, int h);
const char* ButtonName(Input in);       // for the HUD ("[Q]" on PC, "Square" on Vita...)
const char* ButtonSprite(Input in);     // the game's button icon for it (nullptr: use ButtonName)
bool CanQuit();                         // a Quit entry on the title screen (PC only)
void ShowError(const std::string& msg); // blocking system message box (missing or outdated data)
// The data installer (install/installer.h): where the user copies their own game ("" when the
// platform installs no data), the port's HUD font it adds, and its progress on screen.
std::string GameCopyDir();
std::string FontDir();
void ShowProgress(const std::string& msg, float fraction); // called often: the platform throttles it
void EndProgress();

} // namespace platform
} // namespace dd::app
