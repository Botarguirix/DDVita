#include "install/export_audio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>

#include "install/export_data.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

namespace dd::install {

namespace {

const json& At(const json& o, const char* k) {
    static const json null;
    if (!o.is_object()) return null;
    auto it = o.find(k);
    return it == o.end() ? null : *it;
}

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24; }
uint64_t U64(const uint8_t* p) { return U32(p) | static_cast<uint64_t>(U32(p + 4)) << 32; }

// zlib's CRC32 (the key FMOD files its Vorbis setups under)
uint32_t Crc32Update(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    return crc;
}

// Ogg's page checksum
uint32_t OggCrc(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; k++) r = r & 0x80000000u ? (r << 1) ^ 0x04C11DB7u : r << 1;
            table[i] = r;
        }
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 255];
    return crc;
}

// One Ogg page holding one whole packet
bool AddPage(Bytes* ogg, const uint8_t* packet, size_t n, uint8_t flags, uint64_t granule, uint32_t seq) {
    if (n >= 255 * 255) return false;
    size_t at = ogg->size();
    size_t segs = n / 255 + 1;
    ogg->insert(ogg->end(), {'O', 'g', 'g', 'S', 0, flags});
    for (int i = 0; i < 8; i++) ogg->push_back(static_cast<uint8_t>(granule >> (8 * i)));
    for (uint32_t v : {0x44445641u, seq, 0u}) // serial "AVDD", sequence, crc (filled below)
        for (int i = 0; i < 4; i++) ogg->push_back(static_cast<uint8_t>(v >> (8 * i)));
    ogg->push_back(static_cast<uint8_t>(segs));
    for (size_t i = 0; i + 1 < segs; i++) ogg->push_back(255);
    ogg->push_back(static_cast<uint8_t>(n % 255));
    ogg->insert(ogg->end(), packet, packet + n);
    uint32_t crc = OggCrc(ogg->data() + at, ogg->size() - at);
    for (int i = 0; i < 4; i++) (*ogg)[at + 22 + static_cast<size_t>(i)] = static_cast<uint8_t>(crc >> (8 * i));
    return true;
}

