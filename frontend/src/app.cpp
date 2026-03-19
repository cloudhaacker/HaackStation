#include "app.h"
#include "game_browser.h"
#include "controller_nav.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "libretro_bridge.h"
#include "libretro_types.h"
#include "game_scanner.h"
#include "theme_engine.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdexcept>
#include <iostream>

static constexpr int   DEFAULT_W = 1280;
static constexpr int   DEFAULT_H = 720;
static constexpr float FRAME_MS  = 1000.f / 60.f;

HaackApp::HaackApp()  { init(); }
HaackApp::~HaackApp() { shutdown(); }

void HaackApp::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    IMG_Init(IMG_INIT_PNG);

    m_window = SDL_CreateWindow(
        "HaackStation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DEFAULT_W, DEFAULT_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!m_window)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    // ── Set window icon ───────────────────────────────────────────────────────
    SDL_Surface* icon = IMG_Load("assets/icons/icon.png");
    if (!icon) icon = SDL_LoadBMP("assets/icons/icon.bmp");
    if (icon) {
        SDL_SetWindowIcon(m_window, icon);
        SDL_FreeSurface(icon);
    }

    m_renderer = SDL_CreateRenderer(m_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer)
        throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    m_theme    = std::make_unique<ThemeEngine>(m_renderer);
    m_nav      = std::make_unique<ControllerNav>();
    m_scanner  = std::make_unique<GameScanner>();
    m_core     = std::make_unique<LibretroBridge>();

    m_core->setRenderer(m_renderer);
    m_core->setBiosPath("bios/");
    m_core->setSavePath("saves/");

    // Core options for Beetle PSX HW
    // Use software renderer for now — hardware renderer needs OpenGL context setup
    // which is Phase 3. Software renderer is still highly accurate.
    m_core->setCoreOption("beetle_psx_hw_renderer",           "software");
    m_core->setCoreOption("beetle_psx_hw_internal_resolution","1x(native)");
    m_core->setCoreOption("beetle_psx_hw_filter",             "nearest");
    m_core->setCoreOption("beetle_psx_hw_dither_mode",        "internal resolution");
    m_core->setCoreOption("beetle_psx_hw_display_vram",       "disabled");

    m_browser  = std::make_unique<GameBrowser>(m_renderer, m_theme.get(), m_nav.get());
    m_settings = std::make_unique<SettingsScreen>(m_renderer, m_theme.get(),
                                                   m_nav.get(), &m_haackSettings);
    m_splash   = std::make_unique<SplashScreen>(m_renderer, m_theme.get());

    m_scanner->scanDefaultPaths();
    m_browser->setLibrary(m_scanner->getLibrary());
    m_splash->onScanComplete();

    m_state   = AppState::STARTUP;
    m_running = true;

    std::cout << "[HaackStation] Ready — "
              << m_scanner->getLibrary().size() << " games found\n";
}

void HaackApp::shutdown() {
    if (m_core && m_core->isGameLoaded()) m_core->unloadGame();
    m_splash.reset();
    m_settings.reset();
    m_browser.reset();
    m_core.reset();
    m_scanner.reset();
    m_nav.reset();
    m_theme.reset();
    IMG_Quit();
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window)   { SDL_DestroyWindow(m_window);     m_window   = nullptr; }
    SDL_Quit();
}

int HaackApp::run() {
    Uint32 lastTick = SDL_GetTicks();
    while (m_running) {
        Uint32 now   = SDL_GetTicks();
        float  delta = static_cast<float>(now - lastTick);
        lastTick     = now;
        handleEvents();
        update(delta);
        render();
        if (m_state != AppState::IN_GAME) {
            Uint32 elapsed = SDL_GetTicks() - now;
            if (elapsed < (Uint32)FRAME_MS)
                SDL_Delay((Uint32)FRAME_MS - elapsed);
        }
    }
    return 0;
}

void HaackApp::handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                m_running = false;
                return;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    m_browser->onWindowResize(e.window.data1, e.window.data2);
                    m_theme->onWindowResize(e.window.data1, e.window.data2);
                }
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    if (m_state == AppState::IN_GAME)       stopGame();
                    else if (m_state == AppState::SETTINGS) setState(AppState::GAME_BROWSER);
                }
                if (e.key.keysym.sym == SDLK_F11) toggleFullscreen();
                // Keyboard shortcut: Enter opens settings from empty shelf
                if (e.key.keysym.sym == SDLK_RETURN &&
                    m_state == AppState::GAME_BROWSER)
                    setState(AppState::SETTINGS);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                m_nav->onControllerAdded(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                m_nav->onControllerRemoved(e.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                // Start button always opens settings regardless of library state
                if (m_state == AppState::GAME_BROWSER &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    setState(AppState::SETTINGS);
                    continue;  // Don't pass this event to the browser
                }
                // Start+Select together opens in-game menu / quits to shelf
                // Select alone is passed through to the game (needed for many PS1 games)
                if (m_state == AppState::IN_GAME &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    // Check if Start is also held
                    SDL_GameController* ctrl = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(0));
                    if (ctrl && SDL_GameControllerGetButton(
                            ctrl, SDL_CONTROLLER_BUTTON_START)) {
                        stopGame();  // Start+Select = quit to shelf
                    }
                    // Otherwise Select passes through to the game normally
                }
                break;
            default: break;
        }

        // Route events to active screen AFTER app-level handling
        switch (m_state) {
            case AppState::GAME_BROWSER: m_browser->handleEvent(e);  break;
            case AppState::SETTINGS:     m_settings->handleEvent(e); break;
            default: break;
        }
    }
    if (m_state == AppState::IN_GAME) updateGameInput();
}

