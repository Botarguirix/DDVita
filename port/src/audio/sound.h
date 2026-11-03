// Game audio: SoundCues, the Jukebox (stage playlist, boss / out-of-time / triumphant tracks,
// jingles) and the sounds the original plays for each simulation event.
//
// data/audio comes from tools/export_audio.py (the user's own copy of the game).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "sim/sim.h"

namespace dd::audio {

class Sound {
public:
    bool Load(const std::string& dir, Mixer* mixer, std::string* err);
    bool loaded() const { return mixer_ != nullptr; }

    // SoundCue: random clip, pitch and volume
    void PlayCue(const std::string& cue, Mixer::Group group = Mixer::SoundGroup);
    void PlayClip(const std::string& clip, float volume = 1, float pitch = 1, Mixer::Group group = Mixer::SoundGroup);
    void PlayNamed(const std::string& key); // a clip bound to a scene component (audio.json "named")

    // ---- Jukebox ----
    void Shuffle(int unlockedPacks);        // JukeboxShuffler.GetShuffledPlaylist (pack A + unlocked)
    void IncrementStageMusicIndex();
    void PlayStageMusic(bool bossStage, float delay);
    void PlayOutOfTimeMusic();
    void PlayTriumphantMusic();
    void FadeOutMusic(float seconds);
    void PlayJingle(const std::string& namedKey);
    void Update(float dt); // Jukebox.SwapTrackCoroutine

    // ---- gameplay ----
    void StageStarted(sim::Game& g);              // StageStartSequences jingles + stage music
    void OnEvent(const sim::Event& e, sim::Game& g); // called as the view plays each event
    void CommandPressed(bool tooExpensive);       // CommandObject.Select
    void PlayerSwitched();                        // PlayerManager.playerSwitchSound

private:
    struct Cue {
        float pitch = 1, pitchVar = 0, vol = 1, volVar = 0;
        std::vector<std::string> clips;
    };
    Mixer* mixer_ = nullptr;
    std::string dir_;
    std::map<std::string, Cue> cues_;
    std::map<std::string, std::string> clipFiles_, named_, enemyDeath_, attack_;
    std::vector<std::vector<std::string>> packs_;
    std::map<std::string, std::shared_ptr<PcmClip>> cache_;
    std::vector<std::string> playlist_;
    int stageIndex_ = 0;
    std::string currentTrack_;
    float triumphantPoint_ = 0;
    std::string swapTo_;
    float swapTimer_ = -1;
    bool swapFadeIn_ = false;
    bool switchedToTriumphant_ = false;
    std::map<int, bool> lowStamina_; // Player.staminaWasLowOnLastCheck
    uint32_t rng_ = 0x2545F491u;

    float Rand(); // cosmetic (UnityEngine.Random in the original)
    std::shared_ptr<PcmClip> Clip(const std::string& name);
    std::string ClipPath(const std::string& name) const;
    void PlayMusicTrack(const std::string& clip, float fadeIn);
    void SwapTrack(const std::string& clip, bool fadeIn);
    void Voice(const sim::Player* p, const char* event, bool important);
    bool IsTriumphant(sim::Game& g) const;
};

} // namespace dd::audio
