#pragma once
#include <SDL2/SDL.h>
#include <memory>
#include <string>

class GameBrowser;
class ControllerNav;
class LibretroBridge;
class GameScanner;
class ThemeEngine;
class SettingsScreen;
class SplashScreen;
struct HaackSettings;

enum class AppState {
    STARTUP,       // Splash screen
    GAME_BROWSER,  // Main game shelf
    IN_GAME,       // Running a game
    SETTINGS,      // Settings screen
    SHUTDOWN
};

// Forward-declared here, defined in settings_screen.h
// Included by app.cpp where needed
struct HaackSettings;

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

private:
    void init();
    void shutdown();
    void handleEvents();
    void update(float deltaMs);
    void render();
    void updateGameInput();   // Translates SDL controller to libretro bitmask

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = false;
    AppState      m_state    = AppState::STARTUP;

    std::unique_ptr<ThemeEngine>     m_theme;
    std::unique_ptr<ControllerNav>   m_nav;
    std::unique_ptr<GameScanner>     m_scanner;
    std::unique_ptr<LibretroBridge>  m_core;
    std::unique_ptr<GameBrowser>     m_browser;
    std::unique_ptr<SettingsScreen>  m_settings;
    std::unique_ptr<SplashScreen>    m_splash;

    // Settings values (owned here, passed by pointer to SettingsScreen)
    // We include the full struct inline to avoid a forward-declare headache
    struct HaackSettingsLocal {
        std::string romsPath;
        std::string biosPath;
        bool fullscreen     = false;
        bool vsync          = true;
        bool showFps        = false;
        int  rendererChoice = 0;
        int  internalRes    = 1;
        int  shaderChoice   = 0;
        int  audioVolume    = 100;
        bool audioReplacement = true;
        bool textureReplacement = true;
        bool aiUpscaling    = false;
        int  aiUpscaleScale = 2;
    } m_haackSettings;
};
