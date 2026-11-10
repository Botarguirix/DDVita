// How to Play (HowToPlayPageConfig pages plus the port's controls page) and the in-game info popup
// (EnemyInfoPopup / PlayerStatsPopup / TileInfoConfig) for the tile under the cursor.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
using namespace dd::gfx;
using ui::Align;
namespace col = ui::col;

void App::DrawHowToPlay() {
    if ((helpReturn_ == Screen::Pause || helpReturn_ == Screen::TeamInfo) && st_.view) ui_.Shade(0.75f);
    const float W = ui_.W(), H = ui_.H();
    ui_.Text(ui::Large, W * 0.5f, 8, col::White, T("FrontEnd/HowToPlay", "How to Play"), Align::Center);
    // the page list
    int pages = HelpPageCount();
    float lx = 14, ly = 40, rowH = 20;
    int visible = static_cast<int>((H - ly - 40) / rowH);
    int first = std::clamp(sel_ - visible / 2, 0, std::max(0, pages - visible));
    for (int i = first; i < pages && i < first + visible; i++) {
        std::string name = i < static_cast<int>(help_.pages.size())
                               ? T(help_.pages[static_cast<size_t>(i)].header, help_.pages[static_cast<size_t>(i)].headerFallback)
                               : T("General/Help_Controls", "Controls");
        bool on = i == sel_;
        float y = ly + (i - first) * rowH;
        ui_.Panel(lx, y, 150, rowH - 2, on ? col::Rgb(190, 38, 51, 0.95f) : col::Rgb(27, 38, 50, 0.9f), on ? col::White : col::DarkGrey);
        ui_.Text(ui::Small, lx + 8, y + 3, col::White, name);
    }
    // the page
    float px = 176, pw = W - px - 14, py = 40, ph = H - py - 36;
    ui_.Panel(px, py, pw, ph);
    float y = py + 10 - helpScroll_, x = px + 12, tw = pw - 24;
    float bottom = py + ph - 6;
    auto visibleLine = [&](float yy, float h) { return yy >= py + 2 && yy + h <= bottom; };
    if (sel_ < static_cast<int>(help_.pages.size())) {
        const HelpPage& page = help_.pages[static_cast<size_t>(sel_)];
        for (const HelpItem& it : page.items) {
            // images (sprites and button prompts) in a row above the text
            float ix = x;
            bool anyImage = false;
            for (const HelpImage& im : it.images) {
                if (im.player) continue; // a generated player: shown by the stage itself
                if (!im.sprite.empty() && visibleLine(y, 32)) {
                    ui_.Sprite(im.sprite, ix, y, 32, 32);
                    anyImage = true;
                    ix += 36;
                }
            }
            if (anyImage) y += 36;
            if (it.hasSub && !it.sub.empty()) {
                std::string sub = T(it.sub, "");
                if (visibleLine(y, ui_.LineHeight(ui::Medium))) ui_.Text(ui::Medium, x, y, ui::ParseColor(it.subColor), sub);
                y += ui_.LineHeight(ui::Medium) + 4;
            }
            std::string text = T(it.text, it.textFallback);
            uint32_t c = it.color.empty() ? col::White : ui::ParseColor(it.color);
            float lh = ui_.LineHeight(ui::Small) + 2;
            for (const std::string& line : ui_.Wrap(ui::Small, text, tw)) {
                if (visibleLine(y, lh)) ui_.Text(ui::Small, x, y, c, line);
                y += lh;
            }
            y += 10;
        }
    } else {
        // the port's controls page (the original's is for keyboard / pad)
        ui_.Text(ui::Medium, x, y, col::Yellow, T("General/Help_Controls", "Controls"));
        y += 24;
        std::pair<Input, std::string> rows[] = {
            {Input::Up, T("Gameplay/Action_Move", "Move") + " (cursor)"},
            {Input::Confirm, T("Gameplay/Command_Confirm", "Confirm")},
            {Input::PassTake, T("Gameplay/Action_Pass", "Pass") + " / " + T("Gameplay/Action_Take", "Take")},
            {Input::Shoot, T("Gameplay/Action_Shoot", "Shoot")},
            {Input::Rest, T("Gameplay/Action_Rest", "Rest") + " / " + T("General/MenuNav_Back", "Back")},
            {Input::Undo, T("Gameplay/Action_Undo", "Undo")},
            {Input::Switch, T("Gameplay/Action_SwitchPlayer", "Switch Player")},
            {Input::Info, T("Gameplay/Help", "Help")},
            {Input::Menu, T("Gameplay/Pause", "Pause")},
        };
        for (const auto& [in, label] : rows) {
            if (visibleLine(y, 18)) {
                float w = ui_.Prompt(x, y, platform::ButtonName(in), platform::ButtonSprite(in));
                ui_.Text(ui::Small, x + std::max(w, 70.0f), y, col::White, label);
            }
            y += 20;
        }
    }
    helpMaxScroll_ = std::max(0.0f, y + helpScroll_ - bottom + 10);
    if (helpMaxScroll_ > 0) {
        float frac = helpScroll_ / helpMaxScroll_;
        ui_.Fill(px + pw - 4, py + 4 + (ph - 28) * frac, 2, 20, col::Grey);
    }
    std::vector<std::pair<Input, std::string>> hints = {{Input::Back, T("General/MenuNav_Back", "Back")}};
    float lh = ui_.LineHeight(ui::Small);
    float hy = H - lh - 8;
    ui_.Fill(0, hy - 5, W, lh + 10, PackColor(0, 0, 0, 0.6f));
    float hx = W * 0.5f - 120;
    hx += ui_.Text(ui::Small, hx, hy, col::Yellow, "[" + std::string(platform::ButtonName(Input::Up)) + "]") + 4;
    hx += ui_.Text(ui::Small, hx, hy, col::White, T("General/MenuNav_Next", "Next")) + 12;
    if (helpMaxScroll_ > 0) {
        hx += ui_.Text(ui::Small, hx, hy, col::Yellow, "[" + std::string(platform::ButtonName(Input::Left)) + "]") + 4;
        hx += ui_.Text(ui::Small, hx, hy, col::White, T("General/MenuNav_Scroll", "Scroll")) + 12;
    }
    hx += ui_.Prompt(hx, hy, platform::ButtonName(Input::Back), platform::ButtonSprite(Input::Back)) + 2;
    ui_.Text(ui::Small, hx, hy, col::White, T("General/MenuNav_Back", "Back"));
}