struct FsbSample {
    int format = 0, channels = 0, rate = 0;
    uint64_t frames = 0;
    uint32_t vorbisCrc = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// The first subsound of an FSB5 bank
bool ParseFsb(const uint8_t* p, size_t n, FsbSample* s) {
    if (n < 0x48 || std::memcmp(p, "FSB5", 4) != 0) return false;
    uint32_t version = U32(p + 4), count = U32(p + 8), headers = U32(p + 12), names = U32(p + 16), dataSize = U32(p + 20);
    s->format = static_cast<int>(U32(p + 24));
    size_t at = version == 1 ? 0x3C : 0x40;
    size_t dataStart = at + headers + names;
    if (count == 0 || dataStart > n) return false;
    uint64_t raw = U64(p + at);
    at += 8;
    static const int rates[] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
    unsigned freq = static_cast<unsigned>(raw >> 1) & 15;
    s->rate = freq < 11 ? rates[freq] : 44100;
    s->channels = static_cast<int>((raw >> 5) & 1) + 1;
    size_t offset = static_cast<size_t>((raw >> 6) & 0x0FFFFFFF) * 16;
    s->frames = (raw >> 34) & 0x3FFFFFFF;
    for (bool more = raw & 1; more;) {
        if (at + 4 > n) return false;
        uint32_t c = U32(p + at);
        more = c & 1;
        size_t len = (c >> 1) & 0xFFFFFF;
        unsigned type = (c >> 25) & 0x7F;
        if (at + 4 + len > n) return false;
        if (type == 1 && len >= 1) s->channels = p[at + 4];
        if (type == 2 && len >= 4) s->rate = static_cast<int>(U32(p + at + 4));
        if (type == 11 && len >= 4) s->vorbisCrc = U32(p + at + 4);
        at += 4 + len;
    }
    size_t end = dataSize;
    if (count > 1 && at + 8 <= n) end = static_cast<size_t>((U64(p + at) >> 6) & 0x0FFFFFFF) * 16;
    if (dataStart + end > n || offset > end) return false;
    s->data = p + dataStart + offset;
    s->size = end - offset;
    return true;
}

// extract_assets.safe (file names of extracted/audio): what is not a clean name gets no WAV there
std::string SafeFileName(const std::string& name) {
    std::string s;
    for (char c : name) s += (std::string("<>:\"/\\|?*").find(c) != std::string::npos || static_cast<unsigned char>(c) < 0x20) ? '_' : c;
    size_t b = s.find_first_not_of(" \t\n\r\v\f"), e = s.find_last_not_of(" \t\n\r\v\f");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// export_audio.safe: clip file names in data/audio/clips
std::string ClipFile(const std::string& name) {
    std::string s;
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        bool keep = u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || c == '-' || c == '_' || c == '.';
        s += keep ? c : '_';
    }
    return s;
}

constexpr int kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,    34,    37,
    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,
    1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int kIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr size_t kBlock = 4096;

// audioop.lin2adpcm on one channel of one block; codes low nibble first, an odd last code left as 0
void EncodeBlock(const int16_t* pcm, size_t stride, size_t n, int* pred, int* idx, std::string* out) {
    uint8_t byte = 0;
    for (size_t i = 0; i < n; i++) {
        int s = pcm[i * stride], step = kStep[*idx], diff = s - *pred, code = 0;
        if (diff < 0) code = 8, diff = -diff;
        int vp = step >> 3;
        if (diff >= step) code |= 4, diff -= step, vp += step;
        step >>= 1;
        if (diff >= step) code |= 2, diff -= step, vp += step;
        step >>= 1;
        if (diff >= step) code |= 1, vp += step;
        *pred = std::clamp(code & 8 ? *pred - vp : *pred + vp, -32768, 32767);
        *idx = std::clamp(*idx + kIndex[code & 7], 0, 88);
        if (i % 2 == 0) {
            byte = static_cast<uint8_t>(i + 1 < n ? code : 0);
        } else {
            out->push_back(static_cast<char>(byte | code << 4));
        }
    }
    if (n % 2) out->push_back(static_cast<char>(byte));
}

std::string EncodeDda(const Pcm& pcm) {
    size_t frames = pcm.frames(), ch = static_cast<size_t>(pcm.channels), blocks = (frames + kBlock - 1) / kBlock;
    std::string out = "DDAU";
    auto put = [&](uint64_t v, int bytes) {
        for (int i = 0; i < bytes; i++) out.push_back(static_cast<char>(v >> (8 * i)));
    };
    put(static_cast<uint32_t>(pcm.rate), 4), put(ch, 2), put(0, 2), put(frames, 4), put(blocks, 4);
    std::vector<int> pred(ch, 0), idx(ch, 0);
    for (size_t b = 0; b < blocks; b++) {
        size_t start = b * kBlock, end = std::min(frames, start + kBlock);
        for (size_t c = 0; c < ch; c++) {
            put(static_cast<uint16_t>(static_cast<int16_t>(pred[c])), 2), put(static_cast<uint32_t>(idx[c]), 1), put(0, 1);
            EncodeBlock(pcm.samples.data() + start * ch + c, ch, end - start, &pred[c], &idx[c], &out);
        }
    }
    return out;
}

} // namespace

bool VorbisSetups::Load(const std::string& dllPath, const std::vector<uint32_t>& crcs) {
    Bytes dll;
    if (!ReadFile(dllPath, &dll)) return false;
    std::set<uint32_t> want(crcs.begin(), crcs.end());
    // setup packets start with "\x05vorbis"; each is tried at every length up to the next one
    std::vector<size_t> starts;
    static const uint8_t tag[7] = {5, 'v', 'o', 'r', 'b', 'i', 's'};
    for (size_t i = 0; i + 7 <= dll.size(); i++)
        if (dll[i] == 5 && std::memcmp(dll.data() + i, tag, 7) == 0) starts.push_back(i);
    for (size_t k = 0; k < starts.size() && setups_.size() < want.size(); k++) {
        size_t end = std::min(dll.size(), k + 1 < starts.size() ? starts[k + 1] : starts[k] + 16384);
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t n = starts[k]; n < end; n++) {
            crc = Crc32Update(crc, dll[n]);
            uint32_t c = ~crc;
            if (want.count(c) && !setups_.count(c)) setups_[c] = Bytes(dll.begin() + static_cast<long>(starts[k]), dll.begin() + static_cast<long>(n + 1));
        }
    }
    return true;
}

