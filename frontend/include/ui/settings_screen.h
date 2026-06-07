#pragma once
#include "controller_nav.h"
#include "theme_engine.h"
#include "onscreen_keyboard.h"
#include <SDL2/SDL.h>
#include <memory>
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
	bool shelfEnabled[3] = { true, true, true };

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

    // Turbo mode speed index (persistent speed boost, toggled with R1+R2):
    //   0 = 1.5×   1 = 2×   2 = 3×   3 = 4×   4 = 6×
    // Unlike fast-forward, turbo stays active until toggled off, and keeps
    // audio running at speed (no mute). Great for grinding or long cutscenes.
    int  turboSpeed         = 0;  // Default: 1.5×

    // Mute audio while turbo is active. Off by default — the SoundTouch
    // pitch-correction means turbo audio is tolerable at lower speeds.
    // Users grinding at 4× or 6× for long sessions will appreciate this.
    bool turboMuteAudio     = false;

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

    // ── Ambient Music ─────────────────────────────────────────────────────────
    // Background music player for the browser and hub screens.
    // musicFolder: path to folder containing MP3, OGG, WAV, or FLAC files.
    // musicVolume: 0–100 (mapped to SDL_mixer 0–128 internally).
    // musicEnabled: master toggle — when off the strip hides and playback stops.
    bool        musicEnabled = false;
    int         musicVolume  = 50;
    std::string musicFolder  = "music";   // Relative to HaackStation install dir

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

    // Master RA feature toggle.  When false: Trophy Hub is hidden from the
    // HaackStation Hub, the trophy row is collapsed in the Game Details panel,
    // and RA login/tracking is skipped entirely.  Default on.
    bool        raEnabled          = true;

    bool        raHardcore         = false;

    // Auto-capture a screenshot when an achievement unlocks.
    // Saved to media/trophy_screenshots/<game>/<id>_<title>_<timestamp>.png
    // The screenshot includes the unlock notification popup (the "receipt").
    bool        raAutoScreenshot   = false;
};

class SettingsScreen {
public:
    SettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                   ControllerNav* nav, HaackSettings* settings);
    ~SettingsScreen() = default;

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose()          const { return m_wantsClose; }
    bool wantsScrape()         const { return m_wantsScrape; }
    bool wantsRemap()          const { return m_wantsRemap; }
    bool wantsRaLogin()        const { return m_wantsRaLogin; }
    bool wantsConvertAll()     const { return m_wantsConvertAll; }
    bool wantsDownloadTools()  const { return m_wantsDownloadTools; }

    void clearScrape()              { m_wantsScrape        = false; }
    void clearRemap()               { m_wantsRemap         = false; }
    void clearRaLogin()             { m_wantsRaLogin       = false; }
    void clearConvertAll()          { m_wantsConvertAll    = false; }
    void clearDownloadTools()       { m_wantsDownloadTools = false; }

    // Called by app.cpp after the async RA login callback fires.
    // success=true  → show "Logged in as: X"
    // success=false → show "Login failed" and restore "Not logged in" label
    void notifyLoginResult(bool success);
    void onWindowResize(int w, int h);
    bool wantsQuit()  const { return m_wantsQuit; }
    void resetClose()       { m_wantsClose = false; m_wantsQuit = false; }

private:
    void buildTabs();
    void navigateAction(NavAction action, bool isRepeat = false);
    void activateCurrentItem();
    void renderTab(const SettingTab& tab);
    void renderItem(const SettingItem& item, int x, int y, int w, bool selected);

    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;
    HaackSettings* m_settings = nullptr;

    // On-screen keyboard (modal overlay, used by RA settings tab)
    std::unique_ptr<OnScreenKeyboard> m_osk;

    // Temporary staging for RA credentials entered via OSK
    // (copied to m_settings->raUser / raPassword only on Login action)
    std::string m_raUsernameStaging;
    std::string m_raPasswordStaging;

    // Temporary staging for ScreenScraper credentials entered via OSK
    // (copied to m_settings->ssUser / ssPassword only on Save action)
    std::string m_ssUsernameStaging;
    std::string m_ssPasswordStaging;

    std::vector<SettingTab> m_tabs;
    int  m_activeTab    = 0;
    int  m_activeItem   = 0;
    int  m_scrollOffset = 0;
    bool m_wantsClose           = false;
    bool m_wantsQuit            = false;
    bool m_wantsScrape          = false;
    bool m_wantsRemap           = false;
    bool m_wantsRaLogin         = false;
    bool m_wantsConvertAll      = false;   // NEW: batch CHD conversion requested
    bool m_wantsDownloadTools   = false;   // NEW: navigate to Download Tools screen
    bool m_raLoginPending       = false;
    bool m_editingChoice        = false;
	int  m_windowW      = 1280;
    int  m_windowH      = 720;

    static constexpr int TAB_BAR_H = 56;
    static constexpr int ITEM_H    = 52;
    static constexpr int PANEL_X   = 60;
    static constexpr int PANEL_Y   = 80 + TAB_BAR_H;
};
