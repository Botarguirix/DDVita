// Canvas_Gameplay's GameControls/Popups for the tile under the cursor (the original shows them on
// mouse-over): EnemyInfoPopup (top right), InfoPopup_Tile (TileInfoPopup, bottom right) and the
// CommandTooltip over the cursor's command (stamina and AP costs, the attack or the shot). R hides /
// shows them; the Options' popup settings turn each off.
#include <algorithm>
#include <cstdio>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
const uint32_t kGreyDark = col::Rgb(101, 109, 113), kTeal = col::Rgb(47, 72, 78), kBlueDark = col::Rgb(0, 87, 132);
const char* const kDivider = "<color=#656d71>/</color>";
} // namespace

bool App::DrawGameplayPopups() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0 || !st_.game || !st_.view) return false;
    Game& g = *st_.game;
    if (!g.PositionIsWithinField(st_.cursor)) return true;
    FieldTile* tile = g.GetTile(st_.cursor);
    int controls = c.Find("GameControls", root), popups = c.Find("Popups", controls);
    int enemyPopup = c.Find("EnemyInfoPopup", popups), tilePopup = c.Find("InfoPopup_Tile", popups);
    int tooltip = c.Find("CommandTooltip", popups);
    bool anyShown = false;
    for (int k : c.node(popups).kids) c.SetActive(k, false);
    auto popup = [&](const char* field, const char* fb) { return T(help_.Popup("EnemyInfoPopup", field, ""), fb); };

    // EnemyInfoPopup.Display
    if (showInfo_ && settings_.enemyPopups && tile->occupant && tile->occupant->entityType == EntityType::Enemy && enemyPopup >= 0) {
        const Enemy& e = *static_cast<const Enemy*>(tile->occupant);
        const EnemyConfig& cfg = *e.config;
        int b = c.Find("Borders", enemyPopup), contents = c.Find("Body/Contents", b);
        c.SetActive(enemyPopup, true);
        anyShown = true;
        c.SetText(c.Find("Header/Text_M", b), e.Name());
        c.SetText(c.Find("Health/Text_S", contents), T("Gameplay/Health", "Health"));
        c.SetText(c.Find("Health/Text_M", contents), std::to_string(e.health) + kDivider + std::to_string(e.maxHealth()));
        c.SetText(c.Find("ActOrder/Text_M", contents), std::to_string(e.actOrder) + kDivider + std::to_string(g.enemies.size()));
        // movement behaviour
        static const char* const moves[] = {"localizedMoveRandom", "localizedMoveChaseBall", "localizedMoveBlockBall", "localizedMoveStatic",
                                            "localizedMoveChasePlayers", "localizedMoveRandom"};
        static const char* const movesFb[] = {"Wanders aimlessly.", "Chases the ball.", "Blocks the ball.", "Cannot move.", "Chases players.", "Avoids players."};
        const uint32_t moveCols[] = {col::Green, col::Orange, col::Blue, col::Purple, col::Rgb(68, 137, 26), col::Rgb(115, 41, 48)};
        int mb = std::clamp(static_cast<int>(cfg.moveBehaviour), 0, 5);
        int mt = c.Find("MovementBehaviour/Text_S (Movement)", contents);
        c.SetText(mt, popup(moves[mb], movesFb[mb]));
        c.SetTextColor(mt, moveCols[mb]);
        int mr = c.Find("MoveRange", contents);
        bool moves1 = e.moveRange() > 0;
        c.SetText(c.Find("Text_M (MoveRange)", mr), moves1 ? std::to_string(e.moveRange()) : "-");
        for (const char* n : {"Text_M (MoveRange)", "Text_S"}) c.SetTextColor(c.Find(n, mr), moves1 ? col::White : kGreyDark);
        c.SetImageColor(c.Find("Icon", mr), moves1 ? col::White : kGreyDark);
        // attack behaviour and the AttackTooltip
        const AttackConfig* a = cfg.attackConfig;
        std::string attack = !a || a->attackBehaviour == AttackBehaviour::None ? popup("localizedAttackNone", "Cannot attack.")
                             : a->attackBehaviour == AttackBehaviour::Charge ? popup("localizedAttackCharge", "Charges in straight line.")
                             : a->attackBehaviour == AttackBehaviour::LightningBolt ? popup("localizedAttackLightning", "Attacks any tile.")
                             : a->spawner() ? popup("localizedAttackSpawn", "Spawns more monsters.")
                             : a->range > 1 ? popup("localizedAttackPierce", "Attacks a row of adjacent tiles.")
                                            : popup("localizedAttackAdjacent", "Attacks adjacent tiles.");
        c.SetText(c.Find("AttackBehaviour/Text_S (Attack)", contents), attack);
        int at = c.Find("AttackTooltip", contents);
        if (e.cAttack) FillAttackTooltip(c, at, e.cAttack->config, e.cAttack->attackDamage(), e.cAttack->knockbackDistance(), e.cAttack->stunValue(), false);
        c.SetActive(c.Find("KnockbackResist", contents), cfg.knockbackResistance > 0);
        c.SetText(c.Find("KnockbackResist/Text_M (KnockbackResist)", contents), std::to_string(cfg.knockbackResistance));
        c.SetActive(c.Find("Catcher", contents), cfg.ballCatcher);
        // stunned, else the plan
        int strip = c.Find("Body/BottomStrip", b);
        bool stunned = e.stunned();
        c.SetActive(c.Find("Stunned", strip), stunned);
        c.SetActive(c.Find("Intention", strip), !stunned);
        if (stunned) {
            int left = std::max(1, e.cStun->stunTurnDuration - (g.Turn() - e.cStun->turnStunned));
            std::string s = e.cStun->stunnedIndefinitely() ? popup("localizedStunnedIndefinitely", "Stunned")
                                                          : ui::Format(popup("localizedStunnedCounter", "Stunned ({value})"), {{"value", ui::Int(left)}});
            c.SetText(c.Find("Stunned/Text_S", strip), s);
        } else {
            const char* key = "localizedWait";
            const char* fb = "Wait";
            const char* icon = "Icons_IntentionIcon_Wait";
            V2 dir{0, 1};
            if (e.hasIntention && e.nextAction.isStep()) key = "localizedMove", fb = "Move", icon = "Icons_IntentionIcon_Move", dir = e.nextAction.direction;
            else if (e.hasIntention && e.nextAction.isAttack() && e.cAttack) {
                dir = e.nextAction.direction;
                if (e.cAttack->pierce()) key = "localizedPierceAttack", fb = "Pierce Attack", icon = "Icons_IntentionIcon_Attack";
                else if (e.cAttack->wind()) key = "localizedWindAttack", fb = "Wind Attack", icon = "Icons_IntentionIcon_Attack";
                else if (e.cAttack->attackBehaviour() == AttackBehaviour::Charge) key = "localizedChargeAttack", fb = "Charge", icon = "Icons_IntentionIcon_Charge";
                else if (e.cAttack->attackBehaviour() == AttackBehaviour::LightningBolt) key = "localizedLightningAttack", fb = "Lightning", icon = "Icons_IntentionIcon_Lightning";
                else if (e.cAttack->config->spawner()) key = "localizedSpawn", fb = "Spawn", icon = "Icons_IntentionIcon_Spawn";
                else key = "localizedAttack", fb = "Attack", icon = "Icons_IntentionIcon_Attack";
            }
            int in = c.Find("Intention", strip);
            c.SetText(c.Find("Text_S (Intention)", in), popup(key, fb));
            int ic = c.Find("Icon/IntentionIcon", in);
            c.SetSprite(ic, icon);
            // up 0, down 180, left 90, right 270 (the field's y is the screen's up)
            c.node(ic).rotation = dir.y < 0 ? 180.0f : dir.x < 0 ? 90.0f : dir.x > 0 ? 270.0f : 0.0f;
        }
    }

    // TileInfoPopup.ShowTileInfo: what else the tile holds
    if (showInfo_ && settings_.tilePopups && tilePopup >= 0 && !(tile->occupant && tile->occupant->entityType == EntityType::Enemy)) {
        const char* key = tile->containsBall() ? "Ball"
                          : tile->containsGem() ? (run_->IsHalloween() ? "Gem_HalloweenEvent" : "Gem")
                          : tile->occupant && tile->occupant->entityType == EntityType::Obstacle ? "Wall"
                          : g.PositionIsPastFinishLine(st_.cursor.x) ? "FinishLine"
                                                                     : nullptr;
        const Hazard* hz = nullptr;
        for (const Hazard* h : g.hazards)
            if (h->gridPos == st_.cursor) key = h->config->kind == HazardKind::Lightning ? "LightningHazard" : "SpikeTrap", hz = h;
        auto it = key ? help_.tiles.find(key) : help_.tiles.end();
        if (it != help_.tiles.end()) {
            const TileInfo& ti = it->second;
            std::string header = T(ti.header, ti.headerFallback), body = T(ti.text, ti.textFallback);
            if (hz) { // a hazard's: its name, its damage
                if (header.empty()) header = !hz->config->actorName.empty() ? hz->config->actorName : hz->config->name;
                body = ui::Format(body, {{"damage", ui::Int(hz->config->damage)}});
            }
            if (ti.isBall)
                if (const BallModifierConfig* bm = g.modifierConfig()) header = ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", bm->friendlyName}});
            int b = c.Find("Borders", tilePopup);
            c.SetActive(tilePopup, true);
            anyShown = true;
            c.SetText(c.Find("Header/Text_M (Header)", b), header);
            c.SetText(c.Find("Body/Text_S", b), body);
            // InfoPopup.SetBorderColour
            uint32_t k = ti.color.empty() ? kBlueDark : ui::ParseColor(ti.color, 1);
            c.SetImageColor(c.Find("Header/BG", b), k);
            c.SetImageColor(c.Find("Outline", b), k);
        }
    }

    // CommandTooltip over the cursor's command
    const Command* cmd = nullptr;
    for (const Command& k : st_.commands)
        if (k.onTile && k.tile == st_.cursor && !k.actionConfig->switchControl && !k.actionConfig->undo) cmd = &k;
    if (cmd && settings_.commandCostPopups && tooltip >= 0) {
        c.SetActive(tooltip, true);
        anyShown = true;
        int sa = c.Find("StaminaAndAP", tooltip);
        int st = c.Find("Stamina", sa), ap = c.Find("AP", sa);
        c.SetText(c.Find("Text_S", st), T("Gameplay/Stamina", "Stamina"));
        c.SetText(c.Find("Text_S", ap), T("Gameplay/ActionPointsShort", "AP"));
        if (cmd->staminaReplenish > 0) {
            c.SetText(c.Find("Text_M (Stamina)", st), "+" + std::to_string(cmd->staminaReplenish));
            c.SetImageColor(c.Find("BG", st), col::Rgb(68, 137, 26));
        } else {
            c.SetText(c.Find("Text_M (Stamina)", st), std::to_string(cmd->staminaCost));
            c.SetImageColor(c.Find("BG", st), cmd->player->stamina >= cmd->staminaCost ? col::Orange : col::Red);
        }
        c.SetText(c.Find("Text_M (AP)", ap), std::to_string(cmd->actionPointCost));
        c.SetImageColor(c.Find("BG", ap), cmd->player->actionPoints >= cmd->actionPointCost ? col::Blue : col::Red);
        int at = c.Find("AttackTooltip", tooltip);
        ActorAction act = cmd->actionConfig->action;
        c.SetActive(at, act == ActorAction::Char_Attack || act == ActorAction::Player_Shoot);
        Player* p = cmd->player;
        if (act == ActorAction::Char_Attack && p->cAttack)
            FillAttackTooltip(c, at, p->cAttack->config, p->cAttack->attackDamage(), p->cAttack->knockbackDistance(), p->cAttack->stunValue(), false);
        else if (act == ActorAction::Player_Shoot)
            FillAttackTooltip(c, at, data_.prefabs.ballAttackConfig, g.ball ? g.ball->GetDamageTotal() : 0, g.ball ? g.ball->GetKnockbackTotal() : 0,
                              g.ball ? g.ball->GetStunDuration() : 0, true);
        // over the tile (attachTooltipToWorld), above the health bars
        float sx, sy;
        if (st_.view->ProjectTile(st_.cursor, &sx, &sy)) {
            c.Layout(ui_, root, loc_);
            float k = c.node(root).rect.w / static_cast<float>(r_.outW());
            ui::CNode& n = c.node(tooltip);
            n.pos[0] = std::clamp(sx * k, 44.0f, c.node(root).rect.w - 44);
            n.pos[1] = (static_cast<float>(r_.outH()) - sy) * k + 30 + (act == ActorAction::Char_Attack ? 6.0f : 0.0f);
        }
    }
    if (!anyShown) return true;
    // only the popups
    std::vector<int> kids = c.node(root).kids, ckids = c.node(controls).kids;
    std::vector<bool> was, cwas;
    for (int k : kids) was.push_back(c.node(k).active), c.SetActive(k, k == controls);
    for (int k : ckids) cwas.push_back(c.node(k).active), c.SetActive(k, k == popups);
    float appear = c.appear;
    c.appear = 1;
    c.Draw(ui_, root, loc_);
    c.appear = appear;
    for (size_t i = 0; i < kids.size(); i++) c.SetActive(kids[i], was[i]);
    for (size_t i = 0; i < ckids.size(); i++) c.SetActive(ckids[i], cwas[i]);
    return true;
}

// MenuPage_SuggestUndo (GameController.SuggestUndo): Undo / Ignore
bool App::DrawSuggestUndoCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0) return false;
    int pages = c.Find("MenuPages", root);
    int page = c.Find("MenuPage_SuggestUndo", pages);
    if (page < 0) return false;
    std::vector<int> kids = c.node(root).kids;
    std::vector<bool> was;
    for (int k : kids) was.push_back(c.node(k).active), c.SetActive(k, k == pages);
    for (int k : c.node(pages).kids) c.SetActive(k, k == page);
    int buttons = c.Find("PageArea/Buttons", page);
    c.SetActive(c.Find("Undo/Button_SuggestUndo (Undo)/SelectedOutline", buttons), sel2_ == 0);
    c.SetActive(c.Find("DontUndo/Button_SuggestUndo (Ignore)/SelectedOutline", buttons), sel2_ == 1);
    c.Draw(ui_, root, loc_);
    c.SetActive(page, false);
    for (size_t i = 0; i < kids.size(); i++) c.SetActive(kids[i], was[i]);
    return true;
}

} // namespace dd::app
