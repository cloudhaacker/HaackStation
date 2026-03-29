#pragma once
#include "controller_nav.h"
#include "theme_engine.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>

enum class SettingType {
    TOGGLE, CHOICE, PATH, SLIDER, ACTION, SEPARATOR, LABEL
};

struct SettingItem {
    std::string  id;
    std::string  label;
    std::string  description;
    SettingType  type        = SettingType::LABEL;
    bool*        toggleValue = nullptr;
    std::vector<std::string> choices;
    int*         choiceIndex = nullptr;
    int*         sliderValue = nullptr;
    int          sliderMin   = 0;
    int          sliderMax   = 100;
    std::string* pathValue   = nullptr;
    std::function<void()> action;
    std::string  labelValue;
};

struct SettingTab {
    std::string              label;
    std::vector<SettingItem> items;
};

struct HaackSettings {
    // ── Paths ─────────────────────────────────────────────────────────────────
    std::string romsPath;
    std::string biosPath;

    // ── General ───────────────────────────────────────────────────────────────
    bool fullscreen         = false;
    bool vsync              = true;
    bool showFps            = false;

    // ── Emulation ─────────────────────────────────────────────────────────────
    // Fast Boot: skips the PS1 BIOS logo/animation on startup.
    // Can cause compatibility issues with a small number of games
    // (e.g. Saga Frontier, some PAL titles). Disable per-game if needed.
    bool fastBoot           = false;

    // Fast Forward speed multiplier index:
    //   0 = 2×   1 = 4×   2 = 6×   3 = 8×
    // Applied when R2 (or F key) is held. Higher = faster but may cause
    // audio glitches or missed frames on slower machines.
    int  fastForwardSpeed   = 1;  // Default: 4×

    // Top row mode — what shows above the main game shelf:
    //   0 = Recently Played   1 = Favorites   2 = None
    int  topRowMode         = 0;  // Default: Recently Played

    // ── Video ─────────────────────────────────────────────────────────────────
    int  rendererChoice     = 0;
    int  internalRes        = 1;
    int  shaderChoice       = 0;

    // ── Audio ─────────────────────────────────────────────────────────────────
    int  audioVolume        = 100;
    bool audioReplacement   = true;
    bool audioReplacementLog= false;

    // ── Textures ──────────────────────────────────────────────────────────────
    bool textureReplacement = true;
    bool aiUpscaling        = false;
    int  aiUpscaleScale     = 0;

    // ── ScreenScraper credentials ─────────────────────────────────────────────
    std::string ssUser;
    std::string ssPassword;
    std::string ssDevId;
    std::string ssDevPassword;

    // ── RetroAchievements ─────────────────────────────────────────────────────
    std::string raUser;
    std::string raApiKey;      // Session token (stored after login)
    std::string raPassword;    // Used for initial login only
    bool        raHardcore = false;
};

class SettingsScreen {
public:
    SettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                   ControllerNav* nav, HaackSettings* settings);
    ~SettingsScreen() = default;

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose()  const { return m_wantsClose; }
    bool wantsScrape() const { return m_wantsScrape; }
    void clearScrape()       { m_wantsScrape = false; }
    void onWindowResize(int w, int h);
    bool wantsQuit()  const { return m_wantsQuit; }
    void resetClose()       { m_wantsClose = false; m_wantsQuit = false; }

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
    int  m_activeTab    = 0;
    int  m_activeItem   = 0;
    int  m_scrollOffset = 0;
    bool m_wantsClose   = false;
    bool m_wantsQuit    = false;
    bool m_wantsScrape  = false;
    int  m_windowW      = 1280;
    int  m_windowH      = 720;

    static constexpr int TAB_BAR_H = 56;
    static constexpr int ITEM_H    = 52;
    static constexpr int PANEL_X   = 60;
    static constexpr int PANEL_Y   = 80 + TAB_BAR_H;
};
