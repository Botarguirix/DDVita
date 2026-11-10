// Draws one stage of the simulation: the arena, the field tiles and every actor, and plays the
// simulation's events back over time (moves, attacks, hits, deaths, ball flights).
//
// Layout constants come from the prefabs (Player / Enemy / Treasure / FieldTile / Hazard_*): actor
// sprites are camera-facing billboards scaled 0.75 with the pivot at the feet; floor sprites lie flat
// (rotation 90 degrees around x) one unit per tile; grid (x, y) is world (x, 0, y).
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "render/assets.h"
#include "render/gfx.h"
#include "sim/sim.h"

namespace dd::gfx {

struct Camera {
    Vec3 eye{0, 6, -4}, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    float fov = 55.0f;
    Mat4 Proj(float aspect) const { return Mat4::Perspective(fov, aspect, 0.3f, 100.0f); }
    Mat4 View() const { return Mat4::View(eye, right, up, forward); }
    void LookAt(Vec3 target);
};

class StageView {
public:
    StageView(Assets& assets, Renderer& r, sim::Game& game);
    ~StageView();

    void Update(float dt);
    // Options: CameraController's dynamic cameras; SetBloodColour* (grey blood)
    bool dynamicCamera = true, greyBlood = false;
    bool Busy() const { return wait_ > 0 || !queue_.empty() || consumed_ < game_.events.size(); }
    // cursor: tile highlighted by the player's selection (nullptr = none)
    void Draw(const sim::V2* cursor, const std::vector<sim::Command>& commands);
    // 2D pass in output pixels (after Renderer::EndScene): health numbers over the actors.
    void DrawOverlay();
    // where the health bars go (output pixels, over each living actor's head)
    struct BarInfo {
        float x, y;
        int health, maxHealth, actOrder; // actOrder < 0: not an enemy
        bool stunned;
    };
    std::vector<BarInfo> Bars() const;
    // Screen position (output pixels) of a world point; false when behind the camera.
    bool Project(Vec3 p, float* sx, float* sy) const;
    Vec3 ActorPosition(int uid) const;
    float time() const { return time_; }
    const std::vector<std::string>& log() const { return log_; }
    // Called as each event starts playing on screen (sounds follow the animations, not the sim).
    std::function<void(const sim::Event&)> onEvent;

private:
    struct Vis {
        Vec3 from, to;
        float moveT = 1, moveDur = 0.35f;
        float lungeT = 1;
        Vec3 lungeDir;
        float flash = 0;  // hit flash, seconds left
        float fade = 1;   // 1 visible, fading to 0 after death
        bool dying = false, shown = true;
        std::string pose; // enemy sprite pose override ("Attack") while lungeT < 1
        Vec3 Pos() const;
    };
    struct PlayerSprites {
        RuntimeSprite plain, fear, joy, tired;
    };

    Assets& a_;
    Renderer& r_;
    sim::Game& game_;
    Camera cam_;
    Vec3 camTarget_;
    bool camInit_ = false;
    // the dynamic cameras (attack / ball / charge / spawn cams): a closer shot of `focus_` while
    // focusHold_ lasts, blended in and out; introT_: the intro cams (the exit door, then the field)
    Vec3 focus_;
    float focusW_ = 0, focusHold_ = 0, introT_ = 0;
    void FocusOn(Vec3 p, float hold) {
        if (!dynamicCamera) return;
        focus_ = focusHold_ > 0 ? Lerp(focus_, p, 0.5f) : p;
        focusHold_ = std::max(focusHold_, hold);
    }
    float time_ = 0;
    float wait_ = 0;
    size_t consumed_ = 0;
    std::deque<sim::Event> queue_;
    std::map<int, Vis> vis_;
    std::map<int, PlayerSprites> playerSprites_;
    std::vector<std::string> log_;
    // ball
    int ballCarrier_ = -1;
    Vec3 ballFrom_, ballTo_;
    float ballT_ = 1, ballDur_ = 0.3f;
    bool ballArc_ = false;
    // floor
    struct FloorLook { int basic = 0; bool flipX = false, flipY = false; };
    std::vector<FloorLook> floor_;
    std::vector<Vertex> batch_;
    // effects: simple particles (FX_Blood, FX_MoveSmokePuffs) and sprite animations (LightningBolt)
    struct Particle {
        Vec3 p, v;
        float life = 0, maxLife = 1, size = 0.06f, gravity = 0;
        float r = 1, g = 1, b = 1, a = 1;
    };
    struct SpriteFx {
        std::string anim;
        Vec3 pos;
        float t = 0;
    };
    std::vector<Particle> particles_;
    std::vector<SpriteFx> spriteFx_;
    uint32_t fxSeed_ = 1;

    void Apply(const sim::Event& e);
    void SyncToGame();
    void UpdateCamera(float dt);
    Vis& VisFor(int uid);
    // drawing helpers
    void Billboard(const std::string& sprite, Vec3 anchor, float scale, uint32_t color, Vec3 localOffset = {});
    void BillboardTex(const Texture& tex, const SpriteDef& d, Vec3 anchor, float scale, uint32_t color, Vec3 localOffset = {});
    void Quad(const Texture& tex, const SpriteDef& d, Vec3 origin, Vec3 axisX, Vec3 axisY, uint32_t color, bool flipX, bool flipY,
              Blend blend);
    void Flat(const std::string& sprite, Vec3 center, Vec3 axisX, Vec3 axisY, uint32_t color, bool flipX = false, bool flipY = false);
    void FlatSliced(const std::string& sprite, Vec3 start, Vec3 dir, float length, uint32_t color);
    void DrawArena();
    void DrawFloor(const sim::V2* cursor, const std::vector<sim::Command>& commands);
    void DrawActors();
    void DrawIntentions(const sim::V2* cursor, const std::vector<sim::Command>& commands);
    void DrawEffects();
    void UpdateEffects(float dt);
    float FxRand(); // cosmetic random in [0, 1)
    void Burst(Vec3 at, int count, float r, float g, float b, float speed, float up, float gravity, float life, float size);
    // frame of a looping (or one-shot, when t runs past the end: nullptr) SimpleAnimationConfig
    const std::string* AnimFrameAt(const std::string& anim, float t, bool loop) const;
    std::string EnemySprite(const sim::Enemy* e, const Vis& v) const;
    const RuntimeSprite& PlayerSprite(const sim::Player* p);
};

Vec3 TileCenter(sim::V2 p);

} // namespace dd::gfx
