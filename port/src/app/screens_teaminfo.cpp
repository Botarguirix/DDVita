// Canvas_Gameplay's MenuPage_Help (the toolbar's Help button; the front touch screen here): TeamInfo
// with a PlayerStatsPopup per living player (stamina costs, class, perk, the attack and the shot as
// AttackTooltips), the TeamTraitsDisplay, the BallInfoPopup and the run's ScoreDisplay. Its buttons:
// Exit and How to Play.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
std::string Hex(uint32_t c) { // PackColor is RGBA little endian
    char b[16];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
    return b;
}
std::string Coloured(const std::string& s, uint32_t c) { return "<color=" + Hex(c) + ">" + s + "</color>"; }
std::string Commas(int v) {
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}
const uint32_t kOrangeBright = col::Rgb(247, 142, 49), kPurple = col::Rgb(153, 100, 249), kGreyDark = col::Rgb(101, 109, 113),
               kRedDark = col::Rgb(115, 41, 48), kBlueDeep = col::Rgb(0, 87, 132), kGreen = col::Rgb(68, 137, 26);
} // namespace

// AttackTooltip.ShowInfo
void App::FillAttackTooltip(ui::Canvas& c, int tip, const AttackConfig* cfg, int damage, int knockback, int stun, bool shoot) {
    int contents = c.Find("Contents", tip);
    auto stat = [&](const char* row, const char* text, int v, uint32_t on) {
        int r = c.Find(row, contents);
        uint32_t k = v > 0 ? on : kGreyDark;
        c.SetText(c.Find(text, r), std::to_string(v));
        c.SetTextColor(c.Find(text, r), k);
        c.SetImageColor(c.Find("Icon", r), k);
        if (int l = c.Find("Text_S", r); l >= 0) c.SetTextColor(l, k);
    };
    stat("Damage", "Text_M (Damage)", damage, kOrangeBright);
    stat("Knockback", "Text_M (Knockback)", knockback, col::Yellow);
    stat("Stun", "Text_M (Stun)", stun, kPurple);
    c.SetImageColor(c.Find("BG", tip), shoot ? kBlueDeep : kRedDark);
    int special = c.Find("SpecialInfo", contents);
    const char* label = nullptr;
    const char* icon = nullptr;
    if (cfg) {
        if (shoot && !cfg->pierce && !cfg->wind) label = nullptr; // the shot: only what changes it
        else if (cfg->pierce) label = "Pierce", icon = "Icons_AttackPierce";
        else if (cfg->wind) label = "Wind", icon = "Icons_Attack_Wind";
        else if (cfg->attackBehaviour == AttackBehaviour::Charge) label = "Charge", icon = "Icons_AttackCharge";
        else if (cfg->spawner()) label = "Spawn", icon = "Icons_AttackSpawn";
    }
    c.SetActive(special, label != nullptr);
    if (label) {
        c.SetText(c.Find("Text_S (SpecialInfo)", special), T(std::string("Gameplay/Attack_") + label, label));
        c.SetSprite(c.Find("Icon", special), icon);
        c.SetActive(c.Find("Text_M (Range)", special), cfg->rangeEnabled);
        c.SetText(c.Find("Text_M (Range)", special), std::to_string(cfg->range));
    }
}

