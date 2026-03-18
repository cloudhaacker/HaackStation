#include "app.h"
#include "ui/game_browser.h"
#include "ui/menu_system.h"
#include "ui/controller_nav.h"
#include "core_bridge/libretro_bridge.h"
#include "library/game_scanner.h"
#include "renderer/theme_engine.h"
#include <SDL2/SDL.h>
#include <stdexcept>
#include <iostream>

// ─── Window config ─────────────────────────────────────────────────────────────
static constexpr int  DEFAULT_W     = 1280;
static constexpr int  DEFAULT_H     = 720;
static constexpr auto WINDOW_TITLE  = "HaackStation";
static constexpr int  TARGET_FPS    = 60;
static constexpr float FRAME_MS     = 1000.f / TARGET_FPS;

HaackApp::HaackApp()  { init(); }
HaackApp::~HaackApp() { shutdown(); }

void HaackApp::init() {
    // SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    m_window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DEFAULT_W, DEFAULT_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!m_window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    m_renderer = SDL_CreateRenderer(
        m_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!m_renderer)
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());

    // Subsystems
    m_theme    = std::make_unique<ThemeEngine>(m_renderer);
    m_nav      = std::make_unique<ControllerNav>();
    m_scanner  = std::make_unique<GameScanner>();
    m_core     = std::make_unique<LibretroBridge>();
    m_browser  = std::make_unique<GameBrowser>(m_renderer, m_theme.get(), m_nav.get());
    m_menu     = std::make_unique<MenuSystem>(m_renderer, m_theme.get(), m_nav.get());

    // Scan for games on first launch
    m_scanner->scanDefaultPaths();
    m_browser->setLibrary(m_scanner->getLibrary());

    m_state   = AppState::GAME_BROWSER;
    m_running = true;

    std::cout << "[HaackStation] Initialized — SDL2 window " << DEFAULT_W << "x" << DEFAULT_H << "\n";
}

void HaackApp::shutdown() {
    m_menu.reset();
    m_browser.reset();
    m_core.reset();
    m_scanner.reset();
    m_nav.reset();
    m_theme.reset();

    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window)   { SDL_DestroyWindow(m_window);     m_window   = nullptr; }
    SDL_Quit();
}

int HaackApp::run() {
    mainLoop();
    return 0;
}

void HaackApp::mainLoop() {
    Uint32 lastTick = SDL_GetTicks();

    while (m_running) {
        Uint32 now   = SDL_GetTicks();
        float  delta = static_cast<float>(now - lastTick);
        lastTick     = now;

        handleEvents();
        update(delta);
        render();

        // Cap to ~60fps when not in game (core drives its own timing during gameplay)
        if (m_state != AppState::IN_GAME) {
            Uint32 elapsed = SDL_GetTicks() - now;
            if (elapsed < static_cast<Uint32>(FRAME_MS))
                SDL_Delay(static_cast<Uint32>(FRAME_MS) - elapsed);
        }
    }
}

void HaackApp::handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                m_running = false;
                break;
            case SDL_KEYDOWN:
                // ESC always goes back / opens menu
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    if (m_state == AppState::IN_GAME)
                        setState(AppState::GAME_BROWSER);
                    else if (m_state == AppState::SETTINGS)
                        setState(AppState::GAME_BROWSER);
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                m_nav->onControllerAdded(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                m_nav->onControllerRemoved(e.cdevice.which);
                break;
            default:
                break;
        }

        // Route input to the active screen
        switch (m_state) {
            case AppState::GAME_BROWSER:
                m_browser->handleEvent(e);
                break;
            case AppState::SETTINGS:
                m_menu->handleEvent(e);
                break;
            default:
                break;
        }
    }
}

void HaackApp::update(float deltaMs) {
    switch (m_state) {
        case AppState::GAME_BROWSER:
            m_browser->update(deltaMs);
            // If the browser says "launch this game", do it
            if (m_browser->hasPendingLaunch()) {
                launchGame(m_browser->consumeLaunchPath());
            }
            break;
        case AppState::IN_GAME:
            m_core->runFrame();
            break;
        case AppState::SETTINGS:
            m_menu->update(deltaMs);
            break;
        default:
            break;
    }
}

void HaackApp::render() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    switch (m_state) {
        case AppState::GAME_BROWSER:
            m_browser->render();
            break;
        case AppState::IN_GAME:
            // Core renders to its own framebuffer; we blit it here
            m_core->blitFramebuffer(m_renderer);
            break;
        case AppState::SETTINGS:
            m_menu->render();
            break;
        default:
            break;
    }

    SDL_RenderPresent(m_renderer);
}

void HaackApp::setState(AppState next) {
    std::cout << "[HaackStation] State: " << static_cast<int>(m_state)
              << " -> " << static_cast<int>(next) << "\n";
    m_state = next;
}

void HaackApp::launchGame(const std::string& path) {
    std::cout << "[HaackStation] Launching: " << path << "\n";
    if (m_core->loadGame(path)) {
        setState(AppState::IN_GAME);
    } else {
        std::cerr << "[HaackStation] Failed to load: " << path << "\n";
    }
}

void HaackApp::stopGame() {
    m_core->unloadGame();
    setState(AppState::GAME_BROWSER);
}
