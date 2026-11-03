#include "render/stage_view.h"

#include <algorithm>
#include <cmath>

namespace dd::gfx {

using namespace dd::sim;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kActorScale = 0.75f; // SpriteAnchor/Sprite localScale in every actor prefab
// TimingConfig
constexpr float kCharacterMove = 0.35f;
constexpr float kAttack = 0.5f;
constexpr float kTick = 0.35f;

float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
uint32_t Col(const float c[4]) { return PackColor(c[0], c[1], c[2], c[3]); }
// Deterministic cosmetic hash (the original uses UnityEngine.Random for these).
uint32_t Hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}
} // namespace

Vec3 TileCenter(V2 p) { return {static_cast<float>(p.x), 0, static_cast<float>(p.y)}; }

void Camera::LookAt(Vec3 target) {
    forward = Normalize(target - eye);
    right = Normalize(Cross(Vec3{0, 1, 0}, forward));
    up = Cross(forward, right);
}

Vec3 StageView::Vis::Pos() const {
    Vec3 p = Lerp(from, to, Ease(moveT));
    if (lungeT < 1) p += lungeDir * (std::sin(lungeT * kPi) * 0.35f);
    return p;
}

StageView::StageView(Assets& assets, Renderer& r, Game& game) : a_(assets), r_(r), game_(game) {
    floor_.resize(static_cast<size_t>(game.fieldX * game.fieldY));
    for (int i = 0; i < static_cast<int>(floor_.size()); i++) {
        uint32_t h = Hash(static_cast<uint32_t>(game.run.session.seed), static_cast<uint32_t>(i + 1000 * game.run.currentStageNumberAsOverallStage()));
        floor_[i] = {static_cast<int>(h % 4), (h >> 8 & 1) != 0, (h >> 9 & 1) != 0};
    }
    for (const auto& e : game.entities) {
        if (e->isPlayer() || e->isObstacle()) {
            Vis& v = VisFor(e->uid);
            v.from = v.to = TileCenter(static_cast<Actor*>(e.get())->gridPos);
        }
    }
    if (game.ball->carrier) ballCarrier_ = game.ball->carrier->uid;
    ballFrom_ = ballTo_ = TileCenter(game.ball->gridPos);
}

StageView::~StageView() {
    for (auto& [uid, ps] : playerSprites_)
        for (RuntimeSprite* s : {&ps.plain, &ps.fear, &ps.joy, &ps.tired}) r_.Free(s->tex);
}

StageView::Vis& StageView::VisFor(int uid) { return vis_[uid]; }

Vec3 StageView::ActorPosition(int uid) const {
    auto it = vis_.find(uid);
    return it == vis_.end() ? Vec3{} : it->second.Pos();
}

