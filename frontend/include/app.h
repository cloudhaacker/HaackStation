#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include "settings_screen.h"
#include "scrape_screen.h"
#include "game_scraper.h"
#include "save_state_manager.h"
#include "ingame_menu.h"
#include "ra_manager.h"
#include "game_details_panel.h"
#include "play_history.h"
#include "rewind_manager.h"   // ← NEW

class GameBrowser;
class ControllerNav;
class LibretroBridge;
class GameScanner;
class ThemeEngine;
class SettingsScreen;
class SplashScreen;

enum class AppState {
    STARTUP,
    GAME_BROWSER,
    IN_GAME,
    SETTINGS,
    SCRAPING,
    SHUTDOWN
};

class HaackApp {
public:
    HaackApp();
    ~HaackApp();
    int run();

    void setState(AppState next);
    AppState getState() const { return m_state; }
    void launchGame(const std::string& path);
    void stopGame();
    void toggleFullscreen();
    void applySettings();
    void saveRaToken(const std::string& token);
    void processInGameMenuActions();
    void takeScreenshot();          // Save framebuffer to screenshots/ folder

private:
    void init();
    void shutdown();
    void handleEvents();
    void update(float deltaMs);
    void render();
    void updateGameInput();
    void renderFastForwardIndicator();
    void renderRewindIndicator();                    // ← NEW
    void renderScreenshotNotification(); // Brief toast after screenshot capture

    // ── Rewind helpers ────────────────────────────────────────────────────────
    // stripRomRegion() removes parenthetical / bracketed region & revision tags
    // from a ROM filename stem so screenshot folders match ScreenScraper names.
    // e.g. "Crash Bandicoot (USA)" → "Crash Bandicoot"
    static std::string stripRomRegion(const std::string& stem);

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = false;
    AppState      m_state    = AppState::STARTUP;

    std::unique_ptr<ThemeEngine>      m_theme;
    std::unique_ptr<ControllerNav>    m_nav;
    std::unique_ptr<GameScanner>      m_scanner;
    std::unique_ptr<LibretroBridge>   m_core;
    std::unique_ptr<GameBrowser>      m_browser;
    std::unique_ptr<SettingsScreen>   m_settings;
    std::unique_ptr<SplashScreen>     m_splash;
    std::unique_ptr<ScrapeScreen>     m_scraper;
    std::unique_ptr<SaveStateManager> m_saveStates;
    std::unique_ptr<InGameMenu>       m_inGameMenu;
    std::unique_ptr<RAManager>        m_ra;
    std::unique_ptr<GameDetailsPanel> m_details;
    std::unique_ptr<PlayHistory>      m_playHistory;
    std::unique_ptr<RewindManager>    m_rewind;      // ← NEW

    Uint32 m_inputCooldownUntil = 0;

    // Fast forward: true while R2 (controller) or F (keyboard) is held.
    // m_ffHeldSince tracks when the button was first pressed so we can
    // require a short hold before activating (prevents accidental triggers).
    bool   m_fastForward  = false;
    Uint32 m_ffHeldSince  = 0;
    static constexpr Uint32 FF_HOLD_DELAY_MS = 500;

    // Rewind: true while L2 (controller) or R (keyboard) is held.
    // Same hold-delay pattern as fast-forward to prevent accidental triggers.
    // m_rewindFired tracks whether at least one stepBack succeeded this session
    // (used to decide whether to show the "empty buffer" indicator).
    bool   m_rewinding       = false;
    Uint32 m_rewindHeldSince = 0;
    static constexpr Uint32 REWIND_HOLD_DELAY_MS = 300;

    // Rumble pulse for FF / rewind: fire a short rumble every N ms while active.
    Uint32 m_rumbleNextAt    = 0;
    static constexpr Uint32 RUMBLE_PULSE_INTERVAL_MS = 600;

    std::string m_currentGamePath;           // Set on launch, used for screenshot naming
    Uint32      m_screenshotNotifyUntil = 0; // Show screenshot toast until this tick

    HaackSettings m_haackSettings;
};
