#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dd::audio {

namespace {

constexpr int kBlock = 4096;
constexpr int kMaxVoices = 24;

const int16_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int8_t kIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct Header {
    int rate, channels, frames, blocks;
};

bool ReadHeader(FILE* f, Header* h) {
    uint8_t b[20];
    if (std::fread(b, 1, 20, f) != 20 || std::memcmp(b, "DDAU", 4) != 0) return false;
    auto u32 = [&](int o) { return static_cast<int>(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | static_cast<uint32_t>(b[o + 3]) << 24); };
    h->rate = u32(4);
    h->channels = b[8] | b[9] << 8;
    h->frames = u32(12);
    h->blocks = u32(16);
    return h->channels >= 1 && h->channels <= 2 && h->rate > 0;
}

int BlockBytes(int frames, int channels) { return channels * (4 + (frames + 1) / 2); }

// Decodes one block (planar on disk) into interleaved samples.
void DecodeBlock(const uint8_t* src, int frames, int channels, int16_t* out) {
    for (int c = 0; c < channels; c++) {
        int pred = static_cast<int16_t>(src[0] | src[1] << 8);
        int idx = std::min<int>(src[2], 88);
        src += 4;
        for (int i = 0; i < frames; i++) {
            int code = (i & 1) ? src[i >> 1] >> 4 : src[i >> 1] & 15;
            int step = kStep[idx];
            int diff = step >> 3;
            if (code & 4) diff += step;
            if (code & 2) diff += step >> 1;
            if (code & 1) diff += step >> 2;
            pred += (code & 8) ? -diff : diff;
            pred = std::clamp(pred, -32768, 32767);
            idx = std::clamp(idx + kIndex[code & 7], 0, 88);
            out[i * channels + c] = static_cast<int16_t>(pred);
        }
        src += (frames + 1) / 2;
    }
}

} // namespace

bool LoadDda(const std::string& path, PcmClip* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    Header h;
    bool ok = ReadHeader(f, &h);
    if (ok) {
        out->rate = h.rate;
        out->channels = h.channels;
        out->frames = h.frames;
        out->pcm.assign(static_cast<size_t>(h.frames) * h.channels, 0);
        std::vector<uint8_t> raw(BlockBytes(kBlock, h.channels));
        for (int b = 0; b < h.blocks && ok; b++) {
            int n = std::min(kBlock, h.frames - b * kBlock);
            size_t bytes = BlockBytes(n, h.channels);
            ok = std::fread(raw.data(), 1, bytes, f) == bytes;
            if (ok) DecodeBlock(raw.data(), n, h.channels, &out->pcm[static_cast<size_t>(b) * kBlock * h.channels]);
        }
    }
    std::fclose(f);
    return ok;
}

DdaStream::~DdaStream() {
    if (f_) std::fclose(f_);
}

bool DdaStream::Open(const std::string& path) {
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) return false;
    Header h;
    if (!ReadHeader(f_, &h)) return false;
    rate_ = h.rate;
    channels_ = h.channels;
    frames_ = h.frames;
    blocks_ = h.blocks;
    dataStart_ = std::ftell(f_);
    buf_.resize(static_cast<size_t>(kBlock) * channels_);
    raw_.resize(BlockBytes(kBlock, channels_));
    return true;
}

bool DdaStream::LoadBlock(int b) {
    if (b < 0 || b >= blocks_) return false;
    // every block before the last one is full size, so its offset is a multiple
    if (b != block_ + 1) std::fseek(f_, dataStart_ + static_cast<long>(b) * BlockBytes(kBlock, channels_), SEEK_SET);
    int n = std::min(kBlock, frames_ - b * kBlock);
    size_t bytes = BlockBytes(n, channels_);
    if (std::fread(raw_.data(), 1, bytes, f_) != bytes) return false;
    DecodeBlock(raw_.data(), n, channels_, buf_.data());
    block_ = b;
    return true;
}

void DdaStream::Seek(int frame) { pos_ = std::clamp(frame, 0, frames_); }

bool DdaStream::Next(int16_t* l, int16_t* r) {
    if (pos_ >= frames_) return false;
    int b = pos_ / kBlock;
    if (b != block_ && !LoadBlock(b)) return false;
    int i = pos_ - b * kBlock;
    *l = buf_[static_cast<size_t>(i) * channels_];
    *r = channels_ == 2 ? buf_[static_cast<size_t>(i) * 2 + 1] : *l;
    pos_++;
    return true;
}

// ---------------------------------------------------------------------------------------------

void Mixer::Play(std::shared_ptr<const PcmClip> clip, float volume, float pitch, Group group) {
    if (!clip || clip->frames == 0) return;
    std::lock_guard<std::mutex> lock(m_);
    if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin()); // drop the oldest
    Voice v;
    v.clip = std::move(clip);
    v.step = static_cast<double>(v.clip->rate) / kRate * std::max(0.1f, pitch);
    v.vol = volume;
    v.group = group;
    voices_.push_back(std::move(v));
}

