// What the game needs from the system: a GL context, input, a clock, audio output and paths.
// platform_sdl.cpp (PC) and platform_vita.cpp (PS Vita: vitaGL, SceCtrl, SceAudio) implement it.
#pragma once
#include <string>
#include <vector>

#include "audio/mixer.h"

namespace dd::app {

enum class Input { None, Up, Down, Left, Right, Confirm, PassTake, Shoot, Rest, Undo, Switch, Yes, No, Log, Quit };

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

} // namespace platform
} // namespace dd::app