// ---------------------------------------------------------------------------------------------
// Event playback
// ---------------------------------------------------------------------------------------------
void StageView::Apply(const Event& e) {
    if (onEvent) onEvent(e);
    switch (e.type) {
    case Ev::Move: {
        Vis& v = VisFor(e.actor);
        v.from = v.Pos();
        v.to = TileCenter(e.to);
        v.moveT = 0;
        int dist = std::abs(e.to.x - e.from.x) + std::abs(e.to.y - e.from.y);
        v.moveDur = kCharacterMove * (dist > 1 ? 0.6f + 0.2f * dist : 1.0f);
        wait_ = v.moveDur * 0.8f;
        break;
    }
    case Ev::Attack: {
        Vis& v = VisFor(e.actor);
        Vec3 d{static_cast<float>(e.to.x), 0, static_cast<float>(e.to.y)};
        if (std::abs(e.to.x) + std::abs(e.to.y) > 1) d = Normalize(TileCenter(e.to) - v.Pos()); // target tile
        v.lungeDir = d;
        v.lungeT = 0;
        v.pose = "Attack";
        wait_ = kAttack * 0.6f;
        break;
    }
    case Ev::Hit:
        VisFor(e.actor).flash = 0.3f;
        wait_ = 0.15f;
        break;
    case Ev::Died:
        VisFor(e.actor).dying = true;
        wait_ = 0.3f;
        break;
    case Ev::Spawned: {
        Vis& v = VisFor(e.actor);
        v.from = v.to = TileCenter(e.to);
        v.moveT = 1;
        v.shown = true;
        v.dying = false;
        v.fade = 1;
        wait_ = 0.12f;
        break;
    }
    case Ev::BallShot:
    case Ev::BallPass:
    case Ev::BallRebound: {
        ballCarrier_ = -1;
        ballFrom_ = TileCenter(e.from);
        ballTo_ = TileCenter(e.to);
        ballT_ = 0;
        ballArc_ = e.type != Ev::BallShot;
        float dist = Length(ballTo_ - ballFrom_);
        ballDur_ = 0.12f + 0.06f * dist;
        wait_ = ballDur_;
        break;
    }
    case Ev::BallDropped:
        ballCarrier_ = -1;
        if (ballT_ >= 1) ballFrom_ = TileCenter(e.to);
        ballTo_ = TileCenter(e.to);
        break;
    case Ev::BallAcquired:
        ballCarrier_ = e.actor;
        ballT_ = 1;
        wait_ = 0.1f;
        break;
    case Ev::HazardTriggered:
        wait_ = 0.3f;
        break;
    case Ev::LightningRow:
        wait_ = 0.4f;
        break;
    case Ev::Stunned:
    case Ev::Healed:
    case Ev::TreasureTaken:
    case Ev::SpawnBlocked:
        wait_ = 0.1f;
        break;
    case Ev::TurnStarted:
        wait_ = kTick * 0.5f;
        break;
    case Ev::StageWon:
    case Ev::StageLost:
        wait_ = 1.0f;
        break;
    case Ev::Undo:
        // UndoManager restores every actor in place: snap the views to the restored state.
        for (auto& [uid, v] : vis_) {
            Entity* ent = game_.FindEntity(uid);
            if (!ent || !ent->isActor()) continue;
            Actor* a = static_cast<Actor*>(ent);
            v = Vis{};
            v.from = v.to = TileCenter(a->gridPos);
            v.shown = a->isAlive && a->active;
        }
        ballT_ = 1;
        ballCarrier_ = game_.ball->carrier ? game_.ball->carrier->uid : -1;
        ballFrom_ = ballTo_ = TileCenter(game_.ball->gridPos);
        break;
    default:
        break;
    }
    // effects
    switch (e.type) {
    case Ev::Move:
        if (Entity* ent = game_.FindEntity(e.actor); ent && ent->isCharacter())
            Burst(TileCenter(e.from) + Vec3{0, 0.1f, 0}, 4, 0.75f, 0.72f, 0.68f, 0.5f, 0.4f, 0, 0.45f, 0.07f); // FX_MoveSmokePuffs
        break;
    case Ev::Spawned:
        Burst(TileCenter(e.to) + Vec3{0, 0.2f, 0}, 8, 0.75f, 0.72f, 0.68f, 0.9f, 0.6f, 0, 0.5f, 0.08f);
        break;
    case Ev::Hit:
    case Ev::Died: {
        Entity* ent = game_.FindEntity(e.actor);
        Vec3 at = VisFor(e.actor).Pos() + Vec3{0, 0.4f, 0};
        int n = e.type == Ev::Died ? 18 : 10;
        if (ent && ent->isObstacle()) Burst(at, n, 0.55f, 0.5f, 0.45f, 1.2f, 1.2f, 5, 0.6f, 0.06f); // rubble
        else Burst(at, n, 0.6f, 0.08f, 0.1f, 1.4f, 1.6f, 7, 0.7f, 0.05f);                            // FX_Blood
        break;
    }
    case Ev::Attack:
        if (Entity* ent = game_.FindEntity(e.actor); ent && ent->isEnemy()) {
            auto* en = static_cast<Enemy*>(ent);
            if (en->cAttack && en->cAttack->attackBehaviour() == AttackBehaviour::LightningBolt)
                spriteFx_.push_back({"LightningBolt", TileCenter(e.to), 0});
        }
        break;
    case Ev::HazardTriggered:
        if (e.value == static_cast<int>(HazardKind::Lightning)) spriteFx_.push_back({"LightningBolt", TileCenter(e.to), 0});
        break;
    default:
        break;
    }
    switch (e.type) {
    case Ev::PhaseChanged:
    case Ev::Intention:
    case Ev::CurrentPlayer:
    case Ev::Stamina:
        break;
    default:
        log_.push_back(DescribeEvent(game_, e));
        if (log_.size() > 6) log_.erase(log_.begin());
    }
}

void StageView::SyncToGame() {
    for (auto& [uid, v] : vis_) {
        Entity* e = game_.FindEntity(uid);
        if (!e || !e->isActor()) continue;
        Actor* a = static_cast<Actor*>(e);
        if (!a->isAlive) {
            if (!v.dying && v.shown) v.dying = true;
            continue;
        }
        if (v.moveT >= 1 && v.lungeT >= 1) v.from = v.to = TileCenter(a->gridPos);
    }
    for (Enemy* e : game_.enemies) {
        if (vis_.count(e->uid)) continue;
        Vis& v = VisFor(e->uid);
        v.from = v.to = TileCenter(e->gridPos);
    }
    if (ballT_ >= 1) {
        ballCarrier_ = game_.ball->carrier ? game_.ball->carrier->uid : -1;
        ballFrom_ = ballTo_ = TileCenter(game_.ball->gridPos);
    }
}

