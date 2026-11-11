// Software mixer: 48 kHz stereo, int16. Sound effects are decoded to memory; music and jingles are
// streamed from .dda files (IMA ADPCM blocks written by tools/export_audio.py).
//
// The platform pulls audio with Mix() from its own thread (SDL callback / sceAudioOut thread); every
// other method is called from the game thread. A mutex guards the shared state.
#pragma once
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dd::audio {

struct PcmClip {
    int rate = 48000, channels = 1;
    int frames = 0;
    std::vector<int16_t> pcm; // interleaved
};

// Decodes a whole .dda file.
bool LoadDda(const std::string& path, PcmClip* out);

// Sequential reader of a .dda file (one block of up to 4096 frames decoded at a time).
class DdaStream {
public:
    ~DdaStream();
    bool Open(const std::string& path);
    void Seek(int frame);
    // Next frame as stereo; false at the end.
    bool Next(int16_t* l, int16_t* r);
    int rate() const { return rate_; }
    int frames() const { return frames_; }
    int position() const { return pos_; }

private:
    FILE* f_ = nullptr;
    int rate_ = 0, channels_ = 0, frames_ = 0, blocks_ = 0;
    long dataStart_ = 0;
    int block_ = -1, pos_ = 0;
    std::vector<int16_t> buf_; // decoded block, interleaved
    std::vector<uint8_t> raw_;
    bool LoadBlock(int b);
};

class Mixer {
public:
    static constexpr int kRate = 48000;
    enum Stream { Music, Jingle, Crowd, kStreams }; // Crowd: CrowdAudio loop (SFX_Crowd, in SoundGroup)
    // AudioMixer groups of the original (the Music stream plays in MusicGroup, Jingle in JingleGroup)
    enum Group { MasterGroup, MusicGroup, JingleGroup, SoundGroup, VoiceGroup, kGroups };

    // Called from the audio thread: fills frames * 2 samples.
    void Mix(int16_t* out, int frames);

    void Play(std::shared_ptr<const PcmClip> clip, float volume, float pitch, Group group = SoundGroup);
    // Starts a stream (replacing the one in that slot). fadeIn > 0 starts silent and fades up.
    void PlayStream(Stream s, const std::string& path, bool loop, float startSeconds, float fadeIn, float delay = 0);
    // Fades the stream volume to target over seconds; stops it at the end if stop is set.
    void FadeStream(Stream s, float target, float seconds, bool stop);
    void StopStream(Stream s);
    bool StreamPlaying(Stream s);
    float StreamSeconds(Stream s); // playback position
    // Volume slider (0..1) of a group. VolumeLevels sets the mixer to 20*log10(v*v) dB, a gain of v*v.
    void SetVolume(Group group, float slider);

private:
    struct Voice {
        std::shared_ptr<const PcmClip> clip;
        double pos = 0, step = 1;
        float vol = 1;
        Group group = SoundGroup;
    };
    struct StreamSlot {
        std::unique_ptr<DdaStream> dda;
        bool loop = false, stopAtTarget = false;
        double pos = 0, step = 1; // fractional source position for resampling
        int16_t cur[2] = {0, 0}, next[2] = {0, 0};
        bool primed = false;
        float gain = 1, target = 1, gainStep = 0;
        int delayFrames = 0;
    };
    std::mutex m_;
    std::vector<Voice> voices_;
    StreamSlot streams_[kStreams];
    // AppSettingsDeathball defaults: 0.7 master / music / jingles / sounds, 1 voices
    float gain_[kGroups] = {0.49f, 0.49f, 0.49f, 0.49f, 1.0f};
    std::vector<float> acc_;
    void MixStream(StreamSlot& s, float* acc, int frames, float vol);
};

} // namespace dd::audio
