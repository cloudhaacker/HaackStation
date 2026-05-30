#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
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
#include "rewind_manager.h"
#include "input_map.h"
#include "remap_screen.h"
#include "trophy_room.h"
#include "trophy_hub.h"
#include "omnisave_vault.h"
#include "omnisave_card_shelf.h"    // ← NEW
#include "memcard_manager.h"
#include "ui/haack_hub.h"
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
    HAACK_HUB,
    IN_GAME,
    SETTINGS,
    REMAPPING,
    TROPHY_ROOM,
    TROPHY_HUB,
    OMNISAVE_VAULT,
    OMNISAVE_CARD_SHELF,    // ← NEW: global save browser, launched from Hub
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
    void revertPerGameSettings();
    void saveRaToken(const std::string& token);
    void processInGameMenuActions();
    void takeScreenshot();
    void takeUIScreenshot();
    void snapshotCardHistory();
    void captureCardScreenshot();

    static constexpr int CARD_HISTORY_MAX = 20;

private:
    void init();
    void shutdown();
    void handleEvents();
    void update(float deltaMs);
    void render();
    void updateGameInput();
    void renderFastForwardIndicator();
    void renderRewindIndicator();
    void renderTurboIndicator();
    void renderTurboDiagnostic();
    void renderScreenshotNotification();
    void renderMemcardToast();
    void renderTrophyShotToast();

    static std::string stripRomRegion(const std::string& stem);
    static std::vector<std::string> parseM3uDiscs(const std::string& path);

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = false;
    AppState      m_state     = AppState::STARTUP;
    AppState      m_prevState = AppState::GAME_BROWSER;

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
    DiscMemory                         m_discMemory;
    PerGameSettings                    m_perGameSettings;
    FavoriteManager                    m_favorites;
    std::unique_ptr<RewindManager>    m_rewind;
    std::unique_ptr<RemapScreen>      m_remapScreen;
    std::unique_ptr<TrophyRoom>       m_trophyRoom;
    std::unique_ptr<TrophyHub>        m_trophyHub;
    std::unique_ptr<HaackHub>         m_haackHub;
    std::unique_ptr<OmniSaveVault>    m_omniSave;
    std::unique_ptr<OmniSaveCardShelf> m_cardShelf;   // ← NEW
    std::unique_ptr<MemCardManager>   m_memCards;
    std::string m_currentGameTitle;
    std::string m_currentGameSerial;
    InputMap                           m_inputMap;

    Uint32 m_inputCooldownUntil = 0;

    std::string m_activeCardPath;
    Uint32      m_memcardFlushTimer = 0;

    uint32_t m_sramChecksum      = 0;
    Uint32   m_sramPollTimer     = 0;
    Uint32   m_memcardToastUntil = 0;
    static constexpr Uint32 SRAM_POLL_INTERVAL_MS = 2000;

    Uint32 m_trophyShotToastUntil = 0;
    bool   m_cardSwapToast        = false;
    bool   m_suppressSramPoll    = false;
    Uint32 m_sramSettleUntil     = 0;

    std::shared_ptr<std::atomic<int>> m_pendingLoginResult;
    static constexpr Uint32 MEMCARD_FLUSH_INTERVAL_MS = 30000;

    time_t m_sessionStartTime = 0;

    bool   m_fastForward  = false;
    Uint32 m_ffHeldSince  = 0;
    static constexpr Uint32 FF_HOLD_DELAY_MS = 500;

    bool   m_rewinding       = false;
    Uint32 m_rewindHeldSince = 0;
    static constexpr Uint32 REWIND_HOLD_DELAY_MS = 300;

    Uint32 m_rumbleNextAt    = 0;
    static constexpr Uint32 RUMBLE_PULSE_INTERVAL_MS = 600;

    std::string m_currentGamePath;
    Uint32      m_screenshotNotifyUntil = 0;

    bool   m_turboActive      = false;
    Uint32 m_turboHeldSince   = 0;
    static constexpr Uint32 TURBO_HOLD_DELAY_MS = 600;
    static constexpr int    TURBO_TABLE_SIZE    = 5;

    HaackSettings m_haackSettings;
};