void StageView::Update(float dt) {
    time_ += dt;
    for (auto& [uid, v] : vis_) {
        if (v.moveT < 1) v.moveT += dt / v.moveDur;
        if (v.lungeT < 1) {
            v.lungeT += dt / 0.35f;
            if (v.lungeT >= 1) v.pose.clear();
        }
        if (v.flash > 0) v.flash -= dt;
        if (v.dying && v.fade > 0) {
            v.fade -= dt / 0.4f;
            if (v.fade <= 0) v.shown = false;
        }
    }
    if (ballT_ < 1) ballT_ += dt / ballDur_;
    UpdateEffects(dt);
    wait_ -= dt;
    while (wait_ <= 0) {
        for (; consumed_ < game_.events.size(); consumed_++) queue_.push_back(game_.events[consumed_]);
        if (queue_.empty()) {
            wait_ = 0;
            SyncToGame();
            break;
        }
        Event e = queue_.front();
        queue_.pop_front();
        Apply(e);
    }
    UpdateCamera(dt);
}

// CinemachineTrackedDolly on FollowPlayerDollyPath (y 6, z -4, x from -1 to fieldX - 1) with a group
// composer framing the four corner tiles and the players.
void StageView::UpdateCamera(float dt) {
    Vec3 sum;
    float weight = 0;
    V2 corners[4] = {{0, 0}, {game_.fieldX - 1, 0}, {0, game_.fieldY - 1}, {game_.fieldX - 1, game_.fieldY - 1}};
    for (V2 c : corners) {
        sum += TileCenter(c);
        weight += 1;
    }
    for (Player* p : game_.players) {
        if (!p->isAlive) continue;
        float w = p == game_.currentPlayer ? 5.0f : 2.5f;
        sum += ActorPosition(p->uid) * w;
        weight += w;
    }
    Vec3 target = sum * (1.0f / weight);
    float k = camInit_ ? std::min(1.0f, dt * 3.0f) : 1.0f;
    camInit_ = true;
    camTarget_ = Lerp(camTarget_, target, k);
    // The dolly follows the group's (weighted) position; the composer aims at the centre of the
    // group's bounding box, i.e. the middle of the field (the players are always inside it).
    cam_.eye = {std::clamp(camTarget_.x, -1.0f, static_cast<float>(game_.fieldX - 1)), 6.0f, -4.0f};
    Vec3 look{(game_.fieldX - 1) * 0.5f, 0, (game_.fieldY - 1) * 0.5f};
    cam_.LookAt(look);
    // CinemachineGroupComposer, AdjustmentMode DollyOnly: keep the lens (FOV 55) and move the camera
    // along its forward axis until the field's bounding box fills 80% of the frame.
    float aspect = static_cast<float>(r_.lowW()) / r_.lowH();
    float tanV = std::tan(cam_.fov * kPi / 360.0f) * 0.8f, tanH = tanV * aspect;
    float dist = 0;
    for (V2 c : corners)
        for (float dx : {-0.5f, 0.5f})
            for (float dz : {-0.5f, 0.5f}) {
                Vec3 d = TileCenter(c) + Vec3{dx, 0, dz} - look;
                float z = Dot(d, cam_.forward);
                dist = std::max(dist, std::abs(Dot(d, cam_.right)) / tanH - z);
                dist = std::max(dist, std::abs(Dot(d, cam_.up)) / tanV - z);
            }
    cam_.eye = look - cam_.forward * dist;
}

bool StageView::Project(Vec3 p, float* sx, float* sy) const {
    Mat4 pv = cam_.Proj(static_cast<float>(r_.lowW()) / r_.lowH());
    Mat4 vv = cam_.View();
    float e[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0};
    float in[4] = {p.x, p.y, p.z, 1};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) e[i] += vv.m[j * 4 + i] * in[j];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) c[i] += pv.m[j * 4 + i] * e[j];
    if (c[3] <= 0.01f) return false;
    float nx = c[0] / c[3], ny = c[1] / c[3];
    float scale = std::min(static_cast<float>(r_.outW()) / r_.lowW(), static_cast<float>(r_.outH()) / r_.lowH());
    float w = r_.lowW() * scale, h = r_.lowH() * scale;
    *sx = (r_.outW() - w) * 0.5f + (nx * 0.5f + 0.5f) * w;
    *sy = (r_.outH() - h) * 0.5f + (1 - (ny * 0.5f + 0.5f)) * h;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Sprite drawing
// ---------------------------------------------------------------------------------------------
void StageView::Quad(const Texture& tex, const SpriteDef& d, Vec3 o, Vec3 ax, Vec3 ay, uint32_t color, bool flipX, bool flipY,
                     Blend blend) {
    if (!tex.id || tex.w == 0) return;
    float x0 = -d.px / d.ppu, x1 = (d.w - d.px) / d.ppu;
    float y0 = -d.py / d.ppu, y1 = (d.h - d.py) / d.ppu;
    if (flipX) {
        x0 = -x0;
        x1 = -x1;
    }
    if (flipY) {
        y0 = -y0;
        y1 = -y1;
    }
    float u0 = d.x / tex.w, u1 = (d.x + d.w) / tex.w, v0 = d.y / tex.h, v1 = (d.y + d.h) / tex.h;
    Vec3 p00 = o + ax * x0 + ay * y0, p10 = o + ax * x1 + ay * y0, p11 = o + ax * x1 + ay * y1, p01 = o + ax * x0 + ay * y1;
    Vertex v[6] = {
        {p00.x, p00.y, p00.z, u0, v0, color}, {p10.x, p10.y, p10.z, u1, v0, color}, {p11.x, p11.y, p11.z, u1, v1, color},
        {p00.x, p00.y, p00.z, u0, v0, color}, {p11.x, p11.y, p11.z, u1, v1, color}, {p01.x, p01.y, p01.z, u0, v1, color},
    };
    r_.Draw(tex, v, 6, blend, blend != Blend::Alpha);
}

