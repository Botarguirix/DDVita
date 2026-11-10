// Options (AppSettingsDeathball): volumes and Suggest Undo, in settings.json.
#include <fstream>

#include <nlohmann/json.hpp>

#include "app/app.h"

namespace dd::app {

using json = nlohmann::json;

bool Settings::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return false;
    auto vol = [&](const char* k, float* v) {
        if (j.contains(k) && j[k].is_number()) *v = std::min(1.0f, std::max(0.0f, j[k].get<float>()));
    };
    vol("master", &master);
    vol("music", &music);
    vol("jingles", &jingles);
    vol("sounds", &sounds);
    vol("voices", &voices);
    vol("crowd", &crowd);
    auto flag = [&](const char* k, bool* v) {
        if (j.contains(k) && j[k].is_boolean()) *v = j[k].get<bool>();
    };
    flag("suggestUndo", &suggestUndo);
    flag("autoSkipCinematics", &autoSkipCinematics);
    flag("healthBars", &healthBars);
    flag("fastMode", &fastMode);
    flag("dynamicCamera", &dynamicCamera);
    flag("greyBlood", &greyBlood);
    flag("actionPopups", &actionPopups);
    flag("commandCostPopups", &commandCostPopups);
    flag("menuPopups", &menuPopups);
    flag("enemyPopups", &enemyPopups);
    flag("tilePopups", &tilePopups);
    flag("pixelFilter", &pixelFilter);
    flag("smoothFonts", &smoothFonts);
    return true;
}

bool Settings::Save(const std::string& path) const {
    json j = {{"master", master}, {"music", music}, {"jingles", jingles}, {"sounds", sounds}, {"voices", voices}, {"crowd", crowd}, {"suggestUndo", suggestUndo},
              {"autoSkipCinematics", autoSkipCinematics}, {"healthBars", healthBars},
              {"fastMode", fastMode}, {"dynamicCamera", dynamicCamera}, {"greyBlood", greyBlood}, {"actionPopups", actionPopups},
              {"commandCostPopups", commandCostPopups}, {"menuPopups", menuPopups}, {"enemyPopups", enemyPopups}, {"tilePopups", tilePopups},
              {"pixelFilter", pixelFilter}, {"smoothFonts", smoothFonts}};
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << j.dump(1);
    return static_cast<bool>(f);
}

void Settings::Apply(audio::Mixer& mixer) const {
    mixer.SetVolume(audio::Mixer::MasterGroup, master);
    mixer.SetVolume(audio::Mixer::MusicGroup, music);
    mixer.SetVolume(audio::Mixer::JingleGroup, jingles);
    mixer.SetVolume(audio::Mixer::SoundGroup, sounds);
    mixer.SetVolume(audio::Mixer::VoiceGroup, voices);
}

} // namespace dd::app
