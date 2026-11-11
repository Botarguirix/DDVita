// The Options pages (sharedassets prefabs MenuPage_Options_Main / _Sub_Gameplay / _Sub_Sound /
// _Sub_Graphics, spawned by mooChangeMenuPageDeathball_SpawnPage): VolumeSlider and SettingToggle
// rows. The Controls page (key bindings) is the PC's and is not offered.
#include <algorithm>

#include "app/app.h"

namespace dd::app {

namespace {
void Outline(ui::Canvas& c, int row, bool on) {
    if (row >= 0) c.SetActive(c.FindDeep("SelectedOutline", row), on);
}
} // namespace

// The SettingToggle rows of the Gameplay (1) and Graphics (3) pages, in the pages' order. Graphics on
// the Vita: Pixel Filter (the 3D at the original's 640x360), Smooth Fonts and, in the VSync row (the
// Vita has no such setting), the port's Health Bars.
std::vector<App::ToggleRow> App::ToggleRows() {
    if (optionsPage_ == 1)
        return {{"SettingToggle (FastMode)", "General/Setting_FastMode", "Fast Mode", &settings_.fastMode},
                {"SettingToggle (DynamicCameras)", "General/Setting_DynamicCamera", "Dynamic Camera", &settings_.dynamicCamera},
                {"SettingToggle (AutoSkipCinematics)", "General/Setting_AutoSkipCinematics", "Auto Skip Cinematics", &settings_.autoSkipCinematics},
                {"SettingToggle (GreyBlood)", "General/Setting_GreyBlood", "Grey Blood", &settings_.greyBlood},
                {"SettingToggle (SuggestUndo)", "General/Setting_SuggestUndo", "Undo Suggestions", &settings_.suggestUndo},
                {"SettingToggle (ActionPopups)", "General/Setting_Popups_Actions", "Action Popups", &settings_.actionPopups},
                {"SettingToggle (CommandCostPopups)", "General/Setting_Popups_CommandCosts", "Command Cost Popups", &settings_.commandCostPopups},
                {"SettingToggle (MenuPopups)", "General/Setting_Popups_Menu", "Menu Popups", &settings_.menuPopups},
                {"SettingToggle (EnemyPopups)", "General/Setting_Popups_Enemy", "Enemy Popups", &settings_.enemyPopups},
                {"SettingToggle (TilePopups)", "General/Setting_Popups_Tile", "Tile Popups", &settings_.tilePopups}};
    if (optionsPage_ == 3)
        return {{"SettingToggle (PixelFilter)", "General/Setting_PixelFilter", "Pixel Filter", &settings_.pixelFilter},
                {"SettingToggle (SmoothFonts)", "General/Setting_SmoothFonts", "Smooth Fonts", &settings_.smoothFonts},
                {"SettingToggle (Vsync)", "", "Health Bars", &settings_.healthBars}};
    return {};
}

// rows of the options page showing: 0 main (Gameplay, Graphics, Sound, Restore Defaults), 1 gameplay
// and 3 graphics (their toggles), 2 sound (six volumes)
int App::OptionRows() {
    return optionsPage_ == 0 ? 4 : optionsPage_ == 2 ? 6 : static_cast<int>(ToggleRows().size());
}

bool App::DrawOptionsCanvas() {
    ui::Canvas& c = optionsUi_;
    if (!c.loaded()) return false;
    const char* names[4] = {"MenuPage_Options_Main", "MenuPage_Options_Sub_Gameplay", "MenuPage_Options_Sub_Sound", "MenuPage_Options_Sub_Graphics"};
    int page = c.Find(names[optionsPage_]);
    if (page < 0) return false;
    c.SetActive(page, true);
    Outline(c, c.FindDeep("Button_Simple_Back", page), false);
    if (optionsPage_ == 0) {
        int body = c.Find("PageArea/Body", page), pages = c.Find("Pages", body);
        Outline(c, c.Find("Button_ChangePage_Spawn (Gameplay)", pages), sel_ == 0);
        Outline(c, c.Find("Button_ChangePage_Spawn (Graphics)", pages), sel_ == 1);
        Outline(c, c.Find("Button_ChangePage_Spawn (Sound)", pages), sel_ == 2);
        c.SetActive(c.Find("Button_ChangePage_Spawn (Graphics)", pages), true);
        c.SetActive(c.Find("Button_ChangePage_Spawn (Controls)", pages), false);
        Outline(c, c.Find("DefaultsButton/Button_RestoreDefaultSettings", body), sel_ == 3);
    } else if (optionsPage_ == 1 || optionsPage_ == 3) {
        int list = c.Find("PageArea/Body/Contents", page);
        std::vector<ToggleRow> shown = ToggleRows();
        for (int k : c.node(list).kids) {
            const std::string& n = c.node(k).name;
            bool keep = optionsPage_ == 1 && n.rfind("OptionsSubHeader", 0) == 0; // "General" / "Interface"
            for (size_t r = 0; r < shown.size(); r++)
                if (n == shown[r].node) {
                    keep = true;
                    c.SetText(c.Find("Text_M", k), *shown[r].key ? T(shown[r].key, shown[r].fb) : std::string(shown[r].fb));
                    c.SetSprite(c.Find("Toggle/Background/Icon", k), *shown[r].value ? "Icons_Toggle_On" : "Icons_Toggle_Off");
                    Outline(c, k, sel_ == static_cast<int>(r));
                }
            c.SetActive(k, keep);
        }
    } else {
        int list = c.Find("PageArea/Body/Contents", page);
        const char* rows[6] = {"VolumeSlider (Master Volume)", "VolumeSlider (Music Volume)", "VolumeSlider (Jingle Volume)",
                               "VolumeSlider (SFX Volume)",    "VolumeSlider (Crowd Volume)", "VolumeSlider (Voices Volume)"};
        const char* keys[6] = {"General/Setting_Volume_Master", "General/Setting_Volume_Music", "General/Setting_Volume_Jingles",
                               "General/Setting_Volume_Sounds", "General/Setting_Volume_Crowd", "General/Setting_Volume_Voices"};
        const char* fb[6] = {"Master", "Music", "Jingles", "Sound FX", "Crowd", "Voices"};
        for (int r = 0; r < 6; r++) {
            int row = c.Find(rows[r], list);
            if (row < 0) continue;
            float v = *Volume(r);
            c.SetText(c.Find("Text_M", row), T(keys[r], fb[r]));
            // Slider: the fill and the handle follow the value through their anchors
            int fill = c.Find("Slider/Fill Area/Fill", row), handle = c.Find("Slider/Handle Slide Area/Handle", row);
            if (fill >= 0) { // MaximiseRectOnStart, then Slider.UpdateVisuals
                ui::CNode& f = c.node(fill);
                f.amin[0] = f.amin[1] = 0, f.amax[0] = v, f.amax[1] = 1;
                f.pos[0] = f.pos[1] = 0, f.size[0] = f.size[1] = 0;
            }
            if (handle >= 0) {
                ui::CNode& h = c.node(handle);
                h.amin[0] = h.amax[0] = v, h.amin[1] = 0, h.amax[1] = 1;
                h.size[1] = 8; // taller than the 8 px bar
            }
            Outline(c, row, sel_ == r);
        }
    }
    c.Draw(ui_, page, loc_);
    c.SetActive(page, false);
    return true;
}

} // namespace dd::app
