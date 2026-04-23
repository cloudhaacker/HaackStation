#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include "settings_screen.h"
#include "scrape_screen.h"
#include "game_scraper.h"
#include "save_state_manager.h"
#include "ingame_menu.h"
#include "ra_manager.h"
#include "game_details_panel.h"
#include "play_history.h"
#include "per_game_settings.h"
#include "per_game_settings_screen.h"
#include "disc_memory.h"
#include "favorites.h"
#include "rewind_manager.h"   // ← NEW
#include "input_map.h"
#include "remap_screen.h"
#include "trophy_room.h"
#include <ctime>

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
    REMAPPING,
    TROPHY_ROOM,
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
    void applyPerGameSettings(const std::string& gamePath, const std::string& serial);
    void revertPerGameSettings(); // restore global settings after game exits
    void saveRaToken(const std::string& token);
    void processInGameMenuActions();
    void takeScreenshot();          // Save game framebuffer to screenshots/
    void takeUIScreenshot();        // F10 — capture any screen to screenshots/HaackStation/

private:
    void init();
    void shutdown();
    void handleEvents();
    void update(float deltaMs);
    void render();
    void updateGameInput();
    void renderFastForwardIndicator();
    void renderRewindIndicator();
    void renderTurboIndicator();                     // ← NEW
    void renderTurboDiagnostic();                    // TEMP: remove once turbo confirmed working
    void renderScreenshotNotification();

    // ── Rewind helpers ────────────────────────────────────────────────────────
    // stripRomRegion() removes parenthetical / bracketed region & revision tags
    // from a ROM filename stem so screenshot folders match ScreenScraper names.
    // e.g. "Crash Bandicoot (USA)" → "Crash Bandicoot"
    static std::string stripRomRegion(const std::string& stem);
    static std::vector<std::string> parseM3uDiscs(const std::string& path);

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
    std::unique_ptr<GameDetailsPanel>        m_details;
    std::unique_ptr<PerGameSettingsScreen>   m_perGameScreen;
    std::unique_ptr<PlayHistory>      m_playHistory;
    DiscMemory                         m_discMemory;  // persists last disc per game
    PerGameSettings                    m_perGameSettings; // per-game overrides
    FavoriteManager                    m_favorites;   // persists favorited games
    std::unique_ptr<RewindManager>    m_rewind;      // ← NEW
    std::unique_ptr<RemapScreen>      m_remapScreen;
    std::unique_ptr<TrophyRoom>       m_trophyRoom;
    InputMap                           m_inputMap;    // global button map, loaded from saves/input_map.json

    Uint32 m_inputCooldownUntil = 0;

    // Session playtime tracking — set when a game launches, used on stopGame()
    // to compute elapsed seconds and pass to PlayHistory::recordStop().
    time_t m_sessionStartTime = 0;

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

    // ── Turbo mode ────────────────────────────────────────────────────────────
    // Persistent speed multiplier — toggled ON/OFF by holding R1+R2 (or T key)
    // for TURBO_HOLD_DELAY_MS. Unlike fast-forward it stays active until toggled
    // again. No rumble while active. Green badge indicator top-right.
    bool   m_turboActive      = false;
    Uint32 m_turboHeldSince   = 0;
    static constexpr Uint32 TURBO_HOLD_DELAY_MS = 600;
    static constexpr int    TURBO_TABLE_SIZE    = 5;
    // NOTE: Multiplier table lives in app.cpp as TURBO_MULTS[] to avoid
    // MSVC ODR issues with static constexpr arrays in class definitions.

    HaackSettings m_haackSettings;
};