void StageView::BillboardTex(const Texture& tex, const SpriteDef& d, Vec3 anchor, float scale, uint32_t color, Vec3 off) {
    // Billboard: rotation = camera rotation turned 180 degrees around y, cancelled by the sprites'
    // flipX, so pixels run along the camera's right; child offsets use the turned frame.
    Vec3 o = anchor + cam_.right * (-off.x) + cam_.up * off.y + cam_.forward * (-off.z);
    bool alpha = (color >> 24) < 250;
    Quad(tex, d, o, cam_.right * scale, cam_.up * scale, color, false, false, alpha ? Blend::Alpha : Blend::Cutout);
}

void StageView::Billboard(const std::string& sprite, Vec3 anchor, float scale, uint32_t color, Vec3 off) {
    const SpriteDef* d = a_.Sprite(sprite);
    if (!d) return;
    BillboardTex(a_.Tex(d->texture), *d, anchor, scale, color, off);
}

void StageView::Flat(const std::string& sprite, Vec3 center, Vec3 ax, Vec3 ay, uint32_t color, bool flipX, bool flipY) {
    const SpriteDef* d = a_.Sprite(sprite);
    if (!d) return;
    Quad(a_.Tex(d->texture), *d, center, ax, ay, color, flipX, flipY, Blend::Alpha);
}

// SpriteRenderer in Sliced mode with size (1, length): fixed bottom/top borders, stretched middle.
void StageView::FlatSliced(const std::string& sprite, Vec3 start, Vec3 dir, float length, uint32_t color) {
    const SpriteDef* d = a_.Sprite(sprite);
    if (!d) return;
    const Texture& tex = a_.Tex(d->texture);
    if (!tex.id) return;
    Vec3 ax{dir.z, 0, -dir.x};
    float b = d->border[1] / d->ppu, t = d->border[3] / d->ppu;
    float k = length < b + t ? length / (b + t) : 1.0f;
    float ys[4] = {0, b * k, length - t * k, length};
    float vs[4] = {d->y, d->y + d->border[1], d->y + d->h - d->border[3], d->y + d->h};
    float x0 = -d->px / d->ppu, x1 = (d->w - d->px) / d->ppu;
    float u0 = d->x / tex.w, u1 = (d->x + d->w) / tex.w;
    std::vector<Vertex> v;
    for (int i = 0; i < 3; i++) {
        Vec3 p00 = start + ax * x0 + dir * ys[i], p10 = start + ax * x1 + dir * ys[i];
        Vec3 p11 = start + ax * x1 + dir * ys[i + 1], p01 = start + ax * x0 + dir * ys[i + 1];
        float va = vs[i] / tex.h, vb = vs[i + 1] / tex.h;
        v.push_back({p00.x, p00.y, p00.z, u0, va, color});
        v.push_back({p10.x, p10.y, p10.z, u1, va, color});
        v.push_back({p11.x, p11.y, p11.z, u1, vb, color});
        v.push_back({p00.x, p00.y, p00.z, u0, va, color});
        v.push_back({p11.x, p11.y, p11.z, u1, vb, color});
        v.push_back({p01.x, p01.y, p01.z, u0, vb, color});
    }
    r_.Draw(tex, v.data(), static_cast<int>(v.size()), Blend::Alpha, false);
}

// ---------------------------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------------------------
void StageView::DrawArena() {
    const Arena& arena = a_.ArenaFor(game_.run.CurrentStage().fieldSize);
    int trial = game_.run.currentTrial;
    for (const ArenaDraw& d : arena.draws) {
        if (d.texture.empty()) {
            // Legacy Diffuse without a texture (lit, tinted): flat grey stand-in.
            std::vector<Vertex> v = d.verts;
            for (Vertex& x : v) x.rgba = PackColor(d.color[0] * 0.55f, d.color[1] * 0.55f, d.color[2] * 0.55f, 1);
            r_.DrawIndexed(Texture{}, v.data(), d.idx.data(), static_cast<int>(d.idx.size()), Blend::Opaque);
            continue;
        }
        const Texture& tex = a_.Tex(a_.SwapForTrial(trial, d.texture), true);
        r_.DrawIndexed(tex, d.verts.data(), d.idx.data(), static_cast<int>(d.idx.size()), Blend::Cutout);
    }
    for (const ArenaProp& p : arena.props) {
        const SpriteDef* d = a_.Sprite(p.sprite);
        if (!d) continue;
        Vec3 t{p.m[3], p.m[7], p.m[11]};
        Vec3 ax{p.m[0], p.m[4], p.m[8]}, ay{p.m[1], p.m[5], p.m[9]};
        if (p.billboard) {
            BillboardTex(a_.Tex(d->texture), *d, t, Length(ax), Col(p.color));
        } else {
            Quad(a_.Tex(d->texture), *d, t, ax, ay, Col(p.color), p.flipX, p.flipY, Blend::Cutout);
        }
    }
}

