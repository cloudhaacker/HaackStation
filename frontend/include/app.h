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

private:
    void init();
    void shutdown();
    void handleEvents();
    void update(float deltaMs);
    void render();
    void updateGameInput();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = false;
    AppState      m_state    = AppState::STARTUP;

    std::unique_ptr<ThemeEngine>    m_theme;
    std::unique_ptr<ControllerNav>  m_nav;
    std::unique_ptr<GameScanner>    m_scanner;
    std::unique_ptr<LibretroBridge> m_core;
    std::unique_ptr<GameBrowser>    m_browser;
    std::unique_ptr<SettingsScreen> m_settings;
    std::unique_ptr<SplashScreen>   m_splash;
    std::unique_ptr<ScrapeScreen>      m_scraper;
    std::unique_ptr<SaveStateManager>  m_saveStates;
    std::unique_ptr<InGameMenu>        m_inGameMenu;
    std::unique_ptr<RAManager>         m_ra;
    std::unique_ptr<GameDetailsPanel>  m_details;
    Uint32 m_inputCooldownUntil = 0; // Block game input after save/load

    HaackSettings m_haackSettings;
};