bool App::DrawTeamInfoCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_Gameplay");
    if (root < 0 || !prefabUi_.loaded() || !st_.game || !run_) return false;
    Game& g = *st_.game;
    const Prefabs& pf = data_.prefabs;
    int base = c.Mark();
    std::vector<int> kids = c.node(root).kids;
    std::vector<bool> was;
    for (int k : kids) was.push_back(c.node(k).active), c.SetActive(k, false);
    int pages = c.Find("MenuPages", root);
    c.SetActive(pages, true);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage_Help");
    int page = c.Find("MenuPage_Help", pages);

    int buttons = c.Find("PageArea/Buttons", page);
    c.SetActive(c.Find("Button_Simple (Exit)/SelectedOutline", buttons), sel_ == 0);
    c.SetActive(c.Find("Button_ChangePage (HowToPlay)/SelectedOutline", buttons), sel_ == 1);
    c.SetText(c.Find("Button_ChangePage (HowToPlay)/AnimRoot/Label", buttons), T("FrontEnd/HowToPlay", "How to Play"));

    // TeamInfo
    int info = c.Find("PageArea/InfoPanels/TeamInfo", page);
    const TeamConfig* team = run_->team;
    c.SetText(c.Find("InfoPopup_Header/Text_M (Header)", info), ui::Format(T("FrontEnd/Team_TeamName", "Team {teamname}"), {{"teamname", TeamName(meta_.teamIndex)}}));
    c.SetText(c.Find("Description/Text_S (Description)", info), team->description);
    auto trait = [&](const char* path, const char* text, const TeamTraitConfig* tr, uint32_t k) {
        int n = c.Find(path, c.Find("TeamTraits", info));
        c.SetActive(n, tr && !tr->description.empty());
        if (tr) {
            c.SetText(c.Find(text, n), ui::Format(tr->description, {{"value", ui::Int(tr->valueInt)}}));
            c.SetTextColor(c.Find(text, n), k);
        }
    };
    trait("Positive", "Text_M (PositiveTrait)", team->positiveTrait, col::Green);
    trait("Negative", "Text_M (NegativeTrait)", team->negativeTrait, col::Red);

    int popups = c.Find("PopupsParent", info), prefab = prefabUi_.Find("PlayerStatsPopup");
    for (Player* p : g.players) {
        if (!p->isAlive) continue;
        const HumanPlayer& hp = *p->humanPlayer;
        int r = c.Instantiate(prefabUi_, prefab, popups);
        int header = c.Find("Borders/Header", r), body = c.Find("Borders/Body", r);
        c.SetText(c.Find("Text_M (Header)", header), hp.playerName);
        c.SetText(c.Find("Text_S - Stamina", header), T("Gameplay/Stamina", "Stamina") + " " + Coloured(std::to_string(p->stamina) + "/" + std::to_string(p->maxStamina()), col::White));
        c.SetText(c.Find("Text_S - AP", header), T("Gameplay/ActionPointsShort", "AP") + " " + Coloured(std::to_string(p->actionPoints) + "/" + std::to_string(p->maxAP()), col::White));
        c.SetText(c.Find("Text_S (L)", body), T("Gameplay/StaminaCosts", "STAMINA COSTS"));
        int costs[5] = {g.StaminaCost(pf.configStep, p), g.StaminaCost(pf.configAttack, p, 1), g.StaminaCost(pf.configThrow, p), g.StaminaCost(pf.configPass, p),
                        g.StaminaCost(pf.configTake, p)};
        const char* keys[6] = {"Gameplay/Action_Move", "Gameplay/Action_Attack", "Gameplay/Action_Shoot", "Gameplay/Action_Pass", "Gameplay/Action_Take", "Gameplay/Action_Rest"};
        const char* fbs[6] = {"Step", "Attack", "Shoot", "Pass", "Take", "Rest"};
        std::string labels, stats = "\n<color=" + Hex(col::Orange) + ">";
        for (int i = 0; i < 6; i++) {
            int cost = i < 5 ? costs[i] : 0;
            labels += "\n" + Coloured(T(keys[i], fbs[i]), p->stamina >= cost ? col::White : col::Red);
        }
        bool charge = p->cAttack && p->cAttack->config->attackBehaviour == AttackBehaviour::Charge;
        stats += std::to_string(costs[0]) + "\n" + std::to_string(costs[1]) + (charge ? "+" : "") + "\n" + std::to_string(costs[2]) + "\n" +
                 std::to_string(costs[3]) + "\n" + std::to_string(costs[4]) + "\n</color>" + Coloured("+" + std::to_string(hp.GetRestStaminaReplenishment()), kGreen);
        c.SetText(c.Find("Left/Text_S (L) StatsLabels", body), labels);
        c.SetText(c.Find("Left/Text_S (L) Stats", body), stats);
        int right = c.Find("Right", body);
        c.SetSprite(c.Find("Class/ClassIcon", right), std::string("Icons_Class_") + ToString(hp.playerClassConfig->playerClass));
        c.SetText(c.Find("Class/Text_S (Class)", right), T("General/Class", "CLASS") + "\n" + ClassName(hp.playerClassConfig));
        int perk = c.Find("Perk", right);
        c.SetText(c.Find("Text_S (Perk)", perk), T("General/Perk", "PERK") + "\n" + Coloured(PerkName(hp.perkChain), col::Yellow));
        c.SetActive(c.Find("Text_S (Perk Info)", perk), false);
        bool unlocked = hp.perkLevel > 0 && hp.perkChain;
        c.SetSprite(c.Find("PerkIcon", perk), unlocked ? hp.perkChain->icon : std::string("Perks_Locked"));
        c.SetActive(c.Find("PerkIcon/PerkLevel", perk), unlocked);
        if (unlocked) c.SetSprite(c.Find("PerkIcon/PerkLevel", perk), "Perks_Fill_3-" + std::to_string(std::clamp(hp.perkLevel, 1, 3)));
        const gfx::RuntimeSprite& face = Portrait(hp);
        c.SetRuntimeSprite(c.Find("Sprite", r), &face.tex, face.def);
        c.SetActive(c.Find("Sprite/BallSprite", r), p->isHoldingBall());
        if (g.modifierConfig()) c.SetSprite(c.Find("Sprite/BallSprite", r), g.modifierConfig()->ballSprite);
        // the attack and the shot
        int attacks = c.Find("Attacks", r);
        c.SetText(c.Find("Normal/Header/Text_S (Normal)", attacks), T("Gameplay/Action_Attack", "Attack"));
        c.SetText(c.Find("Shoot/Header/Text_S (Ball)", attacks), T("Gameplay/Action_Shoot", "Shoot"));
        if (p->cAttack)
            FillAttackTooltip(c, c.Find("Normal/AttackTooltip_TeamInfo", attacks), p->cAttack->config, p->cAttack->attackDamage(), p->cAttack->knockbackDistance(),
                              p->cAttack->stunValue(), false);
        FillAttackTooltip(c, c.Find("Shoot/AttackTooltip_TeamInfo_Shoot", attacks), pf.ballAttackConfig, g.ball ? g.ball->GetDamageTotal(p) : 0,
                          g.ball ? g.ball->GetKnockbackTotal(p) : 0, g.ball ? g.ball->GetStunDuration(p) : 0, true);
    }

    // BallInfoPopup
    int ball = c.Find("PageArea/InfoPanels/InfoPopup - Ball/Borders", page);
    const BallModifierConfig* bm = g.modifierConfig();
    c.SetActive(c.Find("PageArea/InfoPanels/InfoPopup - Ball", page), bm != nullptr);
    if (bm) {
        int b = c.Find("Body", ball);
        c.SetText(c.Find("Header/Text_M (Header)", ball), ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", bm->friendlyName}}));
        std::string desc = bm->description;
        if (desc.find('{') != std::string::npos) desc = ui::Format(desc, {{"value", ui::Int(bm->value)}});
        c.SetText(c.Find("Text_S - Ball Name", b), ui::Format(T("Gameplay/BallMod_FullName", "{value} Ball"), {{"value", bm->friendlyName}}));
        c.SetText(c.Find("Text_S - Ball Description", b), desc);
        c.SetSprite(c.Find("BallSprite", b), bm->ballSprite);
        c.SetText(c.Find("StaminaPenalty/Text_S - StaminaPenalty 2", b), g.ball ? std::to_string(g.ball->GetStaminaPenalty()) : "0");
        bool neutral = bm->difficultyEffect == DifficultyEffect::Neutral;
        c.SetActive(c.Find("Multiplier", b), !neutral);
        if (!neutral) {
            float mult = bm->difficultyEffect == DifficultyEffect::Easier
                             ? 1.0f - data_.session.ballMultiplierPerStageEasy * static_cast<float>(run_->easyBallModsUsed)
                             : 1.0f + data_.session.ballMultiplierPerStageHard * static_cast<float>(run_->hardBallModsUsed);
            int pct = static_cast<int>(std::lround(mult * 100.0f)) - 100;
            int t = c.Find("Multiplier/Text_S - ScoreMultiplier 2", b);
            c.SetText(t, (pct < 0 ? std::string() : std::string("+")) + std::to_string(pct) + "%");
            c.SetTextColor(t, pct < 0 ? col::Red : col::Green);
        }
        static const char* const diffKeys[3] = {"Gameplay/BallMod_Diff_Easy", "Gameplay/BallMod_Diff_Normal", "Gameplay/BallMod_Diff_Hard"};
        static const char* const diffFb[3] = {"Easy", "Normal", "Hard"};
        int d = bm->difficultyEffect == DifficultyEffect::Easier ? 0 : neutral ? 1 : 2;
        c.SetText(c.Find("Multiplier/Text_S - ScoreMultiplier 1", b), T("Help/Tip_TeamInfo_ScoreMultiplier", "Score Multiplier"));
        c.SetText(c.Find("StaminaPenalty/Text_S - StaminaPenalty 1", b), T("Gameplay/StaminaPenalty", "Stamina Penalty"));
        c.SetText(c.Find("Difficulty/Text_S - Difficulty (1)", b), T(diffKeys[d], diffFb[d]));
    }
    // ScoreDisplay
    c.SetText(c.Find("ScoreDisplay/Parent/Text_L (Number)", page), Commas(run_->GetStageScores()));

    c.Draw(ui_, root, loc_);
    c.Truncate(base);
    c.SetActive(page, false);
    for (size_t i = 0; i < kids.size(); i++) c.SetActive(kids[i], was[i]);
    return true;
}

void App::InputTeamInfo(Input in) {
    if (in == Input::Left || in == Input::Right || in == Input::Up || in == Input::Down) {
        sel_ ^= 1;
        sound_.PlayCue("MenuMove");
        return;
    }
    if (in == Input::Confirm && sel_ == 1) {
        sound_.PlayCue("MenuSubmit");
        OpenHowToPlay(Screen::TeamInfo);
        return;
    }
    if (in == Input::Confirm || in == Input::Help || in == Input::Menu || in == Input::Back || in == Input::Rest || in == Input::No) {
        sound_.PlayCue("MenuCancel");
        Go(Screen::Play);
    }
}

} // namespace dd::app