const Bytes* VorbisSetups::Find(uint32_t crc) const {
    auto it = setups_.find(crc);
    return it == setups_.end() ? nullptr : &it->second;
}

uint32_t FsbVorbisCrc(const uint8_t* data, size_t size) {
    FsbSample s;
    return ParseFsb(data, size, &s) && s.format == 15 ? s.vorbisCrc : 0;
}

bool DecodeFsb(const uint8_t* data, size_t size, const VorbisSetups& setups, Pcm* out, std::string* err) {
    FsbSample s;
    if (!ParseFsb(data, size, &s)) return *err = "not an FSB5 bank", false;
    out->rate = s.rate, out->channels = s.channels;
    size_t frames = static_cast<size_t>(s.frames), ch = static_cast<size_t>(s.channels);
    out->samples.assign(frames * ch, 0);
    if (s.format == 2 || s.format == 1) { // PCM16 / PCM8
        size_t width = s.format == 2 ? 2 : 1;
        for (size_t i = 0; i < frames * ch && (i + 1) * width <= s.size; i++)
            out->samples[i] = width == 2 ? static_cast<int16_t>(U16(s.data + i * 2)) : static_cast<int16_t>(static_cast<int8_t>(s.data[i]) * 256);
        return true;
    }
    if (s.format != 15) return *err = "FSB format " + std::to_string(s.format) + " not supported", false;
    const Bytes* setup = setups.Find(s.vorbisCrc);
    if (!setup) return *err = "no Vorbis setup for this clip in UnityPlayer.dll", false;
    // The headers FSB leaves out (FMOD's blocks are 256 / 2048 samples), then one page per packet;
    // the last page's granule position trims the end
    uint8_t id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, static_cast<uint8_t>(s.channels)};
    for (int i = 0; i < 4; i++) id[12 + i] = static_cast<uint8_t>(static_cast<uint32_t>(s.rate) >> (8 * i));
    id[28] = 0xB8, id[29] = 1;
    static const uint8_t comment[] = {3, 'v', 'o', 'r', 'b', 'i', 's', 5, 0, 0, 0, 'd', 'd', 'v', 'i', 't', 0, 0, 0, 0, 1};
    Bytes ogg;
    uint32_t seq = 0;
    AddPage(&ogg, id, sizeof id, 2, 0, seq++);
    AddPage(&ogg, comment, sizeof comment, 0, 0, seq++);
    if (!AddPage(&ogg, setup->data(), setup->size(), 0, 0, seq++)) return *err = "setup too large", false;
    std::vector<std::pair<size_t, size_t>> packets;
    for (size_t at = 0; at + 2 <= s.size;) {
        size_t n = U16(s.data + at);
        if (n == 0 || at + 2 + n > s.size) break;
        packets.emplace_back(at + 2, n);
        at += 2 + n;
    }
    for (size_t i = 0; i < packets.size(); i++) {
        bool last = i + 1 == packets.size();
        if (!AddPage(&ogg, s.data + packets[i].first, packets[i].second, last ? 4 : 0, last ? s.frames : ~0ull, seq++))
            return *err = "packet too large", false;
    }
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_memory(ogg.data(), static_cast<int>(ogg.size()), &error, nullptr);
    if (!v) return *err = "Vorbis error " + std::to_string(error), false;
    size_t done = 0;
    float** outputs = nullptr;
    int got, chans = 0;
    while (done < frames && (got = stb_vorbis_get_frame_float(v, &chans, &outputs)) > 0) {
        size_t n = std::min(frames - done, static_cast<size_t>(got));
        for (size_t c = 0; c < ch && c < static_cast<size_t>(chans); c++)
            for (size_t i = 0; i < n; i++) {
                // fmod_toolkit: float32 * 32768, clipped, truncated to int16
                float x = std::clamp(outputs[c][i] * 32768.0f, -32768.0f, 32767.0f);
                out->samples[(done + i) * ch + c] = static_cast<int16_t>(x);
            }
        done += n;
    }
    stb_vorbis_close(v);
    return true;
}