void StageView::DrawFloor(const V2* cursor, const std::vector<Command>& commands) {
    const Vec3 X{1, 0, 0}, Z{0, 0, 1};
    bool halloween = game_.run.IsHalloween();
    for (int x = 0; x < game_.fieldX; x++)
        for (int y = 0; y < game_.fieldY; y++) {
            FieldTile* t = game_.GetTile({x, y});
            const FloorLook& look = floor_[x * game_.fieldY + y];
            // FieldTileTexture.GetFloorSprite
            bool goal = game_.PositionIsPastFinishLine(x);
            bool gem = false;
            for (Pickup* p : t->pickups)
                if (p->isGem() && !static_cast<Treasure*>(p)->hiddenForEaster) gem = true;
            std::string s;
            bool random = false;
            if (t->containsHazard()) s = "Field_Hazard";
            else if (goal) s = gem ? (halloween ? "Field_Pumpkin_Goal" : "Field_Gem_Goal") : "Field_Goal";
            else if (gem) s = halloween ? "Field_Pumpkin" : "Field_Gem";
            else {
                s = "Field_Basic_" + std::to_string(look.basic);
                random = true;
            }
            Vec3 c = TileCenter({x, y});
            c.y = 0.002f;
            const SpriteDef* d = a_.Sprite(s);
            if (d) Quad(a_.Tex(d->texture), *d, c, X, Z, kWhite, random && look.flipX, random && look.flipY, Blend::Cutout);
            // blood splats (BloodSplat prefab, random offset up to 0.2)
            for (int i = 0; i < t->bloodSplats; i++) {
                uint32_t h = Hash(static_cast<uint32_t>(x * 31 + y), static_cast<uint32_t>(i));
                Vec3 o{((h & 255) / 255.0f - 0.5f) * 0.4f, 0.004f + i * 0.0005f, ((h >> 8 & 255) / 255.0f - 0.5f) * 0.4f};
                Flat("BloodSplats_0", c + o, X, Z, PackColor(0.45f, 0.16f, 0.19f, 0.8f));
            }
        }
    bool idle = !Busy();
    // Tiles the enemies intend to hit (FieldTile.desiredTile, FloorTileWarning_Desired animation)
    if (idle) {
        const auto* anim = a_.Animation("FloorTileWarning_Desired");
        std::string hatch = "FloorTileFX_DesiredHatch_1";
        if (anim && !anim->empty()) {
            float total = 0;
            for (const auto& f : *anim) total += f.duration;
            float t = std::fmod(time_, total);
            for (const auto& f : *anim) {
                if (t < f.duration) {
                    hatch = f.sprite;
                    break;
                }
                t -= f.duration;
            }
        }
        for (int x = 0; x < game_.fieldX; x++)
            for (int y = 0; y < game_.fieldY; y++)
                if (game_.GetTile({x, y})->isDesiredByEnemy()) Flat(hatch, TileCenter({x, y}) + Vec3{0, 0.006f, 0}, X, Z, kWhite);
    }
    // Player options, current player, cursor
    if (idle && game_.phase() == Phase::PlayerInput && game_.currentPlayer) {
        // InteractiveMarker (its legacy Animation clip pulses the solid white square; approximated)
        float optionAlpha = 0.28f + 0.1f * std::sin(time_ * 4);
        for (const Command& c : commands)
            if (c.onTile && c.interactable && !c.tooExpensive) {
                bool attack = c.actionConfig->action == ActorAction::Char_Attack;
                uint32_t col = attack ? PackColor(1, 0.35f, 0.3f, optionAlpha + 0.1f) : PackColor(1, 1, 1, optionAlpha);
                Flat("FloorTileFX_PlayerOption", TileCenter(c.tile) + Vec3{0, 0.008f, 0}, X, Z, col);
            }
        Flat("FloorTileFX_CurrentPlayer", ActorPosition(game_.currentPlayer->uid) + Vec3{0, 0.01f, 0}, X, Z, kWhite);
        if (cursor) {
            // CursorHighlight: three flat squares stacked at heights 0, 0.5 and 1 (animated: rising, fading)
            for (int i = 0; i < 3; i++) {
                float h = std::fmod(time_ * 0.6f + i / 3.0f, 1.0f);
                Flat("FloorTileFX_CursorHighlight", TileCenter(*cursor) + Vec3{0, 0.012f + h, 0}, X, Z, PackColor(1, 1, 1, 1 - h));
            }
        }
    }
}

