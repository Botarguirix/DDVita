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
    suggestUndo = j.value("suggestUndo", suggestUndo);
    return true;
}

bool Settings::Save(const std::string& path) const {
    json j = {{"master", master}, {"music", music}, {"jingles", jingles}, {"sounds", sounds}, {"voices", voices}, {"suggestUndo", suggestUndo}};
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
