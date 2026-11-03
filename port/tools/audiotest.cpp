// Audio test without a sound card: plays a stage with a seeded random player, feeds its events to
// Sound like the game does (one command every 0.4 s), mixes the result into a WAV file and prints
// the level of every second.
//
//   audiotest <data dir> <seed> <seconds> <out.wav>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "audio/sound.h"
#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/session_generator.h"
#include "sim/sim.h"

using namespace dd;
using namespace dd::sim;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: audiotest <data dir> <seed> <seconds> <out.wav>\n");
        return 2;
    }
    std::string dir = argv[1], err;
    int seed = std::atoi(argv[2]), seconds = std::atoi(argv[3]);
    GameData data;
    if (!data.Load(dir + "/gamedata.json", &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    audio::Mixer mixer;
    audio::Sound sound;
    if (!sound.Load(dir + "/audio", &mixer, &err)) {
        std::fprintf(stderr, "audio: %s\n", err.c_str());
        return 1;
    }
    SessionRequest req;
    req.buildVersion = data.buildVersion;
    req.date = {2025, 6, 1};
    req.seed = seed;
    req.team = TeamForMode(data, GameMode::Normal, 0);
    Run run(data, GenerateSession(data, req), req.team, req.mode);
    Game g(run);
    g.Start();
    sound.Shuffle(1);
    sound.StageStarted(g);
    MonoRandom chooser(seed);
    size_t fed = 0;

    constexpr int kChunk = 960; // 20 ms
    std::vector<int16_t> out, buf(kChunk * 2);
    float sinceCommand = 0;
    for (int chunk = 0; chunk < seconds * 50; chunk++) {
        sinceCommand += 0.02f;
        if (sinceCommand >= 0.4f && g.phase() != Phase::StageEnded) {
            sinceCommand = 0;
            if (g.phase() == Phase::SuggestUndo) {
                g.RespondToUndoSuggestion(false);
            } else {
                std::vector<Command> cmds = g.GetCommands(), ok;
                for (const Command& c : cmds)
                    if (c.interactable && !c.tooExpensive) ok.push_back(c);
                if (!ok.empty()) {
                    sound.CommandPressed(false);
                    g.Select(ok[static_cast<size_t>(chooser.Next(0, static_cast<int>(ok.size())))]);
                }
            }
        }
        for (; fed < g.events.size(); fed++) sound.OnEvent(g.events[fed], g);
        sound.Update(0.02f);
        mixer.Mix(buf.data(), kChunk);
        out.insert(out.end(), buf.begin(), buf.end());
    }

    for (int s = 0; s < seconds; s++) {
        double sum = 0;
        int peak = 0;
        for (size_t i = static_cast<size_t>(s) * audio::Mixer::kRate * 2; i < static_cast<size_t>(s + 1) * audio::Mixer::kRate * 2; i++) {
            sum += static_cast<double>(out[i]) * out[i];
            peak = std::max(peak, std::abs(static_cast<int>(out[i])));
        }
        std::printf("%2ds rms %6.0f peak %5d\n", s, std::sqrt(sum / (audio::Mixer::kRate * 2)), peak);
    }
    std::ofstream f(argv[4], std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = static_cast<uint32_t>(out.size() * 2);
    f.write("RIFF", 4), u32(36 + bytes), f.write("WAVEfmt ", 8), u32(16), u16(1), u16(2);
    u32(audio::Mixer::kRate), u32(audio::Mixer::kRate * 4), u16(4), u16(16), f.write("data", 4), u32(bytes);
    f.write(reinterpret_cast<const char*>(out.data()), bytes);
    std::printf("events %zu, stage %s\n", g.events.size(), g.phase() == Phase::StageEnded ? "ended" : "running");
    return 0;
}
