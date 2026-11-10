#include "audio/sound.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dd::audio {

using json = nlohmann::json;
using namespace dd::sim;

namespace {
// Jukebox timing
constexpr float kSwapTrackFade = 0.5f, kSwapTrackGap = 0.25f;
constexpr float kUnimportantVoiceChance = 0.666f; // PlayerVoice.unimportantSoundChance
} // namespace

bool Sound::Load(const std::string& dir, Mixer* mixer, std::string* err) {
    std::ifstream f(dir + "/audio.json", std::ios::binary);
    if (!f) {
        *err = "cannot open " + dir + "/audio.json";
        return false;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        *err = e.what();
        return false;
    }
    dir_ = dir;
    for (auto& [name, c] : j["cues"].items()) {
        Cue cue;
        cue.pitch = c["pitch"];
        cue.pitchVar = c["pitchVar"];
        cue.vol = c["vol"];
        cue.volVar = c["volVar"];
        for (auto& clip : c["clips"]) cue.clips.push_back(clip);
        cues_[name] = cue;
    }
    for (auto& [name, c] : j["clips"].items()) clipFiles_[name] = c["file"];
    for (auto& [k, v] : j["named"].items()) named_[k] = v;
    if (j.contains("crowd") && j["crowd"].contains("source")) crowdVolume_ = j["crowd"]["source"].value("volume", 1.0f);
    for (auto& [k, v] : j["enemyDeath"].items()) enemyDeath_[k] = v;
    for (auto& [k, v] : j["attack"].items()) attack_[k] = v;
    for (auto& pack : j["music"]["packs"]) {
        packs_.emplace_back();
        for (auto& t : pack) packs_.back().push_back(t);
    }
    mixer_ = mixer;
    return true;
}

float Sound::Rand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ >> 8) / 16777216.0f;
}

std::string Sound::ClipPath(const std::string& name) const {
    auto it = clipFiles_.find(name);
    return dir_ + "/clips/" + (it != clipFiles_.end() ? it->second : name) + ".dda";
}

std::shared_ptr<PcmClip> Sound::Clip(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    auto c = std::make_shared<PcmClip>();
    if (!LoadDda(ClipPath(name), c.get())) {
        std::fprintf(stderr, "missing sound %s\n", name.c_str());
        c.reset();
    }
    return cache_[name] = c;
}

void Sound::PlayClip(const std::string& clip, float volume, float pitch, Mixer::Group group) {
    if (!mixer_ || clip.empty()) return;
    mixer_->Play(Clip(clip), volume, pitch, group);
}

// SoundCue.GetRandomSound / GetPitch / GetVolume
void Sound::PlayCue(const std::string& name, Mixer::Group group) {
    if (!mixer_) return;
    auto it = cues_.find(name);
    if (it == cues_.end() || it->second.clips.empty()) return;
    const Cue& c = it->second;
    const std::string& clip = c.clips.size() == 1 ? c.clips[0] : c.clips[static_cast<size_t>(Rand() * c.clips.size()) % c.clips.size()];
    float pitch = c.pitch + (c.pitchVar != 0 ? (Rand() * 2 - 1) * c.pitchVar : 0);
    float vol = c.vol - (c.volVar != 0 ? Rand() * c.volVar : 0);
    PlayClip(clip, vol, pitch, group);
}

void Sound::PlayNamed(const std::string& key) {
    auto it = named_.find(key);
    if (it != named_.end()) PlayClip(it->second);
}

// ---------------------------------------------------------------------------------------------
// Jukebox
// ---------------------------------------------------------------------------------------------
void Sound::Shuffle(int unlockedPacks) {
    std::vector<std::string> all;
    for (int i = 0; i < static_cast<int>(packs_.size()) && i < unlockedPacks; i++)
        all.insert(all.end(), packs_[i].begin(), packs_[i].end());
    playlist_.clear();
    while (!all.empty()) {
        size_t i = static_cast<size_t>(Rand() * all.size()) % all.size();
        playlist_.push_back(all[i]);
        all.erase(all.begin() + static_cast<long>(i));
    }
    stageIndex_ = 0;
}

void Sound::IncrementStageMusicIndex() {
    if (++stageIndex_ >= static_cast<int>(playlist_.size())) stageIndex_ = 0;
}

void Sound::PlayStageMusic(bool bossStage, float delay) {
    if (!mixer_) return;
    std::string track = bossStage ? named_["music.boss"] : playlist_.empty() ? "" : playlist_[stageIndex_];
    if (track.empty()) return;
    swapTimer_ = -1;
    currentTrack_ = track;
    mixer_->PlayStream(Mixer::Music, ClipPath(track), true, 0, 0, delay);
}

void Sound::PlayMusicTrack(const std::string& clip, float fadeIn) {
    bool triumphant = clip == named_["music.triumphant"];
    currentTrack_ = clip;
    mixer_->PlayStream(Mixer::Music, ClipPath(clip), true, triumphant ? triumphantPoint_ : 0, fadeIn);
}