std::string StageView::EnemySprite(const Enemy* e, const Vis& v) const {
    std::string s = e->config->sprite;
    auto swapPose = [&](const char* pose) {
        size_t p = s.rfind("_Idle");
        if (p == std::string::npos) return;
        std::string alt = s.substr(0, p) + "_" + pose;
        if (a_.Sprite(alt)) s = alt;
    };
    if (!v.pose.empty()) swapPose(v.pose.c_str());
    else if (e->hasIntention && e->nextAction.isAttack() && !e->stunned()) swapPose("Warmup");
    return s;
}

const RuntimeSprite& StageView::PlayerSprite(const Player* p) {
    auto it = playerSprites_.find(p->uid);
    if (it == playerSprites_.end()) {
        const HumanPlayerSeed& s = p->humanPlayer->seed;
        PlayerClass cls = s.playerClassConfig->playerClass;
        PlayerSprites ps;
        ps.plain = a_.MakePlayerSprite(s.isMale, cls, s.bodyTexture, s.hairTexture, "Plain");
        ps.fear = a_.MakePlayerSprite(s.isMale, cls, s.bodyTexture, s.hairTexture, "Fear");
        ps.joy = a_.MakePlayerSprite(s.isMale, cls, s.bodyTexture, s.hairTexture, "Joy");
        ps.tired = a_.MakePlayerSprite(s.isMale, cls, s.bodyTexture, s.hairTexture, "Tired");
        it = playerSprites_.emplace(p->uid, ps).first;
    }
    // PlayerSpriteAnimation: Plain / Fear (in danger), Tired when stamina is low, Joy after a win.
    const PlayerSprites& ps = it->second;
    if (game_.phase() == Phase::StageEnded && !game_.gameEndedInDefeat) return ps.joy;
    if (p->staminaIsLow()) return ps.tired;
    if (game_.GetTile(p->gridPos)->ContainsPlayerInDanger()) return ps.fear;
    return ps.plain;
}

void StageView::DrawActors() {
    std::string ballSprite = "Ball_Normal";
    if (const BallModifierConfig* m = game_.modifierConfig())
        if (!m->ballSprite.empty() && a_.Sprite(m->ballSprite)) ballSprite = m->ballSprite;
    Vec3 ballCarrierPos;
    bool carried = false;
    for (const auto& ent : game_.entities) {
        Entity* e = ent.get();
        if (e->isPlayer() || e->isEnemy() || e->isObstacle()) {
            auto it = vis_.find(e->uid);
            if (it == vis_.end() || !it->second.shown) continue;
            const Vis& v = it->second;
            Vec3 pos = v.Pos();
            float flash = v.flash > 0 ? 0.35f : 1.0f;
            uint32_t col = PackColor(1, flash, flash, std::clamp(v.fade, 0.0f, 1.0f));
            if (e->isPlayer()) {
                const RuntimeSprite& rs = PlayerSprite(static_cast<Player*>(e));
                BillboardTex(rs.tex, rs.def, pos, kActorScale, col);
            } else if (e->isEnemy()) {
                Enemy* en = static_cast<Enemy*>(e);
                if (en->stunned()) col = PackColor(0.7f, 0.7f * flash, 0.9f * flash, std::clamp(v.fade, 0.0f, 1.0f));
                Billboard(EnemySprite(en, v), pos, kActorScale, col);
                if (en->stunned()) {
                    const auto* anim = a_.Animation("TweetyBirds");
                    if (anim && !anim->empty()) {
                        int f = static_cast<int>(time_ / (*anim)[0].duration) % static_cast<int>(anim->size());
                        Billboard((*anim)[f].sprite, pos, kActorScale, kWhite, {0, 0.75f * kActorScale, 0});
                    }
                }
            } else {
                Obstacle* o = static_cast<Obstacle*>(e);
                Billboard(o->health < o->maxHealth() ? "Obstacle_Wall_Damaged" : "Obstacle_Wall_OK", pos, kActorScale, col);
            }
            if (e->uid == ballCarrier_) {
                carried = true;
                ballCarrierPos = pos;
            }
        } else if (e->isPickup() && !e->isBall()) {
            Treasure* t = static_cast<Treasure*>(e);
            if (t->acquired || !t->active || t->hiddenForEaster) continue;
            std::string s = t->config->sprite;
            const char* animName = s.find("Pumpkin") != std::string::npos   ? "Pumpkin"
                                   : s.find("Easter") != std::string::npos ? "EasterEgg"
                                   : s.find("Gem") != std::string::npos    ? "Gem"
                                                                            : nullptr;
            if (const auto* anim = animName ? a_.Animation(animName) : nullptr) {
                float total = 0;
                for (const auto& f : *anim) total += f.duration;
                float tt = std::fmod(time_, total);
                for (const auto& f : *anim) {
                    if (tt < f.duration) {
                        s = f.sprite;
                        break;
                    }
                    tt -= f.duration;
                }
            }
            // Treasure prefab: SpriteAnchor at z +0.25, Sprite at local z +0.25
            Billboard(s, TileCenter(t->gridPos) + Vec3{0, 0, 0.25f}, kActorScale, kWhite, {0, 0, 0.25f});
        }
    }
    // Ball: BallSprite child of the carrier's sprite (local 0.25, 0, 0.10, inside the 0.75 scale),
    // or the Ball prefab's Sprite (local -0.25, 0, -0.25, scale 0.75) on the floor.
    if (carried) {
        Billboard(ballSprite, ballCarrierPos, kActorScale, kWhite, Vec3{0.25f, 0, 0.10f} * kActorScale);
    } else if (ballCarrier_ < 0) {
        float t = Ease(ballT_);
        Vec3 p = Lerp(ballFrom_, ballTo_, ballT_ >= 1 ? 1.0f : t);
        if (ballArc_ && ballT_ < 1) p.y += std::sin(t * kPi) * 0.8f;
        Billboard(ballSprite, p + Vec3{-0.25f, 0, -0.25f}, kActorScale, kWhite); // Billboard on the Sprite: world offset
    }
    // Hazards
    const Vec3 X{1, 0, 0}, Z{0, 0, 1};
    for (Hazard* h : game_.hazards) {
        Vec3 c = TileCenter(h->gridPos);
        if (h->isSpikeTrap()) {
            // Hazard_FloorSpikes: four holes on the floor and four billboarded spikes at +-0.2
            for (float dx : {-0.2f, 0.2f})
                for (float dz : {-0.2f, 0.2f}) {
                    Flat("Spikes_Hole_1", c + Vec3{dx, 0.005f, dz}, X, Z, kWhite);
                    Billboard("Spikes_Low", c + Vec3{dx, 0, dz}, 1.0f, kWhite);
                }
        } else {
            const std::string* s = AnimFrameAt("FloorTileWarning_LightningBolt", time_, true);
            Flat(s ? *s : "FloorTileFX_LightningTarget_1", c + Vec3{0, 0.007f, 0}, X, Z, kWhite);
        }
    }
}

