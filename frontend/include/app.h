#pragma once

#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <functional>

// ─── Forward declarations ──────────────────────────────────────────────────────
class GameBrowser;
class MenuSystem;
class ControllerNav;
class LibretroBridge;
class GameScanner;
class ThemeEngine;
class AudioReplacer;
class TextureReplacer;
class ShaderManager;
class SplashScreen;

// ─── App states ───────────────────────────────────────────────────────────────
enum class AppState {
    STARTUP,        // Splash / loading
    GAME_BROWSER,   // Main shelf — picking a game
    IN_GAME,        // Running a game
    SETTINGS,       // Settings screen
    ABOUT,          // Credits / about
    SHUTDOWN        // Clean exit
};

// ─── HaackApp ─────────────────────────────────────────────────────────────────
// Central application class. Owns all subsystems, drives the main loop.
class HaackApp {
public:
    HaackApp();
    ~HaackApp();

    // Returns exit code
    int run();

    // Called by subsystems to trigger a state change
    void setState(AppState next);
    AppState getState() const { return m_state; }

    // Launch a game by file path (ISO, BIN/CUE, CHD, M3U)
    void launchGame(const std::string& path);
    void stopGame();

private:
    void init();
    void shutdown();
    void mainLoop();

    void handleEvents();
    void update(float deltaMs);
    void render();

    // SDL
    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = false;

    // App state
    AppState m_state = AppState::STARTUP;

    // Subsystems (owned)
    std::unique_ptr<GameBrowser>    m_browser;
    std::unique_ptr<MenuSystem>     m_menu;
    std::unique_ptr<ControllerNav>  m_nav;
    std::unique_ptr<LibretroBridge> m_core;
    std::unique_ptr<GameScanner>    m_scanner;
    std::unique_ptr<ThemeEngine>    m_theme;
    std::unique_ptr<AudioReplacer>  m_audio;
    std::unique_ptr<TextureReplacer>m_textures;
    std::unique_ptr<ShaderManager>  m_shaders;
    std::unique_ptr<SplashScreen>   m_splash;
};
