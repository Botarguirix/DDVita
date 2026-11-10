// The stage HUD from the original's Canvas_Gameplay (StageHUD: stage number, game mode, turn phases
// and turn nodes, the toolbar) with a PlayerInfoPanel prefab per player in PIPs, filled in the way
// GameplayUI, PhasesDisplay, TurnDisplay, PlayerInfoPanel and APGauge do.
#include <algorithm>
#include <cmath>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
uint32_t Grey() { return col::Grey; } // PhasesDisplay.greyedOutColour (arneGrey)
} // namespace

bool App::DrawHudCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0 || !prefabUi_.loaded() || !st_.game) return false;
    Game& g = *st_.game;
    if (hudMark_ < 0) {
        hudMark_ = c.Mark();
        // the authored colours the scripts restore (PhasesDisplay.Awake)
        for (const char* p : {"PlayerPhase/Image", "HazardPhase/Image", "MonsterPhase/Image"}) {
            int i = c.Find(std::string("StageHUD/StageInfo/Borders/TurnPhases/PhaseBlocks/") + p, root);
            phaseColors_.push_back(i >= 0 ? c.node(i).imageColor : col::White);
        }
    }
    c.Truncate(hudMark_);

    // what is not part of the HUD here: the menus, the mouse popups and the on-screen commands (the
    // Vita plays with the buttons; the port's hints show them)
    c.SetActive(c.Find("MenuPages", root), false);
    c.SetActive(c.Find("GameControls", root), false);
    c.SetActive(c.Find("SeasonalEventPopups", root), false);
    c.SetActive(c.Find("UndoPopup", root), false);
    int hud = c.Find("StageHUD", root);
    c.SetActive(c.Find("StageHUD/StageInfo/Borders/Info/TurnNumberFlash", root), false);
    // ShortcutIconDisplay: the button that opens each (Start: pause, Select: help); none for act order
    auto shortcut = [&](const char* path, Input in) {
        int i = c.Find(path, hud);
        const char* s = platform::ButtonSprite(in);
        c.SetActive(i, s != nullptr);
        if (s) c.SetSprite(i, s);
    };
    shortcut("Toolbar/Button_Simple_GameplayMenu - Pause/ShortcutIcon", Input::Menu);
    c.SetActive(c.Find("Toolbar/Button_Simple_GameplayMenu - Help/ShortcutIcon", hud), false); // no button opens it here
    c.SetActive(c.Find("Toolbar/TeamInfoShortcut_Enemy/ShortcutIcon", hud), false);

    // SimpleStageNumberDisplay / SimpleGameModeDisplay
    c.SetText(c.Find("StageHUD/StageInfo/Borders/Info/StageNumber/Text_S (Stage)", root), StageName());
    c.SetText(c.Find("StageHUD/StageInfo/Borders/Info/StageNumber/Text_S (Mode)", root), ModeName(rec_.mode));

    // PhasesDisplay
    Phase ph = g.phase();
    bool on[3] = {ph == Phase::PlayerInput || ph == Phase::PlayerActing || ph == Phase::SuggestUndo, ph == Phase::HazardsActing,
                  ph == Phase::EnemiesActing};
    const char* blocks[3] = {"PlayerPhase/Image", "HazardPhase/Image", "MonsterPhase/Image"};
    for (int b = 0; b < 3; b++)
        c.SetImageColor(c.Find(std::string("StageHUD/StageInfo/Borders/TurnPhases/PhaseBlocks/") + blocks[b], root),
                        on[b] ? phaseColors_[static_cast<size_t>(b)] : Grey());

    // TurnDisplay: "Turn x / Par y" coloured by how close the par is, a node per turn of the par
    int turn = std::max(1, g.Turn()), par = g.TurnPar();
    int strip = c.Find("StageHUD/StageInfo/Borders/TurnPhases/TopStrip", root);
    int turnText = c.Find("Text_S", strip);
    c.SetText(turnText, ui::Format(T("Gameplay/Turns_CurrentOfPar", "Turn {0.turn} / Par {1.par}"), {{"turn", ui::Int(turn)}, {"par", ui::Int(par)}}));
    uint32_t tc = turn < par - 4 ? col::White : turn < par - 2 ? col::Yellow : turn == par ? col::Red : turn > par ? col::Grey : col::Orange;
    c.SetTextColor(turnText, tc);
    int nodes = c.Find("Turns", strip), nodePrefab = prefabUi_.Find("TurnNodeIcon");
    int bonus = g.ParBonusRankTarget();
    for (int i = 0; i < par && nodes >= 0; i++) {
        int n = c.Instantiate(prefabUi_, nodePrefab, nodes);
        c.SetSprite(n, i == turn - 1 ? "GameInfoIcons_TurnNode4_Current" : i < turn - 1 ? "GameInfoIcons_TurnNode4_Expired" : "GameInfoIcons_TurnNode4_Upcoming");
        c.SetImageColor(n, i >= turn - 1 && i < par + bonus ? col::Yellow : col::White);
    }

    // GameplayUI: a PlayerInfoPanel per living player, in PIPs
    int pips = c.Find("PIPs", hud), panel = prefabUi_.Find("PlayerInfoPanel");
    int apBg = prefabUi_.Find("APIcon_BG"), apIcon = prefabUi_.Find("APIcon");
    for (Player* p : g.players) {
        if (!p->isAlive || pips < 0) continue;
        const HumanPlayer& hp = *p->humanPlayer;
        int r = c.Instantiate(prefabUi_, panel, pips);
        c.SetText(c.Find("PlayerName/Text_S", r), hp.playerName);
        const gfx::RuntimeSprite& face = Portrait(hp);
        c.SetRuntimeSprite(c.Find("PlayerPortrait/Mask/Sprite", r), &face.tex, face.def);
        c.SetActive(c.Find("BG/CurrentPlayer", r), p == g.currentPlayer && screen_ == Screen::Play);
        c.SetActive(c.Find("DeadX", r), false);
        c.SetActive(c.Find("SwitchShortcutTooltip", r), false);
        c.SetActive(c.Find("Stamina/StaminaWarning", r), false);
        c.SetActive(c.Find("InfoPopups", r), false);
        c.SetActive(c.Find("Icons/Ball", r), p->isHoldingBall());
        c.SetSprite(c.Find("Icons/Class", r), std::string("Icons_Class_") + ToString(hp.playerClassConfig->playerClass));
        // SimplePerkIcon
        int perk = c.Find("Icons/PerkIcon", r);
        if (hp.perkUnlocked() && hp.perkChain) {
            c.SetSprite(perk, hp.perkChain->icon);
            c.SetSprite(c.Find("PerkLevel", perk), "Perks_Fill_3-" + std::to_string(std::clamp(hp.perkLevel, 1, 3)));
        } else {
            c.SetSprite(perk, "Perks_Locked");
            c.SetActive(c.Find("PerkLevel", perk), false);
        }
        // stamina gauge
        float fill = p->maxStamina() > 0 ? std::clamp(static_cast<float>(p->stamina) / p->maxStamina(), 0.0f, 1.0f) : 0.0f;
        for (const char* gname : {"Fill", "FillDifferenceProjected", "FillDifference"}) c.SetFill(c.Find(std::string("Stamina/StaminaGauge/") + gname, r), fill);
        c.SetText(c.Find("Stamina/StaminaGauge/Number", r), std::to_string(p->stamina));
        // APGauge: a background and a point icon per AP, pointIconStartX 10 and pointIconSpacing 12
        int ap = c.Find("AP", r);
        c.SetSprite(c.Find("Label", ap), p->actionPoints > 0 ? "PlayerInfoPanel_AP_Label_Full" : "PlayerInfoPanel_AP_Label_Empty");
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < p->maxAP(); i++) {
                if (pass == 1 && p->actionPoints < i + 1) continue;
                int n = c.Instantiate(prefabUi_, pass == 0 ? apBg : apIcon, ap);
                c.node(n).pos[0] = 10.0f + i * 12.0f;
                c.node(n).pos[1] = 0;
            }
    }
    c.Draw(ui_, root, loc_);
    return true;
}

} // namespace dd::app