void Sound::FadeOutMusic(float seconds) {
    if (!mixer_) return;
    if (currentTrack_ == named_["music.triumphant"]) triumphantPoint_ = mixer_->StreamSeconds(Mixer::Music); // CacheTriumphantMusicPoint
    mixer_->FadeStream(Mixer::Music, 0, seconds, true);
}

void Sound::SwapTrack(const std::string& clip, bool fadeIn) {
    FadeOutMusic(kSwapTrackFade);
    swapTo_ = clip;
    swapFadeIn_ = fadeIn;
    swapTimer_ = kSwapTrackFade + kSwapTrackGap;
}

void Sound::PlayOutOfTimeMusic() {
    if (mixer_) SwapTrack(named_["music.outOfTime"], false);
}

void Sound::PlayTriumphantMusic() {
    if (mixer_) SwapTrack(named_["music.triumphant"], true);
}

void Sound::PlayJingle(const std::string& namedKey) {
    auto it = named_.find(namedKey);
    if (mixer_ && it != named_.end()) mixer_->PlayStream(Mixer::Jingle, ClipPath(it->second), false, 0, 0);
}

void Sound::PlayNamedMusic(const std::string& namedKey, float delay, float fadeIn) {
    auto it = named_.find(namedKey);
    if (!mixer_ || it == named_.end()) return;
    swapTimer_ = -1;
    currentTrack_ = it->second;
    mixer_->PlayStream(Mixer::Music, ClipPath(it->second), true, 0, fadeIn, delay);
}

bool Sound::triumphantMusicIsPlaying() const {
    auto it = named_.find("music.triumphant");
    return mixer_ && it != named_.end() && currentTrack_ == it->second && swapTimer_ < 0 && mixer_->StreamPlaying(Mixer::Music);
}

void Sound::StartCrowd() {
    auto it = named_.find("loop.crowd");
    if (!mixer_ || it == named_.end() || crowdOn_) return;
    crowdOn_ = true;
    mixer_->PlayStream(Mixer::Crowd, ClipPath(it->second), true, 0, 1.0f);
    mixer_->FadeStream(Mixer::Crowd, crowdVolume_, 1.0f, false);
}

void Sound::StopCrowd(float fadeSeconds) {
    if (!mixer_ || !crowdOn_) return;
    crowdOn_ = false;
    mixer_->FadeStream(Mixer::Crowd, 0, fadeSeconds, true);
}

void Sound::Update(float dt) {
    if (swapTimer_ < 0) return;
    swapTimer_ -= dt;
    if (swapTimer_ < 0) PlayMusicTrack(swapTo_, swapFadeIn_ ? kSwapTrackFade * 2 : 0);
}

// ---------------------------------------------------------------------------------------------
// Gameplay
// ---------------------------------------------------------------------------------------------
// TriumphTracker.IsTriumphant
bool Sound::IsTriumphant(Game& g) const {
    int live = g.GetNumberOfLivePlayers();
    if (live < 2) return false;
    const ScoringConfig& sc = g.data.scoring;
    FieldSize size = g.run.CurrentStage().fieldSize;
    int speedTarget = size == FieldSize::Small ? sc.parBonusRankTargetSmall
                      : size == FieldSize::Medium ? sc.parBonusRankTargetMedium
                                                  : sc.parBonusRankTargetLarge;
    if (g.Turn() >= g.TurnPar() + speedTarget) return false;
    if (g.ball->carrier && g.ball->carrier->isEnemy()) return false;
    int score = 0, nearGoal = g.fieldX - 1 - 2, half = g.fieldX / 2;
    for (Player* p : g.players) {
        if (p->gridPos.x >= nearGoal) {
            score += 2;
            if (p->isHoldingBall()) score++;
        } else if (p->gridPos.x >= half) {
            score++;
        }
        if (p->isHoldingBall()) score += 3;
    }
    return score >= 5;
}

void Sound::StageStarted(Game& g) {
    switchedToTriumphant_ = false;
    lowStamina_.clear();
    for (Player* p : g.players) lowStamina_[p->uid] = p->staminaIsLow();
    // StageStartSequences: players introduced, enemies appear, then the stage track
    bool boss = g.run.mode == GameMode::Boss || g.run.currentStageIsFinalStageInFinalTrial();
    PlayJingle(boss ? "jingle.enemiesAppearBoss" : "jingle.enemiesAppear");
    PlayStageMusic(g.run.currentStageIsFinalStageInFinalTrial(), 1.5f);
}

// PlayerVoice.PlayPlayerSound: Resources Audio/PlayerVoices/<Male|Female>/<voice>/PlayerVO_<event>
void Sound::Voice(const Player* p, const char* event, bool important) {
    if (!important && Rand() >= kUnimportantVoiceChance) return;
    const HumanPlayerSeed& s = p->humanPlayer->seed;
    PlayCue(std::string("VO/") + (s.isMale ? "male/" : "female/") + std::to_string(s.voice) + "/" + event, Mixer::VoiceGroup);
}