bool ExportAudio(const GameFiles& g, const Extracted& ex, const std::string& dll, const std::string& outDir,
                 std::string* err, const std::function<void(int, int)>& progress) {
    auto configs = [&](const char* cls) -> const std::vector<json>& {
        static const std::vector<json> none;
        auto it = ex.configs.find(cls);
        return it == ex.configs.end() ? none : it->second;
    };
    auto fileOf = [](const json& o) { return At(o, "_file").is_string() ? At(o, "_file").get<std::string>() : std::string(); };
    auto idOf = [&](const json& o) { return fileOf(o) + ":" + std::to_string(At(o, "_path_id").get<int64_t>()); };
    std::set<std::string> used;
    // an AudioClip PPtr -> the clip's name (null for anything else)
    auto clipName = [&](const std::string& file, const json& ref) -> json {
        auto hit = ex.names.find(ex.Key(g, file, ref));
        if (hit == ex.names.end() || hit->second.first != "AudioClip" || !hit->second.second.is_string()) return nullptr;
        used.insert(hit->second.second.get<std::string>());
        return hit->second.second;
    };

    // SoundCues, player voices by their Resources path
    std::unordered_map<std::string, std::string> cueKey, voiceCue;
    for (const json& c : configs("SoundCue")) cueKey[idOf(c)] = At(c, "m_Name").get<std::string>();
    static const std::map<std::string, std::string> events = {{"attack", "Attack"},   {"death", "Death"},
                                                              {"lowstamina", "LowStamina"}, {"move", "Move"},
                                                              {"movewithball", "MoveWithBall"}, {"rest", "Rest"}};
    for (ObjRef o : g.Objects(147)) {
        json rm;
        if (!g.Read(o, &rm)) continue;
        for (const json& entry : At(rm, "m_Container")) {
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string()) continue;
            std::string path = entry[0].get<std::string>();
            for (char& ch : path)
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            std::vector<std::string> parts;
            for (size_t a = 0, b; a <= path.size(); a = b + 1) {
                b = path.find('/', a);
                if (b == std::string::npos) b = path.size();
                parts.push_back(path.substr(a, b - a));
            }
            if (parts.size() < 5 || parts[0] != "audio" || parts[1] != "playervoices") continue;
            std::string key = ex.Key(g, o.file->name, entry[1]);
            if (key.empty() || !g.Resolve(*o.file, entry[1])) continue;
            std::string ev = parts[4];
            if (ev.rfind("playervo_", 0) == 0) ev = ev.substr(9);
            auto e = events.find(ev);
            if (e == events.end()) return *err = "unknown voice event " + parts[4], false;
            voiceCue[key] = "VO/" + parts[2] + "/" + parts[3] + "/" + e->second;
        }
    }
    json cues = json::object();
    for (const json& c : configs("SoundCue")) {
        std::string id = idOf(c);
        auto v = voiceCue.find(id);
        std::string key = v != voiceCue.end() ? v->second : At(c, "m_Name").get<std::string>();
        cueKey[id] = key;
        json clips = json::array();
        for (const json& r : At(c, "audioClips")) {
            json n = clipName(fileOf(c), r);
            if (n.is_string() && !n.get<std::string>().empty()) clips.push_back(n);
        }
        cues[key] = {{"pitch", At(c, "pitchBase")}, {"pitchVar", At(c, "pitchVariance")}, {"vol", At(c, "volumeBase")},
                     {"volVar", At(c, "volumeVariance")}, {"clips", std::move(clips)}};
    }
    auto cueRef = [&](const json& c, const char* field) -> std::string {
        std::string key = ex.Key(g, fileOf(c), At(c, field));
        const SerializedFile* f = g.File(fileOf(c));
        if (key.empty() || !f || !g.Resolve(*f, At(c, field))) return std::string();
        auto it = cueKey.find(key);
        return it == cueKey.end() ? std::string() : it->second;
    };
    json enemyDeath = json::object(), attack = json::object();
    for (const json& c : configs("EnemyConfig"))
        if (std::string k = cueRef(c, "soundOnDeath"); !k.empty()) enemyDeath[At(c, "m_Name").get<std::string>()] = k;
    for (const json& c : configs("AttackConfig"))
        if (std::string k = cueRef(c, "soundOnAttack"); !k.empty()) attack[At(c, "m_Name").get<std::string>()] = k;

    // Music packs, clips bound to scene components (Jukebox, TitleScreen, StageEndUI...)
    std::map<std::string, json> packs;
    for (const json& c : configs("MusicPackConfig")) {
        json tracks = json::array();
        for (const json& r : At(c, "tracks")) tracks.push_back(clipName(fileOf(c), r));
        packs[At(c, "m_Name").get<std::string>()] = std::move(tracks);
    }
    json named = {{"music.boss", "Aggressive9_Boss"},      {"music.outOfTime", "Stage9_OutOfTime"},
                  {"music.triumphant", "Omake-luke-loop"}, {"music.title", "DrumOnly2"},
                  {"music.endingVictory", "Cave3"},        {"jingle.title", "Event 16"},
                  {"jingle.playerDeath", "Event 11"},      {"jingle.enemiesAppear", "Event 24"},
                  {"jingle.enemiesAppearBoss", "Event 21"}, {"jingle.introducePlayer", "Event 20"},
                  {"jingle.endingVictory", "Fanfale1"},    {"jingle.victory", "Event 29"},
                  {"music.results", "Cave4"},              {"music.defeat", "Gameover2"},
                  {"jingle.halloweenTreat", "jingles_NES12"}, {"jingle.halloweenTrick", "jingles_NES00"},
                  {"jingle.titleHalloween", "jingles_NES13"}, {"jingle.titleEaster", "Event 13"},
                  {"sfx.playerSwitch", "Coin 10"},         {"sfx.button", "Button 1"}};

    // CrowdAudio and the AudioSource on its GameObject
    auto crowdIt = ex.scene.find("CrowdAudio");
    if (crowdIt == ex.scene.end() || crowdIt->second.empty()) return *err = "no CrowdAudio", false;
    const json& crowd = crowdIt->second.front();
    const SerializedFile* cf = g.File(fileOf(crowd));
    ObjRef go = cf ? g.Resolve(*cf, At(crowd, "m_GameObject")) : ObjRef{};
    json gov;
    if (!go || !g.Read(go, &gov)) return *err = "CrowdAudio has no GameObject", false;
    std::vector<ObjRef> sources;
    for (const json& c : At(gov, "m_Component"))
        if (ObjRef r = g.Resolve(*go.file, At(c, "component")); r && r.obj->classId == 82) sources.push_back(r);
    if (sources.size() != 1) return *err = "CrowdAudio must have exactly one AudioSource", false;
    json source, groupv;
    if (!g.Read(sources[0], &source)) return *err = "cannot read the crowd AudioSource", false;
    json crowdClip = clipName(sources[0].file->name, At(source, "m_audioClip"));
    ObjRef group = g.Resolve(*sources[0].file, At(source, "OutputAudioMixerGroup"));
    if (!crowdClip.is_string() || !group || !g.Read(group, &groupv) || At(groupv, "m_Name") != "SFX_Crowd")
        return *err = "CrowdAudio clip or output group is missing", false;
    named["loop.crowd"] = crowdClip;
    json crowdAudio = json::object();
    for (const char* k : {"dataSilent", "dataNormal", "dataCheerSmall", "dataCheerBig", "dataCheerVictory", "returnToNormalDuration"})
        crowdAudio[k] = Jsonable(At(crowd, k));
    crowdAudio["source"] = {{"volume", At(source, "m_Volume")}, {"pitch", At(source, "m_Pitch")},
                            {"loop", At(source, "Loop")}, {"group", At(groupv, "m_Name")}};
    for (auto it = named.begin(); it != named.end(); ++it) used.insert(it->get<std::string>());

    // The clips: the first AudioClip of each name, as extract_assets.py wrote it
    std::map<std::string, ObjRef> clipObj;
    for (ObjRef o : g.Objects(83)) {
        auto hit = ex.names.find(o.file->name + ":" + std::to_string(o.obj->pathId));
        if (hit != ex.names.end() && hit->second.second.is_string()) clipObj.emplace(hit->second.second.get<std::string>(), o);
    }
    // each bank is read from its .resource when needed: the files are too big to keep in memory
    struct Job {
        std::string name, source;
        uint64_t offset;
        size_t size;
    };
    std::vector<Job> jobs;
    std::vector<uint32_t> crcs;
    std::vector<std::string> missing;
    Bytes fsb;
    for (const std::string& n : used) {
        auto it = clipObj.find(n);
        json clip;
        if (it == clipObj.end() || SafeFileName(n + ".wav") != n + ".wav" || !g.Read(it->second, &clip)) {
            missing.push_back(n);
            continue;
        }
        const json& res = At(clip, "m_Resource");
        Job job{n, At(res, "m_Source").is_string() ? At(res, "m_Source").get<std::string>() : std::string(),
                At(res, "m_Offset").is_number() ? At(res, "m_Offset").get<uint64_t>() : 0,
                At(res, "m_Size").is_number() ? At(res, "m_Size").get<size_t>() : 0};
        if (!g.ReadResource(job.source, job.offset, job.size, &fsb)) {
            missing.push_back(n);
            continue;
        }
        if (uint32_t crc = FsbVorbisCrc(fsb.data(), fsb.size())) crcs.push_back(crc);
        jobs.push_back(std::move(job));
    }
    VorbisSetups setups;
    if (!setups.Load(dll, crcs)) return *err = "cannot read " + dll, false;
    if (!MakeDirs(outDir + "/clips")) return *err = "cannot create " + outDir + "/clips", false;
    json clips = json::object();
    for (size_t i = 0; i < jobs.size(); i++) {
        Pcm pcm;
        if (!g.ReadResource(jobs[i].source, jobs[i].offset, jobs[i].size, &fsb) || !DecodeFsb(fsb.data(), fsb.size(), setups, &pcm, err))
            return *err = jobs[i].name + ": " + (err->empty() ? std::string("cannot read the clip") : *err), false;
        std::string file = ClipFile(jobs[i].name);
        clips[jobs[i].name] = {{"file", file}, {"rate", pcm.rate}, {"channels", pcm.channels}, {"frames", pcm.frames()}};
        if (!WriteText(outDir + "/clips/" + file + ".dda", EncodeDda(pcm))) return *err = "cannot write " + file + ".dda", false;
        if (progress) progress(static_cast<int>(i + 1), static_cast<int>(jobs.size()));
    }
    for (const std::string& n : missing) std::fprintf(stderr, "audio: no clip %s\n", n.c_str());

    json packList = json::array();
    for (const char* k : {"MusicPackA", "MusicPackB", "MusicPackC", "MusicPackD"})
        packList.push_back(packs.count(k) ? packs[k] : json::array());
    json manifest = {{"cues", std::move(cues)},       {"enemyDeath", std::move(enemyDeath)}, {"attack", std::move(attack)},
                     {"music", {{"packs", std::move(packList)}}}, {"named", std::move(named)},
                     {"clips", std::move(clips)},     {"crowd", std::move(crowdAudio)}};
    if (!WriteText(outDir + "/audio.json", manifest.dump(1))) return *err = "cannot write audio.json", false;
    return true;
}

} // namespace dd::install
