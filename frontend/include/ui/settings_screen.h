#pragma once
// settings_screen.h
// The settings UI — fully navigable by controller.
//
// Settings are grouped into tabs:
//   General   — ROMs paths, BIOS path, language
//   Video     — Renderer, resolution, shader pack
//   Audio     — Volume, audio replacement enable/disable
//   Textures  — Texture pack, AI upscaling toggle
//   Controls  — Button remapping (Phase 3)
//   About     — Credits, version, acknowledgements

#include "ui/controller_nav.h"
#include "renderer/theme_engine.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>

// ─── Setting item types ───────────────────────────────────────────────────────
enum class SettingType {
    TOGGLE,     // On / Off
    CHOICE,     // Pick from a list of strings
    PATH,       // File/folder path (opens path picker)
    SLIDER,     // Integer range
    ACTION,     // Triggers a callback (e.g. "Rescan library")
    SEPARATOR,  // Visual divider, not interactive
    LABEL,      // Read-only info line
};

struct SettingItem {
    std::string       id;
    std::string       label;
    std::string       description;
    SettingType       type;

    // TOGGLE
    bool*             toggleValue = nullptr;

    // CHOICE
    std::vector<std::string> choices;
    int*              choiceIndex = nullptr;

    // SLIDER
    int*              sliderValue = nullptr;
    int               sliderMin   = 0;
    int               sliderMax   = 100;

    // PATH
    std::string*      pathValue   = nullptr;

    // ACTION
    std::function<void()> action;

    // LABEL
    std::string       labelValue;
};

struct SettingTab {
    std::string              label;
    std::vector<SettingItem> items;
};

// ─── HaackSettings ────────────────────────────────────────────────────────────
// The actual settings values, owned here and passed to subsystems
struct HaackSettings {
    // General
    std::string romsPath       = "";
    std::string biosPath       = "";

    // Video
    int  rendererChoice        = 0;    // 0=OpenGL, 1=Vulkan
    int  internalRes           = 1;    // 0=1x, 1=2x, 2=4x, 3=8x
    int  shaderChoice          = 0;    // index into shader list
    bool vsync                 = true;
    bool fullscreen            = false;
    bool showFps               = false;

    // Audio
    int  audioVolume           = 100;
    bool audioReplacement      = true;
    bool audioReplacementLog   = false;

    // Textures
    bool textureReplacement    = true;
    bool aiUpscaling           = false;
    int  aiUpscaleScale        = 2;    // 2x or 4x

    // System
    bool overclock             = false;
    bool cdFastBoot            = false;
    bool saveStates            = true;
};

// ─── SettingsScreen ──────────────────────────────────────────────────────────
class SettingsScreen {
public:
    SettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                   ControllerNav* nav, HaackSettings* settings);
    ~SettingsScreen() = default;

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose() const { return m_wantsClose; }

private:
    void buildTabs();
    void navigateAction(NavAction action);
    void activateCurrentItem();
    void renderTab(const SettingTab& tab);
    void renderItem(const SettingItem& item, int x, int y, int w, bool selected);

    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;
    HaackSettings* m_settings = nullptr;

    std::vector<SettingTab> m_tabs;
    int m_activeTab   = 0;
    int m_activeItem  = 0;
    int m_scrollOffset= 0;

    bool m_wantsClose = false;

    int m_windowW = 1280;
    int m_windowH = 720;

    static constexpr int TAB_BAR_H  = 56;
    static constexpr int ITEM_H     = 52;
    static constexpr int PANEL_X    = 60;
    static constexpr int PANEL_Y    = 80 + TAB_BAR_H;
};