void StageView::DrawIntentions(const V2* cursor, const std::vector<Command>& commands) {
    if (Busy()) return;
    for (Enemy* e : game_.enemies) {
        if (!e->isAlive || !e->hasIntention || e->stunned() || e->nextAction.isWait()) continue;
        Vec3 pos = ActorPosition(e->uid);
        V2 d = e->nextAction.direction;
        Vec3 dir{static_cast<float>(d.x), 0, static_cast<float>(d.y)};
        Vec3 start = pos - dir * 0.5f + Vec3{0, 0.009f, 0};
        if (e->nextAction.isStep()) {
            FlatSliced("Intention_EnemyMove", start, dir, static_cast<float>(e->moveRange()) + 0.5f, kWhite);
            continue;
        }
        switch (e->cAttack->attackBehaviour()) {
        case AttackBehaviour::AdjacentTile:
        case AttackBehaviour::Charge: {
            int len = std::max<int>(1, static_cast<int>(e->targetTiles.size()));
            FlatSliced(e->cAttack->wind() ? "Intention_EnemyAttack_Wind" : "Intention_EnemyAttack", start, dir, len + 0.5f, kWhite);
            break;
        }
        case AttackBehaviour::SpawnAdjacent:
            FlatSliced("Intention_SpawnAdjacent", start, dir, 1.5f, kWhite);
            break;
        case AttackBehaviour::SpawnNearFurthestPlayer:
            Flat("FloorTileFX_SpawnTarget", TileCenter(d) + Vec3{0, 0.009f, 0}, {1, 0, 0}, {0, 0, 1}, kWhite);
            break;
        case AttackBehaviour::LightningBolt:
            Flat("FloorTileFX_LightningTarget_1", TileCenter(d) + Vec3{0, 0.009f, 0}, {1, 0, 0}, {0, 0, 1}, kWhite);
            break;
        default:
            break;
        }
    }
    // Preview of the command under the cursor (IntentionMarkers_Player)
    if (!cursor || game_.phase() != Phase::PlayerInput || !game_.currentPlayer) return;
    for (const Command& c : commands) {
        if (!c.onTile || c.tile != *cursor || c.actionConfig->switchControl) continue;
        Vec3 dir{static_cast<float>(c.direction.x), 0, static_cast<float>(c.direction.y)};
        Vec3 start = ActorPosition(game_.currentPlayer->uid) - dir * 0.5f + Vec3{0, 0.011f, 0};
        int dist = std::abs(c.tile.x - game_.currentPlayer->gridPos.x) + std::abs(c.tile.y - game_.currentPlayer->gridPos.y);
        bool attack = c.actionConfig->action == ActorAction::Char_Attack;
        FlatSliced(attack ? "Intention_PlayerAttack" : "Intention_PlayerMove", start, dir, dist + 0.5f, kWhite);
    }
}

void StageView::Draw(const V2* cursor, const std::vector<Command>& commands) {
    float aspect = static_cast<float>(r_.lowW()) / r_.lowH();
    r_.BeginScene(cam_.Proj(aspect), cam_.View(), 0.03f, 0.03f, 0.05f);
    DrawArena();
    DrawFloor(cursor, commands);
    DrawIntentions(cursor, commands);
    DrawActors();
    DrawEffects();
}

