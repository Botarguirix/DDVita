// data/audio as tools/export_audio.py writes it: audio.json (cues, music packs, voices, named
// clips, crowd) and clips/<name>.dda (IMA ADPCM). The AudioClips are FSB5 banks with Vorbis
// streams whose setup headers FMOD keeps by CRC32 inside UnityPlayer.dll.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "install/extract.h"

namespace dd::install {

// Vorbis setup headers found in UnityPlayer.dll, by CRC32
class VorbisSetups {
public:
    // scans the DLL for the setup packets with these CRCs; false if the file cannot be read
    bool Load(const std::string& dllPath, const std::vector<uint32_t>& crcs);
    const Bytes* Find(uint32_t crc) const;

private:
    std::map<uint32_t, Bytes> setups_;
};

struct Pcm {
    int rate = 0, channels = 0;
    std::vector<int16_t> samples; // interleaved
    size_t frames() const { return channels ? samples.size() / static_cast<size_t>(channels) : 0; }
};

// CRC32 of the Vorbis setup header an FSB5 bank needs (0 if it is not Vorbis)
uint32_t FsbVorbisCrc(const uint8_t* data, size_t size);
// First subsound of an FSB5 bank as 16-bit PCM, converted like UnityPy (float * 32768, truncated)
bool DecodeFsb(const uint8_t* data, size_t size, const VorbisSetups& setups, Pcm* out, std::string* err);

// dll: the game's UnityPlayer.dll. progress(done, total) while the clips are encoded.
bool ExportAudio(const GameFiles& g, const Extracted& ex, const std::string& dll, const std::string& outDir,
                 std::string* err, const std::function<void(int, int)>& progress = {});

} // namespace dd::install