// "View more info" (R): what is on the tile under the cursor
void App::DrawInfoPopup() {
    Game& g = *st_.game;
    if (!g.PositionIsWithinField(st_.cursor)) return;
    FieldTile* tile = g.GetTile(st_.cursor);
    const float W = ui_.W();
    float w = 210, x = W - w - 8, y = 28;
    std::vector<std::pair<std::string, uint32_t>> lines;
    std::string header;
    uint32_t headerCol = col::Rgb(115, 41, 48);
    auto popup = [&](const char* field, const char* fb) { return T(help_.Popup("EnemyInfoPopup", field, ""), fb); };
    if (tile->occupant && tile->occupant->entityType == EntityType::Enemy) {
        if (!settings_.enemyPopups) return; // EnemyInfoPopup.blockPopup
        const Enemy* e = static_cast<const Enemy*>(tile->occupant);
        header = e->Name();
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s %d/%d", T("Gameplay/Health", "Health").c_str(), e->health, e->maxHealth());
        lines.emplace_back(buf, col::Red);
        const AttackConfig* a = e->config->attackConfig;
        std::string attack = !a ? popup("localizedAttackNone", "Cannot attack.")
                             : a->attackBehaviour == AttackBehaviour::Charge ? popup("localizedAttackCharge", "Charges in straight line.")
                             : a->attackBehaviour == AttackBehaviour::LightningBolt ? popup("localizedAttackLightning", "Attacks any tile.")
                             : a->spawner() ? popup("localizedAttackSpawn", "Spawns more monsters.")
                             : a->pierce ? popup("localizedAttackPierce", "Attacks a row of adjacent tiles.")
                             : a->attackBehaviour == AttackBehaviour::None ? popup("localizedAttackNone", "Cannot attack.")
                                                                           : popup("localizedAttackAdjacent", "Attacks adjacent tiles.");
        lines.emplace_back(attack, col::White);
        if (a && a->attackDamage) lines.emplace_back(T("Gameplay/Attack_Damage", "Damage") + " " + std::to_string(a->attackDamage), col::Orange);
        static const char* const moves[] = {"localizedMoveRandom", "localizedMoveChaseBall", "localizedMoveBlockBall", "localizedMoveStatic",
                                            "localizedMoveChasePlayers", "localizedMoveRandom"};
        static const char* const movesFb[] = {"Wanders aimlessly.", "Chases the ball.", "Blocks the ball.", "Cannot move.", "Chases players.",
                                              "Wanders aimlessly."};
        int mb = static_cast<int>(e->moveBehaviour());
        if (mb >= 0 && mb < 6) lines.emplace_back(popup(moves[mb], movesFb[mb]), col::Grey);
        if (e->moveRange() > 1) lines.emplace_back(T("Gameplay/MoveRange", "Move Range") + " " + std::to_string(e->moveRange()), col::Grey);
        if (e->knockbackResistance()) lines.emplace_back(T("Gameplay/PushResist", "Push Resist") + " " + std::to_string(e->knockbackResistance()), col::Grey);
        if (e->config->ballCatcher) lines.emplace_back(T("Gameplay/CatchesShots", "Catches shots"), col::Yellow);
        if (e->stunned()) lines.emplace_back(T("Gameplay/Stunned", "Stunned"), col::Blue);
    } else if (tile->occupant && tile->occupant->entityType == EntityType::Player) {
        const Player* p = static_cast<const Player*>(tile->occupant);
        const HumanPlayer& hp = *p->humanPlayer;
        header = p->Name();
        headerCol = col::Rgb(0, 87, 132);
        lines.emplace_back(ClassName(hp.playerClassConfig), col::White);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s %d/%d", T("Gameplay/Stamina", "Stamina").c_str(), p->stamina, p->maxStamina());
        lines.emplace_back(buf, col::Green);
        lines.emplace_back(T("Gameplay/Strength", "Strength") + " " + std::to_string(hp.GetAttackStrength()), col::Orange);
        lines.emplace_back(T("Gameplay/ActionPointsShort", "AP") + " " + std::to_string(p->actionPoints) + "/" + std::to_string(hp.GetMaxAP()), col::Blue);
        lines.emplace_back(T("General/Perk", "Perk") + ": " + PerkName(hp.perkChain) +
                               (hp.perkLevel > 0 ? " " + std::to_string(hp.perkLevel) : " (" + T("General/Locked", "Locked") + ")"),
                           col::Yellow);
    } else {
        // TileInfoConfig: what else the tile holds
        if (!settings_.tilePopups) return; // TileInfoPopup
        const char* key = tile->containsBall() ? "Ball"
                          : tile->containsGem() ? (run_->IsHalloween() ? "Gem_HalloweenEvent" : "Gem")
                          : tile->occupant && tile->occupant->entityType == EntityType::Obstacle ? "Wall"
                          : g.PositionIsPastFinishLine(st_.cursor.x) ? "FinishLine"
                                                                     : nullptr;
        for (const Hazard* h : g.hazards)
            if (h->gridPos == st_.cursor) key = h->config->kind == HazardKind::Lightning ? "LightningHazard" : "SpikeTrap";
        if (!key) return;
        auto it = help_.tiles.find(key);
        if (it == help_.tiles.end()) return;
        header = T(it->second.header, it->second.headerFallback);
        if (!it->second.color.empty()) headerCol = ui::ParseColor(it->second.color, 0.95f);
        lines.emplace_back(T(it->second.text, it->second.textFallback), col::White);
        if (tile->containsBall())
            if (const BallModifierConfig* bm = g.modifierConfig())
                lines.emplace_back(ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", bm->friendlyName}}), col::Yellow);
    }
    // measure, then draw
    float lh = ui_.LineHeight(ui::Small) + 2, h = 30;
    std::vector<std::pair<std::vector<std::string>, uint32_t>> wrapped;
    for (const auto& [text, c] : lines) {
        wrapped.emplace_back(ui_.Wrap(ui::Small, text, w - 16), c);
        h += static_cast<float>(wrapped.back().first.size()) * lh;
    }
    ui_.Panel(x, y, w, h + 4, col::Rgb(20, 12, 28, 0.92f), col::Grey);
    ui_.Fill(x + 1, y + 1, w - 2, 22, headerCol);
    ui_.Text(ui::Medium, x + 8, y + 4, col::White, header);
    float ty = y + 28;
    for (const auto& [ls, c] : wrapped)
        for (const std::string& l : ls) {
            ui_.Text(ui::Small, x + 8, ty, c, l);
            ty += lh;
        }
}

} // namespace dd::app