// ---------------------------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------------------------
float StageView::FxRand() {
    fxSeed_ = fxSeed_ * 1664525u + 1013904223u;
    return (fxSeed_ >> 8) / 16777216.0f;
}

void StageView::Burst(Vec3 at, int count, float r, float g, float b, float speed, float up, float gravity, float life, float size) {
    for (int i = 0; i < count; i++) {
        Particle p;
        float ang = FxRand() * 2 * kPi, sp = speed * (0.4f + 0.6f * FxRand());
        p.p = at;
        p.v = {std::cos(ang) * sp, up * (0.5f + FxRand()), std::sin(ang) * sp};
        p.maxLife = p.life = life * (0.6f + 0.4f * FxRand());
        p.size = size * (0.7f + 0.6f * FxRand());
        p.gravity = gravity;
        p.r = r;
        p.g = g;
        p.b = b;
        particles_.push_back(p);
    }
}

void StageView::UpdateEffects(float dt) {
    for (Particle& p : particles_) {
        p.life -= dt;
        p.v.y -= p.gravity * dt;
        p.p += p.v * dt;
        if (p.p.y < 0.01f) { // land on the floor
            p.p.y = 0.01f;
            p.v = {0, 0, 0};
        }
        if (p.gravity == 0) p.v = p.v * (1 - std::min(1.0f, dt * 3)); // smoke slows down
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0; }), particles_.end());
    for (SpriteFx& f : spriteFx_) f.t += dt;
    spriteFx_.erase(std::remove_if(spriteFx_.begin(), spriteFx_.end(), [&](const SpriteFx& f) { return !AnimFrameAt(f.anim, f.t, false); }),
                    spriteFx_.end());
}

const std::string* StageView::AnimFrameAt(const std::string& anim, float t, bool loop) const {
    const auto* frames = a_.Animation(anim);
    if (!frames || frames->empty()) return nullptr;
    float total = 0;
    for (const auto& f : *frames) total += f.duration;
    if (loop) t = std::fmod(t, total);
    for (const auto& f : *frames) {
        if (t < f.duration) return &f.sprite;
        t -= f.duration;
    }
    return nullptr;
}

void StageView::DrawEffects() {
    for (const SpriteFx& f : spriteFx_)
        if (const std::string* s = AnimFrameAt(f.anim, f.t, false); s && !s->empty()) Billboard(*s, f.pos, 1.0f, kWhite);
    if (particles_.empty()) return;
    // camera-facing squares, one batch on the white texture
    batch_.clear();
    for (const Particle& p : particles_) {
        float k = p.life / p.maxLife;
        float s = p.gravity == 0 ? p.size * (1.5f - 0.5f * k) : p.size; // smoke puffs grow as they fade
        uint32_t c = PackColor(p.r, p.g, p.b, p.gravity == 0 ? k * 0.8f : std::min(1.0f, k * 3));
        Vec3 rx = cam_.right * s, uy = cam_.up * s;
        Vec3 a = p.p - rx - uy, b = p.p + rx - uy, cc = p.p + rx + uy, d = p.p - rx + uy;
        batch_.push_back({a.x, a.y, a.z, 0, 0, c});
        batch_.push_back({b.x, b.y, b.z, 1, 0, c});
        batch_.push_back({cc.x, cc.y, cc.z, 1, 1, c});
        batch_.push_back({a.x, a.y, a.z, 0, 0, c});
        batch_.push_back({cc.x, cc.y, cc.z, 1, 1, c});
        batch_.push_back({d.x, d.y, d.z, 0, 1, c});
    }
    r_.Draw(Texture{}, batch_.data(), static_cast<int>(batch_.size()), Blend::Alpha, false);
}

void StageView::DrawOverlay() {
    const Font& f = a_.font();
    for (const auto& ent : game_.entities) {
        Entity* e = ent.get();
        if (!(e->isEnemy() || e->isPlayer() || e->isObstacle())) continue;
        Actor* a = static_cast<Actor*>(e);
        auto it = vis_.find(e->uid);
        if (it == vis_.end() || !it->second.shown || it->second.dying || !a->isAlive) continue;
        float sx, sy;
        if (!Project(it->second.Pos() + Vec3{0, 1.2f, 0}, &sx, &sy)) continue;
        std::string s = std::to_string(a->health);
        if (e->isEnemy()) {
            Enemy* en = static_cast<Enemy*>(e);
            if (en->stunned()) s += " z";
            s = std::to_string(en->actOrder) + ": " + s;
        }
        float w = f.Width(s, 1.5f);
        r_.Fill(sx - w * 0.5f - 3, sy - 2, w + 6, f.height * 1.5f + 2, PackColor(0, 0, 0, 0.6f));
        f.Draw(r_, sx - w * 0.5f, sy - 1, 1.5f, e->isPlayer() ? PackColor(0.6f, 1, 0.6f, 1) : PackColor(1, 0.7f, 0.6f, 1), s);
    }
}

} // namespace dd::gfx