void Sound::CommandPressed(bool tooExpensive) { PlayCue(tooExpensive ? "CommandTooExpensive" : "CommandPress"); }

void Sound::PlayerSwitched() { PlayNamed("sfx.playerSwitch"); }

void Sound::OnEvent(const Event& e, Game& g) {
    if (!mixer_) return;
    Entity* ent = g.FindEntity(e.actor);
    switch (e.type) {
    case Ev::TurnStarted:
        if (e.value == g.TurnPar() + 1) PlayOutOfTimeMusic();
        break;
    case Ev::PhaseChanged:
        // GameController.StartPlayerInput
        if (e.value == static_cast<int>(Phase::PlayerInput) && !switchedToTriumphant_ && IsTriumphant(g)) {
            PlayTriumphantMusic();
            switchedToTriumphant_ = true;
        }
        break;
    case Ev::Move: // Actor.MoveToPosition: soundOnStep; Player.StepStarted: voice
        if (!ent) break;
        if (ent->isPlayer()) {
            PlayCue("Player_Step");
            auto* p = static_cast<Player*>(ent);
            if (p == g.currentPlayer && g.phase() != Phase::EnemiesActing) Voice(p, p->isHoldingBall() ? "MoveWithBall" : "Move", false);
        } else if (ent->isEnemy()) {
            PlayCue("Enemy_Step");
        } else if (ent->isObstacle()) {
            PlayCue("Wall_Move");
        }
        break;
    case Ev::Attack: // Attack.Attack_Coroutine: config.soundOnAttack; Player.StartAttack: voice
        if (ent && ent->isEnemy()) {
            auto* en = static_cast<Enemy*>(ent);
            if (!en->cAttack) break;
            auto it = attack_.find(en->cAttack->config->name);
            if (it != attack_.end()) PlayCue(it->second);
            if (en->cAttack->wind()) PlayCue("WindAttack");
            if (en->cAttack->attackBehaviour() == AttackBehaviour::LightningBolt) PlayCue("LightningBolt");
        } else if (ent && ent->isPlayer()) {
            Voice(static_cast<Player*>(ent), "Attack", true);
        }
        break;
    case Ev::Hit: { // Actor.GA_Actor_Hit: soundOnHit unless the attack is wind
        Entity* attacker = g.FindEntity(e.extra);
        bool wind = false;
        if (attacker && attacker->isCharacter()) {
            auto* c = static_cast<Character*>(attacker);
            wind = c->cAttack && c->cAttack->wind();
        }
        if (attacker && attacker->isHazard()) {
            PlayCue(attacker->isSpikeTrap() ? "FloorSpikes_Hit" : "Enemy_Hit"); // FloorSpikes / LightningHazard.soundOnHit
        }
        if (wind || !ent) break;
        PlayCue(ent->isPlayer() ? "Player_Hit" : ent->isObstacle() ? "Wall_Hit" : "Enemy_Hit");
        break;
    }
    case Ev::Died:
        if (!ent) break;
        if (ent->isPlayer()) {
            Voice(static_cast<Player*>(ent), "Death", true);
            PlayJingle("jingle.playerDeath");
        } else if (ent->isObstacle()) {
            PlayCue("Wall_Death");
        } else if (ent->isEnemy()) {
            auto it = enemyDeath_.find(static_cast<Enemy*>(ent)->config->name);
            PlayCue(it != enemyDeath_.end() ? it->second : "Enemy_Death");
        }
        break;
    case Ev::Stunned:
        PlayCue("Enemy_StunStart");
        break;
    case Ev::StunEnded:
        PlayCue("Enemy_StunEnd");
        break;
    case Ev::Stamina: // Player.CheckForLowStamina / GA_Char_Wait
        if (ent && ent->isPlayer()) {
            auto* p = static_cast<Player*>(ent);
            bool low = p->staminaIsLow();
            if (e.extra > 0) Voice(p, "Rest", low); // replenished by a rest
            if (low && !lowStamina_[p->uid]) Voice(p, "LowStamina", true);
            lowStamina_[p->uid] = low;
        }
        break;
    case Ev::BallShot:
        if (ent && ent->isPlayer()) Voice(static_cast<Player*>(ent), "Attack", true);
        PlayCue("Player_Throw");
        break;
    case Ev::BallPass:
        PlayCue("Player_Pass");
        break;
    case Ev::BallAcquired:
        PlayCue("AcquireBall");
        break;
    case Ev::TreasureTaken:
        PlayCue("TreasureAcquire");
        break;
    case Ev::TrickOrTreat:
        PlayJingle(e.extra ? "jingle.halloweenTrick" : "jingle.halloweenTreat");
        break;
    case Ev::HazardTriggered:
        if (e.value == static_cast<int>(HazardKind::FloorSpikes)) PlayCue("FloorSpikes_NoHit"); // FloorSpikes.soundOnStab
        else PlayCue("LightningBolt_Hazard");
        break;
    case Ev::StageWon:
    case Ev::StageLost:
        FadeOutMusic(0.25f);
        break;
    default:
        break;
    }
}

} // namespace dd::audio