void Mixer::PlayStream(Stream s, const std::string& path, bool loop, float startSeconds, float fadeIn, float delay) {
    auto dda = std::make_unique<DdaStream>();
    if (!dda->Open(path)) {
        std::fprintf(stderr, "missing audio %s\n", path.c_str());
        return;
    }
    dda->Seek(static_cast<int>(startSeconds * dda->rate()));
    std::lock_guard<std::mutex> lock(m_);
    StreamSlot& st = streams_[s];
    st = StreamSlot{};
    st.step = static_cast<double>(dda->rate()) / kRate;
    st.dda = std::move(dda);
    st.loop = loop;
    st.delayFrames = static_cast<int>(delay * kRate);
    if (fadeIn > 0) {
        st.gain = 0;
        st.target = 1;
        st.gainStep = 1.0f / (fadeIn * kRate);
    }
}

void Mixer::FadeStream(Stream s, float target, float seconds, bool stop) {
    std::lock_guard<std::mutex> lock(m_);
    StreamSlot& st = streams_[s];
    if (!st.dda) return;
    if (seconds <= 0) {
        st.gain = target;
        if (stop && target <= 0) st.dda.reset();
        return;
    }
    st.target = target;
    st.gainStep = std::abs(target - st.gain) / (seconds * kRate);
    st.stopAtTarget = stop;
}

void Mixer::StopStream(Stream s) {
    std::lock_guard<std::mutex> lock(m_);
    streams_[s].dda.reset();
}

bool Mixer::StreamPlaying(Stream s) {
    std::lock_guard<std::mutex> lock(m_);
    return streams_[s].dda != nullptr;
}

float Mixer::StreamSeconds(Stream s) {
    std::lock_guard<std::mutex> lock(m_);
    const StreamSlot& st = streams_[s];
    return st.dda ? static_cast<float>(st.dda->position()) / st.dda->rate() : 0.0f;
}

void Mixer::SetVolume(Group group, float slider) {
    std::lock_guard<std::mutex> lock(m_);
    gain_[group] = slider * slider;
}

void Mixer::MixStream(StreamSlot& s, float* acc, int frames, float vol) {
    for (int i = 0; i < frames && s.dda; i++) {
        if (s.delayFrames > 0) {
            s.delayFrames--;
            continue;
        }
        if (!s.primed) {
            if (!s.dda->Next(&s.cur[0], &s.cur[1])) {
                s.dda.reset();
                break;
            }
            s.dda->Next(&s.next[0], &s.next[1]);
            s.primed = true;
        }
        float t = static_cast<float>(s.pos);
        float l = s.cur[0] + (s.next[0] - s.cur[0]) * t, r = s.cur[1] + (s.next[1] - s.cur[1]) * t;
        acc[i * 2] += l * s.gain * vol;
        acc[i * 2 + 1] += r * s.gain * vol;
        if (s.gain != s.target) {
            s.gain = s.gain < s.target ? std::min(s.target, s.gain + s.gainStep) : std::max(s.target, s.gain - s.gainStep);
            if (s.gain == s.target && s.stopAtTarget) {
                s.dda.reset();
                break;
            }
        }
        s.pos += s.step;
        while (s.pos >= 1) {
            s.pos -= 1;
            s.cur[0] = s.next[0];
            s.cur[1] = s.next[1];
            if (!s.dda->Next(&s.next[0], &s.next[1])) {
                if (!s.loop) {
                    s.dda.reset();
                    break;
                }
                s.dda->Seek(0);
                s.dda->Next(&s.next[0], &s.next[1]);
            }
        }
    }
}

void Mixer::Mix(int16_t* out, int frames) {
    acc_.assign(static_cast<size_t>(frames) * 2, 0.0f);
    float* acc = acc_.data();
    {
        std::lock_guard<std::mutex> lock(m_);
        for (Voice& v : voices_) {
            const PcmClip& c = *v.clip;
            const int16_t* p = c.pcm.data();
            float sfx = gain_[MasterGroup] * gain_[v.group];
            for (int i = 0; i < frames; i++) {
                int i0 = static_cast<int>(v.pos);
                if (i0 >= c.frames) break;
                float t = static_cast<float>(v.pos - i0);
                int i1 = std::min(i0 + 1, c.frames - 1);
                float l, r;
                if (c.channels == 2) {
                    l = p[i0 * 2] + (p[i1 * 2] - p[i0 * 2]) * t;
                    r = p[i0 * 2 + 1] + (p[i1 * 2 + 1] - p[i0 * 2 + 1]) * t;
                } else {
                    l = r = p[i0] + (p[i1] - p[i0]) * t;
                }
                acc[i * 2] += l * v.vol * sfx;
                acc[i * 2 + 1] += r * v.vol * sfx;
                v.pos += v.step;
            }
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= v.clip->frames; }),
                      voices_.end());
        MixStream(streams_[Music], acc, frames, gain_[MasterGroup] * gain_[MusicGroup]);
        MixStream(streams_[Jingle], acc, frames, gain_[MasterGroup] * gain_[JingleGroup]);
        MixStream(streams_[Crowd], acc, frames, gain_[MasterGroup] * gain_[SoundGroup]);
    }
    for (int i = 0; i < frames * 2; i++) out[i] = static_cast<int16_t>(std::clamp(acc[i], -32768.0f, 32767.0f));
}

} // namespace dd::audio
