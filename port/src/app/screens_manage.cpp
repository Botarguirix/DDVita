// Canvas_StageEnd's MenuPage_2_TeamManagement filled in the way TeamManagementScreen,
// PlayerLevelUpPanel, PlayerStatUpgrader, PlayerStatDisplay and NewRecruitButton do. The cursor is
// the port's: sel2_ the column (the players, then the recruits), sel_ the row (the last: Continue).
#include <algorithm>
#include <cstdlib>

#include "app/app.h"

namespace dd::app {

using namespace dd::sim;
namespace col = ui::col;

namespace {
std::string WithCommas(int v) {
    std::string s = std::to_string(std::abs(v)), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return v < 0 ? "-" + out : out;
}
const uint32_t kOrangeBright = col::Rgb(236, 71, 0); // ColourManager.arneOrangeBright
} // namespace

bool App::DrawManageCanvas() {
    ui::Canvas& c = gameUi_;
    int root = c.Find("Canvas_StageEnd");
    if (root < 0 || !run_ || !prefabUi_.loaded()) return false;
    Run& run = *run_;
    int base = c.Mark();
    int pages = c.Find("MenuPages", root);
    for (int k : c.node(pages).kids) c.SetActive(k, c.node(k).name == "MenuPage_2_TeamManagement");
    c.SetActive(c.Find("StageEndFlash", root), false);
    c.SetActive(c.Find("SkullKeyUnlocks", root), false);
    int page = c.Find("MenuPage_2_TeamManagement", pages), area = c.Find("PageArea", page);
    for (int k : c.node(area).kids) // the mouse-over popups (NewRecruitInfoPopup, the upgrade info)
        if (c.node(k).name.rfind("InfoPopup", 0) == 0) c.SetActive(k, false);
    c.SetText(c.Find("GoldDisplay/Parent/Text_L (Number)", area), WithCommas(run.gold));

    int players = static_cast<int>(run.activePlayers.size());
    bool recruits = !run.teamIsAtMaximumCapacity() && !run.reservePlayers.empty();
    int rowsHere = sel2_ < players ? 3 : std::min<int>(3, static_cast<int>(run.reservePlayers.size()));
    bool onDone = sel_ >= rowsHere;

    int panels = c.Find("PlayerPanels/Panels", area);
    int nodePrefab = prefabUi_.Find("PlayerUpgradeNode");
    static const PlayerStat stats[3] = {PlayerStat::Stamina, PlayerStat::Strength, PlayerStat::Perk};
    static const char* const buttons[3] = {"TeamButton_UpgradeStat (Stamina)", "TeamButton_UpgradeStat (Strength)", "TeamButton_UpgradeStat (Perk)"};
    const uint32_t nodeColour[3] = {col::Green, kOrangeBright, col::Yellow}; // GetNodeColour
    for (int p = 0; p < 2; p++) {
        int pan = c.Find("PlayerLevelUpPanel (" + std::to_string(p + 1) + ")", panels);
        c.SetActive(pan, p < players);
        if (p >= players || pan < 0) continue;
        const HumanPlayer& hp = *run.activePlayers[static_cast<size_t>(p)];
        // PlayerLevelUpPanel.Initialise
        c.SetText(c.Find("PlayerInfo/Text_L", pan), hp.playerName);
        const gfx::RuntimeSprite& face = Portrait(hp);
        c.SetRuntimeSprite(c.Find("PlayerInfo/Portrait", pan), &face.tex, face.def);
        int cls = c.Find("CurrentStats/PlayerStat (Class)", pan);
        c.SetText(c.Find("Text_M", cls), ClassName(hp.playerClassConfig));
        c.SetSprite(c.Find("Icon", cls), std::string("Icons_Class_") + ToString(hp.playerClassConfig->playerClass));
        for (int s = 0; s < 3; s++) {
            int b = c.Find(std::string("Stats/") + buttons[s], pan);
            if (b < 0) continue;
            PlayerStat stat = stats[s];
            bool isPerk = stat == PlayerStat::Perk;
            int cap = hp.GetStatLevelCap(stat), level = hp.GetStatLevel(stat);
            bool max = level >= cap;
            int price = max ? 0 : hp.GetNextUpgradePrice(stat);
            bool tooExpensive = !max && run.gold < price;
            // GetLabel: the perk's name, else the stat's (an enum name in the original)
            int name = c.Find("Info/Text_M - StatName", b);
            c.SetText(name, isPerk ? PerkName(hp.perkChain) : stat == PlayerStat::Stamina ? "Stamina" : "Strength");
            c.SetTextColor(name, tooExpensive ? col::Red : c.node(name).baseTextColor);
            // PlayerStatDisplay
            int disp = c.Find("Info/PlayerStat", b);
            int value = c.Find("Text_M", disp);
            if (isPerk) {
                c.SetText(value, T("General/Perk", "Perk"));
                c.SetSprite(c.Find("Icon", disp), hp.perkChain ? hp.perkChain->icon : std::string("Perks_Locked"));
            } else {
                c.SetText(value, std::to_string(hp.GetStatValue(stat)));
                c.SetSprite(c.Find("Icon", disp), stat == PlayerStat::Stamina ? "PlayerInfoPanel_Stamina" : "PlayerInfoPanel_Strength");
            }
            c.SetTextColor(value, nodeColour[s]);
            // PriceDisplay (hidden at the level cap; red when it cannot be paid)
            int priceNode = c.Find("Price", b);
            c.SetActive(priceNode, !max);
            c.SetText(c.Find("Text_M - Price", priceNode), WithCommas(price));
            int priceBg = c.Find("SectionBG_White", priceNode);
            if (priceBg >= 0) c.SetImageColor(priceBg, tooExpensive ? col::Red : c.node(priceBg).baseImageColor);
            // the level nodes
            int nodes = c.Find("Nodes", b);
            for (int k = 0; k < cap && nodes >= 0; k++) {
                int n = c.Instantiate(prefabUi_, nodePrefab, nodes);
                c.SetSprite(n, k < level ? "UpgradeNode_Filled" : "UpgradeNode_Unfilled");
                c.SetImageColor(n, nodeColour[s]);
            }
            c.SetActive(c.Find("SelectedOutline", b), !onDone && sel2_ == p && sel_ == s);
        }
    }
    // BrowseRecruitsPanel: three NewRecruitButtons
    int rec = c.Find("BrowseRecruitsPanel", panels);
    c.SetActive(rec, recruits);
    if (recruits) {
        int list = c.Find("Players", rec);
        int price = data_.levelUp.newRecruitPrice;
        for (int i = 0; i < 3; i++) {
            int b = c.Find("TeamButton_RecruitPlayer (" + std::to_string(i) + ")", list);
            bool has = i < static_cast<int>(run.reservePlayers.size());
            c.SetActive(b, has);
            if (!has || b < 0) continue;
            const HumanPlayer& hp = *run.reservePlayers[static_cast<size_t>(i)];
            const gfx::RuntimeSprite& face = Portrait(hp);
            c.SetRuntimeSprite(c.Find("Portrait", b), &face.tex, face.def);
            c.SetText(c.Find("Info/Text_L - Name", b), hp.playerName);
            c.SetSprite(c.Find("Info/ClassIcon", b), std::string("Icons_Class_") + ToString(hp.playerClassConfig->playerClass));
            c.SetSprite(c.Find("Info/Perk/PerkIcon", b), hp.perkChain ? hp.perkChain->icon : std::string("Perks_Random"));
            c.SetText(c.Find("Info/Perk/Text_M - PerkName", b), PerkName(hp.perkChain));
            int pr = c.Find("Price", b);
            c.SetText(c.Find("Text_M - Price", pr), WithCommas(price));
            int prBg = c.Find("SectionBG_White", pr);
            if (prBg >= 0) c.SetImageColor(prBg, run.gold < price ? col::Red : c.node(prBg).baseImageColor);
            c.SetActive(c.Find("SelectedOutline", b), !onDone && sel2_ == players && sel_ == i);
        }
    }
    // Continue
    int cont = c.Find("Buttons/Continue/Button_StageEnd", area);
    c.SetText(c.Find("AnimRoot/Label", cont), T("General/MenuNav_Continue", "Continue"));
    c.SetActive(c.Find("SelectedOutline", cont), onDone);
    c.Draw(ui_, root, loc_);
    c.Truncate(base);
    return true;
}

} // namespace dd::app