void HaackApp::update(float deltaMs) {
    switch (m_state) {
        case AppState::STARTUP:
            m_splash->update(deltaMs);
            if (m_splash->isDone()) setState(AppState::GAME_BROWSER);
            break;
        case AppState::GAME_BROWSER:
            m_browser->update(deltaMs);
            if (m_browser->hasPendingLaunch())
                launchGame(m_browser->consumeLaunchPath());
            break;
        case AppState::IN_GAME:
            {
                // Throttle to core-reported FPS (59.94 NTSC, 50.0 PAL)
                // We track frame start time and sleep the remainder
                static Uint32 lastFrameTime = 0;
                Uint32 frameStart = SDL_GetTicks();

                m_core->runFrame();

                // Calculate target frame duration from core timing
                // Default to 59.94fps (NTSC) if core hasn't reported yet
                double fps = m_core->getTargetFps();
                if (fps <= 0.0) fps = 59.94;
                Uint32 frameTargetMs = (Uint32)(1000.0 / fps);

                Uint32 elapsed = SDL_GetTicks() - frameStart;
                if (elapsed < frameTargetMs)
                    SDL_Delay(frameTargetMs - elapsed);

                lastFrameTime = SDL_GetTicks();
                (void)lastFrameTime;
            }
            break;
        case AppState::SETTINGS:
            m_settings->update(deltaMs);
            if (m_settings->wantsClose()) {
                // If user set a new ROM path, rescan
                if (!m_haackSettings.romsPath.empty()) {
                    m_scanner->addSearchPath(m_haackSettings.romsPath);
                    m_scanner->rescan();
                    m_browser->setLibrary(m_scanner->getLibrary());
                }
                setState(AppState::GAME_BROWSER);
            }
            break;
        default: break;
    }
}

void HaackApp::render() {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
    switch (m_state) {
        case AppState::STARTUP:      m_splash->render();                  break;
        case AppState::GAME_BROWSER: m_browser->render();                 break;
        case AppState::IN_GAME:      m_core->blitFramebuffer(m_renderer); break;
        case AppState::SETTINGS:     m_settings->render();                break;
        default: break;
    }
    SDL_RenderPresent(m_renderer);
}

void HaackApp::setState(AppState next) {
    m_state = next;
}

void HaackApp::launchGame(const std::string& path) {
    std::cout << "[HaackStation] Launching: " << path << "\n";
    if (!m_haackSettings.biosPath.empty())
        m_core->setBiosPath(m_haackSettings.biosPath);
    if (!m_core->loadGame(path)) {
        std::cerr << "[HaackStation] Failed to load game.\n";
        std::cerr << "  Ensure BIOS files are in: bios/\n";
    } else {
        setState(AppState::IN_GAME);
    }
}

void HaackApp::stopGame() {
    m_core->unloadGame();
    m_browser->resetAfterGame();
    setState(AppState::GAME_BROWSER);
}

void HaackApp::toggleFullscreen() {
    Uint32 flags = SDL_GetWindowFlags(m_window);
    bool isFs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(m_window, isFs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    m_haackSettings.fullscreen = !isFs;
}

void HaackApp::updateGameInput() {
    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            ctrl = SDL_GameControllerOpen(i);
            break;
        }
    }
    if (!ctrl) return;

    auto btn = [&](SDL_GameControllerButton b) -> bool {
        return SDL_GameControllerGetButton(ctrl, b) != 0;
    };
    int mask = 0;
    if (btn(SDL_CONTROLLER_BUTTON_A))             mask |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (btn(SDL_CONTROLLER_BUTTON_B))             mask |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (btn(SDL_CONTROLLER_BUTTON_X))             mask |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (btn(SDL_CONTROLLER_BUTTON_Y))             mask |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP))       mask |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN))     mask |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT))     mask |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    mask |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (btn(SDL_CONTROLLER_BUTTON_START))         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (btn(SDL_CONTROLLER_BUTTON_BACK))          mask |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (btn(SDL_CONTROLLER_BUTTON_LEFTSTICK))     mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L3);
    if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK))    mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R3);
    Sint16 l2 = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    Sint16 r2 = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (l2 > 8000) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (r2 > 8000) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    m_core->setButtonState(0, mask);
    SDL_GameControllerClose(ctrl);
}
