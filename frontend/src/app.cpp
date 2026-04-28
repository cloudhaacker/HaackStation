#include "app.h"
#include "game_browser.h"
#include "controller_nav.h"
#include "settings_screen.h"
#include "settings_manager.h"
#include "scrape_screen.h"
#include "game_scraper.h"
#include "save_state_manager.h"
#include "ingame_menu.h"
#include "ra_manager.h"
#include "game_details_panel.h"
#include "play_history.h"
#include "per_game_settings.h"
#include "favorites.h"
#include "disc_memory.h"
#include "splash_screen.h"
#include "libretro_bridge.h"
#include "libretro_types.h"
#include "game_scanner.h"
#include "theme_engine.h"
#include "rewind_manager.h"
#include "input_map.h"
#include "trophy_hub.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdexcept>
#include <iostream>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <vector>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <regex>
namespace fs = std::filesystem;

static constexpr int DEFAULT_W = 1280;
static constexpr int DEFAULT_H = 720;

// Fast forward multiplier table — index matches fastForwardSpeed setting
// 0=2x  1=4x  2=6x  3=8x
static constexpr int FF_MULTIPLIERS[] = { 2, 4, 6, 8 };
static constexpr int FF_TABLE_SIZE    = 4;

// Turbo speed ratio table — 0=1.5x  1=2x  2=3x  3=4x  4=6x
// turboStep = fixedStep / ratio → accumulator drains faster → more frames/sec.
static constexpr double TURBO_RATIOS[]       = { 1.5, 2.0, 3.0, 4.0, 6.0 };
static const char*      TURBO_SPEED_LABELS[] = { "1.5x", "2x", "3x", "4x", "6x" };

HaackApp::HaackApp() {
#ifdef _WIN32
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string logPath = exePath;
        auto lastSlash = logPath.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            logPath = logPath.substr(0, lastSlash + 1);
        logPath += "haackstation.log";
        static std::ofstream logFile(logPath);
        if (logFile.is_open()) {
            std::cout.rdbuf(logFile.rdbuf());
            std::cerr.rdbuf(logFile.rdbuf());
        }
    }
#endif
    init();
}
HaackApp::~HaackApp() { shutdown(); }

void HaackApp::init() {
    SettingsManager settingsMgr;
    settingsMgr.load(m_haackSettings);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    m_window = SDL_CreateWindow(
        "HaackStation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        DEFAULT_W, DEFAULT_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!m_window)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    SDL_Surface* icon = IMG_Load("assets/icons/icon.png");
    if (!icon) icon = SDL_LoadBMP("assets/icons/icon.bmp");
    if (icon) { SDL_SetWindowIcon(m_window, icon); SDL_FreeSurface(icon); }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer)
        throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    m_theme   = std::make_unique<ThemeEngine>(m_renderer);
    m_nav     = std::make_unique<ControllerNav>();
    m_scanner = std::make_unique<GameScanner>();
    m_core    = std::make_unique<LibretroBridge>();

    m_core->setRenderer(m_renderer);
    std::string biosPath = m_haackSettings.biosPath.empty()
                           ? "bios/" : m_haackSettings.biosPath;
    m_core->setBiosPath(biosPath);
    m_core->setSavePath("saves/");

    m_core->setCoreOption("beetle_psx_hw_renderer",            "software");
    m_core->setCoreOption("beetle_psx_hw_internal_resolution", "1x(native)");
    m_core->setCoreOption("beetle_psx_hw_filter",              "nearest");
    m_core->setCoreOption("beetle_psx_hw_dither_mode",         "internal resolution");
    m_core->setCoreOption("beetle_psx_hw_display_vram",        "disabled");
    m_core->setCoreOption("beetle_psx_hw_use_mednafen_memcard0_method", "libretro");
    m_core->setCoreOption("beetle_psx_hw_spu_reverb",          "enabled");
    m_core->setCoreOption("beetle_psx_hw_spu_interpolation",   "gaussian");
    m_core->setCoreOption("beetle_psx_hw_cd_access_method",    "sync");
    m_core->setCoreOption("beetle_psx_hw_cd_fastload",         "disabled");

    // Fast Boot: skip PS1 BIOS logo. Loaded from settings, applied per-launch too.
    m_core->setCoreOption("beetle_psx_hw_skip_bios",
        m_haackSettings.fastBoot ? "enabled" : "disabled");

    std::cout << "[HaackStation] Fast Boot: "
              << (m_haackSettings.fastBoot ? "enabled" : "disabled") << "\n";

    m_browser  = std::make_unique<GameBrowser>(m_renderer, m_theme.get(), m_nav.get());
    m_settings = std::make_unique<SettingsScreen>(m_renderer, m_theme.get(),
                                                   m_nav.get(), &m_haackSettings);
    m_splash   = std::make_unique<SplashScreen>(m_renderer, m_theme.get());
    m_scraper  = std::make_unique<ScrapeScreen>(m_renderer, m_theme.get(), m_nav.get());

    m_saveStates = std::make_unique<SaveStateManager>();
    m_saveStates->setBridge(m_core.get());
    m_saveStates->setRenderer(m_renderer);
    m_saveStates->setBaseDir("saves/states/");

    m_inGameMenu = std::make_unique<InGameMenu>(
        m_renderer, m_theme.get(), m_nav.get(), m_saveStates.get());

    m_ra      = std::make_unique<RAManager>();
    m_details = std::make_unique<GameDetailsPanel>(m_renderer, m_theme.get(), m_nav.get());
    m_perGameScreen = std::make_unique<PerGameSettingsScreen>(
        m_renderer, m_theme.get(), m_nav.get());

    // Rewind manager — 10-second buffer, capture every 2 frames.
    // Buffer depth can be made a Settings option later; hardcoded for now.
    m_rewind = std::make_unique<RewindManager>(10, 2);

    // Input map — load from disk (falls back to defaults if not found)
    m_inputMap.load();
    m_remapScreen = std::make_unique<RemapScreen>(
        m_renderer, m_theme.get(), m_nav.get(), &m_inputMap);
    m_trophyRoom  = std::make_unique<TrophyRoom>(
        m_renderer, m_theme.get(), m_nav.get(), m_ra.get());
    m_trophyHub   = std::make_unique<TrophyHub>(
        m_renderer, m_theme.get(), m_nav.get());
    m_trophyHub->setOnViewGame([this](uint32_t gameId, const std::string& title) {
        m_trophyRoom->setGameTitle(title);
        // getCachedAchievementsForGame() works for any game that has been played
        // with RA enabled, even after a cold restart — data is loaded from disk.
        if (m_ra) {
            const auto* achs = m_ra->getCachedAchievementsForGame(gameId);
            if (achs && !achs->empty()) {
                m_trophyRoom->refreshWithData(*achs);
                std::cout << "[App] Trophy Room: loaded " << achs->size()
                          << " achievements for " << title << "\n";
            } else {
                m_trophyRoom->refresh();
                std::cout << "[App] Trophy Room: no cached data for " << title
                          << " — play the game with RA enabled to populate\n";
            }
        } else {
            m_trophyRoom->refresh();
        }
        m_trophyRoom->resetClose();
        setState(AppState::TROPHY_ROOM);
    });

    // Play history — load from disk and pass to browser
    m_playHistory = std::make_unique<PlayHistory>();
    m_playHistory->load();
    m_discMemory.load();
    m_trophyHub->refresh(); // pre-load saved trophy data from disk
    m_favorites.load();
    m_ra->setRenderer(m_renderer);
    m_ra->setTheme(m_theme.get());
    m_ra->setAutoScreenshot(m_haackSettings.raAutoScreenshot);
    if (!m_haackSettings.raUser.empty()) {
        m_ra->setTokenSaveCallback([this](const std::string& token) {
            saveRaToken(token);
        });
        m_ra->initialize(m_haackSettings.raUser,
                          m_haackSettings.raApiKey,
                          m_haackSettings.raPassword);
    }

    if (!m_haackSettings.romsPath.empty())
        m_scanner->addSearchPath(m_haackSettings.romsPath);
    m_scanner->scanDefaultPaths();
    m_browser->setLibrary(m_scanner->getLibrary());
    m_browser->setPlayHistory(m_playHistory.get());
    m_browser->setFavoriteManager(&m_favorites);
    m_browser->setTopRowMode(m_haackSettings.topRowMode);
    auto& autoM3us = m_scanner->autoGeneratedM3us();
    if (!autoM3us.empty()) {
        std::cout << "[HaackStation] Auto-generated "
                  << autoM3us.size() << " M3U file(s) for multi-disc games\n";
    }
    m_splash->onScanComplete();

    if (m_haackSettings.fullscreen) {
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE); // Hide cursor in fullscreen
    }

    m_state   = AppState::STARTUP;
    m_running = true;

    int ffMult = FF_MULTIPLIERS[
        std::max(0, std::min(m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1))];
    std::cout << "[HaackStation] Ready — "
              << m_scanner->getLibrary().size() << " games found\n";
    std::cout << "[HaackStation] Keyboard:\n"
              << "  SHELF:   Arrows=Navigate  X=Launch  Enter=Settings  F2=Details  Esc=Quit\n"
              << "  INGAME:  Arrows=D-pad  X=Cross  Z=Circle  A=Square  S=Triangle\n"
              << "           Q=L1  W=R1  E=L2  R=R2  Enter=Start  Space=Select\n"
              << "           F=Fast Forward (hold)  BackQuote=Rewind (hold)\n"
              << "           F1=In-game menu  Esc=Quit to shelf\n"
              << "  GLOBAL:  F11=Fullscreen\n"
              << "[HaackStation] Fast forward: " << ffMult << "x (hold R2 or F)\n"
              << "[HaackStation] Rewind: hold L2 (controller) or ` (backtick)\n";
}

void HaackApp::shutdown() {
    SettingsManager settingsMgr;
    settingsMgr.save(m_haackSettings);

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
    Uint64 perfFreq    = SDL_GetPerformanceFrequency();
    Uint64 lastTime    = SDL_GetPerformanceCounter();
    double accumulator = 0.0;

    while (m_running) {
        Uint64 now     = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - lastTime) / perfFreq;
        lastTime       = now;
        if (elapsed > 0.1) elapsed = 0.1;

        handleEvents();

        if (m_state == AppState::IN_GAME) {
            if (m_inGameMenu && m_inGameMenu->isOpen()) {
                accumulator = 0.0;
                m_inGameMenu->update((float)(elapsed * 1000.0));
            } else {
                double fps      = m_core->getTargetFps();
                if (fps <= 0.0) fps = 59.94;
                double fixedStep = 1.0 / fps;

                accumulator += elapsed;
                while (accumulator >= fixedStep) {
                    if (m_rewinding) {
                        // ── Rewind ────────────────────────────────────────────
                        // Restore the previous captured state, then call runFrame()
                        // so the core renders it — this makes the picture visibly
                        // step backward on screen. Audio is muted to avoid the
                        // weird backward-SPU effect.
                        SDL_AudioDeviceID audioDev = m_core->getAudioDevice();
                        if (audioDev) SDL_PauseAudioDevice(audioDev, 1);

                        // stepBack() restores state; runFrame() renders it to screen
                        // so the picture visibly rewinds frame-by-frame.
                        if (m_rewind->stepBack())
                            m_core->runFrame();

                        if (audioDev) {
                            SDL_ClearQueuedAudio(audioDev);
                            SDL_PauseAudioDevice(audioDev, 0);
                        }

                        // Periodic rumble pulse while rewinding
                        Uint32 nowMs = SDL_GetTicks();
                        if (nowMs >= m_rumbleNextAt) {
                            if (m_nav) m_nav->rumbleConfirm();
                            m_rumbleNextAt = nowMs + RUMBLE_PULSE_INTERVAL_MS;
                        }

                    } else if (m_fastForward) {
                        // ── Fast Forward ──────────────────────────────────────
                        int idx  = std::max(0, std::min(
                            m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1));
                        int mult = FF_MULTIPLIERS[idx];

                        SDL_AudioDeviceID audioDev = m_core->getAudioDevice();
                        if (audioDev) SDL_PauseAudioDevice(audioDev, 1);

                        for (int ff = 0; ff < mult; ++ff) {
                            m_core->runFrame();
                            if (m_ra && m_ra->isGameLoaded())
                                m_ra->doFrame(m_core.get());
                        }
                        // Still capture rewind states during FF (optional but nice)
                        m_rewind->captureFrame();

                        if (audioDev) {
                            SDL_ClearQueuedAudio(audioDev);
                            SDL_PauseAudioDevice(audioDev, 0);
                        }

                        // Periodic rumble pulse while fast-forwarding
                        Uint32 nowMs = SDL_GetTicks();
                        if (nowMs >= m_rumbleNextAt) {
                            if (m_nav) m_nav->rumbleConfirm();
                            m_rumbleNextAt = nowMs + RUMBLE_PULSE_INTERVAL_MS;
                        }

                    } else if (m_turboActive) {
                        // ── Turbo ─────────────────────────────────────────────
                        // Shrink the effective step so the accumulator drains
                        // faster → more frames run per real second.
                        // Uses 'continue' to skip the outer loop's
                        // "accumulator -= fixedStep" — the inner loop already
                        // drained it fully. Without this, turbo has no effect.
                        int idx = std::max(0, std::min(m_haackSettings.turboSpeed, 4));
                        double turboStep = fixedStep / TURBO_RATIOS[idx];
                        const int TURBO_MAX_FRAMES = 12;
                        int turboFrames = 0;
                        while (accumulator >= turboStep && turboFrames < TURBO_MAX_FRAMES) {
                            m_core->runFrame();
                            if (m_ra && m_ra->isGameLoaded())
                                m_ra->doFrame(m_core.get());
                            accumulator -= turboStep;
                            turboFrames++;
                        }
                        m_rewind->captureFrame();
                        continue; // skip outer accumulator -= fixedStep, inner loop drained it
                    } else {
                        // ── Normal frame ──────────────────────────────────────
                        m_core->runFrame();
                        if (m_ra && m_ra->isGameLoaded())
                            m_ra->doFrame(m_core.get());
                        // Capture rewind snapshot every N frames
                        m_rewind->captureFrame();
                    }
                    accumulator -= fixedStep;
                }
            }
            if (m_ra) m_ra->update((float)(elapsed * 1000.0));
        } else {
            update((float)(elapsed * 1000.0));
            Uint32 frameMs = SDL_GetTicks() - (Uint32)(lastTime * 1000 / perfFreq);
            if (frameMs < 16) SDL_Delay(16 - frameMs);
        }

        render();
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
                if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
                    e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    int w, h;
                    SDL_GetRendererOutputSize(m_renderer, &w, &h);
                    m_browser->onWindowResize(w, h);
                    m_settings->onWindowResize(w, h);
                    m_inGameMenu->onWindowResize(w, h);
                    m_theme->onWindowResize(w, h);
                    if (m_perGameScreen)  m_perGameScreen->onWindowResize(w, h);
                    if (m_remapScreen)    m_remapScreen->onWindowResize(w, h);
                    if (m_trophyRoom)     m_trophyRoom->onWindowResize(w, h);
                    if (m_details)        m_details->onWindowResize(w, h);
                }
                break;

            case SDL_KEYDOWN: {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_F11) { toggleFullscreen(); continue; }
                if (key == SDLK_F10) { takeUIScreenshot(); continue; }

                if (m_state == AppState::IN_GAME) {
                    if (key == SDLK_ESCAPE) { stopGame(); continue; }
                    if (key == SDLK_F5) {
                        m_saveStates->saveState(0);
                        std::cout << "[App] Quick save slot 1\n";
                        continue;
                    }
                    if (key == SDLK_F7) {
                        m_saveStates->loadState(0);
                        std::cout << "[App] Quick load slot 1\n";
                        continue;
                    }
                    if (key == SDLK_F1) {
                        if (m_inGameMenu->isOpen()) m_inGameMenu->close();
                        else                        m_inGameMenu->open();
                        continue;
                    }
                    if (key == SDLK_F12) {
                        // Capture with overlay when in-game menu is open (disc select etc.)
                        // Otherwise capture clean game framebuffer only.
                        if (m_inGameMenu && m_inGameMenu->isOpen())
                            takeUIScreenshot();
                        else
                            takeScreenshot();
                        continue;
                    }
                    if (key == SDLK_f) {
                        // Start hold timer — FF activates after FF_HOLD_DELAY_MS
                        if (m_ffHeldSince == 0)
                            m_ffHeldSince = SDL_GetTicks();
                        continue;
                    }
                    // Backtick / grave = rewind (keyboard equivalent of L2)
                    if (key == SDLK_BACKQUOTE) {
                        if (m_rewindHeldSince == 0)
                            m_rewindHeldSince = SDL_GetTicks();
                        continue;
                    }
                    break;
                }
                if (m_state == AppState::GAME_BROWSER) {
                    if (m_details && m_details->isOpen()) break;
                    if (key == SDLK_ESCAPE) { m_running = false; continue; }
                    if (key == SDLK_RETURN) {
                        m_settings->resetClose();
                        setState(AppState::SETTINGS);
                        continue;
                    }
                    // F3 or Z — open Trophy Hub (global achievements overview)
                    // Z = keyboard B equivalent; B has no function on the shelf
                    if (key == SDLK_F3 || key == SDLK_z) {
                        m_trophyHub->resetClose();
                        setState(AppState::TROPHY_HUB);
                        continue;
                    }
                    // F2 / X button details handled in game_browser.cpp
                    break;
                }
                if (m_state == AppState::SETTINGS) {
                    if (key == SDLK_ESCAPE) {
                        applySettings();
                        setState(AppState::GAME_BROWSER);
                        continue;
                    }
                    break;
                }
                break;
            }

            case SDL_KEYUP:
                if (e.key.keysym.sym == SDLK_f) {
                    m_fastForward = false;
                    m_ffHeldSince = 0;
                    m_rumbleNextAt = 0;
                }
                if (e.key.keysym.sym == SDLK_BACKQUOTE) {
                    m_rewinding       = false;
                    m_rewindHeldSince = 0;
                    m_rumbleNextAt    = 0;
                }
                break;

            case SDL_CONTROLLERDEVICEADDED:
                m_nav->onControllerAdded(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                m_nav->onControllerRemoved(e.cdevice.which);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                // Select+Y = UI screenshot from any non-game state
                if (m_state != AppState::IN_GAME &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
                    SDL_GameController* ctrl2 = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(0));
                    if (ctrl2 && SDL_GameControllerGetButton(
                            ctrl2, SDL_CONTROLLER_BUTTON_BACK)) {
                        takeUIScreenshot();
                        continue;
                    }
                }
                if (m_state == AppState::GAME_BROWSER &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    m_settings->resetClose();
                    setState(AppState::SETTINGS);
                    continue;
                }
                if (m_state == AppState::GAME_BROWSER &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                    m_settings->resetClose();
                    setState(AppState::SETTINGS);
                    continue;
                }
                // B on the browser shelf (no details panel open) → Trophy Hub
                // B has no other function here so it's a natural fit.
                if (m_state == AppState::GAME_BROWSER &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_B &&
                    !(m_details && m_details->isOpen())) {
                    m_trophyHub->resetClose();
                    setState(AppState::TROPHY_HUB);
                    continue;
                }
                if (m_state == AppState::IN_GAME &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
                    SDL_GameController* ctrl = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(0));
                    if (ctrl) {
                        if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_START)) {
                            // Start+Y = in-game menu
                            if (m_inGameMenu->isOpen()) m_inGameMenu->close();
                            else                        m_inGameMenu->open();
                        } else if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_BACK)) {
                            // Select+Y = screenshot.
                            // When in-game menu (disc select etc.) is open, capture
                            // the full screen with overlay via takeUIScreenshot().
                            // Otherwise capture the clean game framebuffer.
                            if (m_inGameMenu && m_inGameMenu->isOpen())
                                takeUIScreenshot();
                            else
                                takeScreenshot();
                        }
                    }
                }
                if (m_state == AppState::IN_GAME &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    SDL_GameController* ctrl = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(0));
                    if (ctrl && SDL_GameControllerGetButton(
                            ctrl, SDL_CONTROLLER_BUTTON_START))
                        stopGame();
                }
                break;

            default: break;
        }

        if (m_state == AppState::GAME_BROWSER &&
            m_details && m_details->isOpen()) {
            // Per-game settings screen takes priority when open
            if (m_perGameScreen && m_perGameScreen->isOpen())
                m_perGameScreen->handleEvent(e);
            else
                m_details->handleEvent(e);
        } else if (m_state == AppState::IN_GAME && m_inGameMenu->isOpen()) {
            m_inGameMenu->handleEvent(e);
        } else {
            switch (m_state) {
                case AppState::GAME_BROWSER: m_browser->handleEvent(e);     break;
                case AppState::SETTINGS:     m_settings->handleEvent(e);    break;
                case AppState::REMAPPING:    m_remapScreen->handleEvent(e); break;
                case AppState::TROPHY_ROOM:  m_trophyRoom->handleEvent(e);  break;
                case AppState::TROPHY_HUB:   m_trophyHub->handleEvent(e);   break;
                case AppState::SCRAPING:     m_scraper->handleEvent(e);     break;
                default: break;
            }
        }
    }

    if (m_state == AppState::IN_GAME) {
        if (!m_inGameMenu || !m_inGameMenu->isOpen())
            updateGameInput();
        else
            processInGameMenuActions();
    }
}

void HaackApp::processInGameMenuActions() {
    if (!m_inGameMenu) return;
    InGameMenuAction action = m_inGameMenu->pendingAction();
    if (action == InGameMenuAction::NONE) return;
    m_inGameMenu->clearAction();

    if (action == InGameMenuAction::RESUME) {
        m_inGameMenu->close();
        // Cooldown prevents the CONFIRM press that closed the menu from
        // immediately registering as a PS1 button press in-game.
        m_inputCooldownUntil = SDL_GetTicks() + 250;
    } else if (action == InGameMenuAction::SAVE_STATE) {
        int slot = m_inGameMenu->selectedSlot();
        SDL_Surface* shot = m_saveStates->captureCleanScreenshot();
        m_saveStates->saveState(slot, shot);
        if (shot) SDL_FreeSurface(shot);
        m_inGameMenu->close();
        m_inputCooldownUntil = SDL_GetTicks() + 250;
    } else if (action == InGameMenuAction::LOAD_STATE) {
        int slot = m_inGameMenu->selectedSlot();
        auto slots = m_saveStates->listSlots();
        if (slot < (int)slots.size())
            m_saveStates->loadState(slots[slot].slotNumber);
        m_inGameMenu->close();
        m_inputCooldownUntil = SDL_GetTicks() + 500;
    } else if (action == InGameMenuAction::CHANGE_DISC) {
        int disc = m_inGameMenu->pendingDiscIndex();
        m_inGameMenu->close();
        std::vector<std::string> discPaths = parseM3uDiscs(m_currentGamePath);
        if (disc < (int)discPaths.size()) {
            std::string discPath = discPaths[disc];
            std::cout << "[HaackStation] Changing to disc " << (disc+1)
                      << ": " << discPath << "\n";
            // Auto-save before switching
            if (m_saveStates && m_core->isGameLoaded()) {
                SDL_Surface* shot = m_saveStates->captureCleanScreenshot();
                m_saveStates->autoSave(shot);
                if (shot) SDL_FreeSurface(shot);
            }
            // Remember this disc for next launch
            m_discMemory.setDisc(m_currentGamePath, disc);

            // Build disc art paths using the scraper's naming convention:
            // media/discs/[safe game title]_disc1.png, _disc2.png, etc.
            // Uses the GameEntry title (from the browser) for accurate matching.
            {
                std::string mediaDir = m_haackSettings.romsPath.empty()
                    ? "media/" : m_haackSettings.romsPath + "/media/";
                const GameEntry* ge = m_browser->selectedGameEntry();
                std::string artTitle = (ge && !ge->title.empty())
                    ? ge->title
                    : stripRomRegion(fs::path(m_currentGamePath).stem().string());
                // Sanitize title the same way the scraper does
                const std::string invalid = "\\/:*?\"<>|";
                for (auto& c : artTitle)
                    if (invalid.find(c) != std::string::npos) c = '_';
                std::vector<std::string> artPaths;
                artPaths.reserve(discPaths.size());
                for (int d = 1; d <= (int)discPaths.size(); d++)
                    artPaths.push_back(mediaDir + "discs/" + artTitle
                                       + "_disc" + std::to_string(d) + ".png");
                m_inGameMenu->setDiscArtPaths(artPaths);
            }

            // Reload core on the new disc
            std::string savedPath = m_currentGamePath;
            if (m_core->isGameLoaded()) m_core->unloadGame();
            if (m_core->loadGame(discPath)) {
                m_currentGamePath = savedPath; // keep M3U path as canonical
                m_saveStates->setCurrentGame(
                    stripRomRegion(fs::path(savedPath).stem().string()),
                    savedPath);
                if (m_rewind) m_rewind->init(m_core.get());
                m_inGameMenu->setDiscInfo(discPaths, disc);
                SDL_Texture* cover = m_browser->getCoverArtForGame(savedPath);
                m_inGameMenu->setCoverTexture(cover);
                std::cout << "[HaackStation] Disc " << (disc+1)
                          << " loaded OK\n";
            } else {
                std::cerr << "[HaackStation] Failed to load disc "
                          << (disc+1) << "\n";
                stopGame();
            }
        }
        m_inputCooldownUntil = SDL_GetTicks() + 500;
    } else if (action == InGameMenuAction::QUIT_TO_SHELF) {
        m_inGameMenu->close();
        stopGame();
    }
}

void HaackApp::update(float deltaMs) {
    switch (m_state) {
        case AppState::STARTUP:
            m_splash->update(deltaMs);
            if (m_splash->isDone()) setState(AppState::GAME_BROWSER);
            break;
        case AppState::GAME_BROWSER:
            // Don't update the browser (which drives shelf scrolling via its
            // held-repeat logic) while the details panel is open. The details
            // panel owns input focus at that point and the shelf scrolling
            // behind it is the visible symptom of this leak.
            if (!m_details || !m_details->isOpen())
                m_browser->update(deltaMs);
            if (m_browser->hasPendingLaunch())
                launchGame(m_browser->consumeLaunchPath());

            // Y button — toggle favorite and give rumble feedback
            if (m_browser->wantsFavoriteToggle()) {
                m_browser->clearFavoriteToggle();
                auto* game = m_browser->selectedGameEntry();
                if (game) {
                    bool nowFav = m_favorites.toggle(game->path);
                    if (nowFav) {
                        std::cout << "[Favorites] Added: " << game->title << "\n";
                        m_nav->rumbleConfirm();
                    } else {
                        std::cout << "[Favorites] Removed: " << game->title << "\n";
                        m_nav->rumbleError(); // distinct feedback for removal
                    }
                    // Rebuild the favorites shelf so it stays in sync
                    m_browser->setFavoriteManager(&m_favorites);
                }
            }

            if (m_browser->wantsDetails() && m_details && !m_details->isOpen()) {
                m_browser->clearWantsDetails();
                auto* game = m_browser->selectedGameEntry();
                if (game) {
                    // Tell the panel where media lives before opening
                    std::string mediaDir = m_haackSettings.romsPath.empty()
                        ? "media/" : m_haackSettings.romsPath + "/media/";
                    m_details->setMediaDir(mediaDir);
                    m_details->open(*game, m_saveStates.get());
                    SDL_Texture* cover = m_browser->getCoverArtForGame(game->path);
                    m_details->setCoverTexture(cover);
                    // Pass playtime stats from play history
                    if (m_playHistory) {
                        m_details->setPlaytimeStats(
                            m_playHistory->getTotalSeconds(game->path),
                            m_playHistory->getPlayCount(game->path));
                    }
                    // Wire in RA trophy data so trophy row is navigable.
                    // Use per-game cache — works on cold start for any previously
                    // played game, not just the most recently loaded one.
                    if (m_ra && game) {
                        // Look up gameId from the ROM path (populated from disk cache)
                        uint32_t raGameId = m_ra->getGameIdForPath(game->path);
                        const std::vector<AchievementInfo>* achievements = nullptr;
                        if (raGameId != 0)
                            achievements = m_ra->getCachedAchievementsForGame(raGameId);

                        if (achievements && !achievements->empty()) {
                            int unlocked = 0;
                            for (const auto& a : *achievements) if (a.unlocked) unlocked++;
                            std::vector<std::string> badgePaths;
                            for (const auto& a : *achievements) {
                                if (a.unlocked && !a.badgeLocalPath.empty()
                                    && fs::exists(a.badgeLocalPath)) {
                                    badgePaths.push_back(a.badgeLocalPath);
                                    if ((int)badgePaths.size() >= 5) break;
                                }
                            }
                            m_details->setTrophyInfo(unlocked, (int)achievements->size(), badgePaths);
                        }
                    }
                }
            } else if (m_browser->wantsDetails()) {
                m_browser->clearWantsDetails();
            }
            if (m_details && m_details->isOpen()) {
                // If per-game settings screen is open, route to it instead
                if (m_perGameScreen && m_perGameScreen->isOpen()) {
                    m_perGameScreen->update(deltaMs);
                    if (m_perGameScreen->wantsClose()) {
                        m_perGameScreen->clearClose();
                        m_perGameScreen->close();
                        // Re-apply settings for next launch (in case game is running)
                        if (m_core && m_core->isGameLoaded())
                            applyPerGameSettings(m_currentGamePath, "");
                    }
                } else {
                    m_details->update(deltaMs);
                    auto act = m_details->pendingAction();
                    if (act == DetailsPanelAction::CLOSE) {
                        m_details->clearAction();
                    } else if (act == DetailsPanelAction::OPEN_TROPHY_ROOM) {
                        m_details->clearAction();
                        auto* game = m_browser->selectedGameEntry();
                        if (game) {
                            // Prefer RA title; fall back to browser title
                            uint32_t raGameId = m_ra ? m_ra->getGameIdForPath(game->path) : 0;
                            const RAGameInfo* raInfo = (raGameId && m_ra)
                                ? m_ra->getCachedGameInfoForGame(raGameId) : nullptr;
                            std::string title = (raInfo && !raInfo->title.empty())
                                ? raInfo->title : game->title;
                            m_trophyRoom->setGameTitle(title);

                            const std::vector<AchievementInfo>* achs = (raGameId && m_ra)
                                ? m_ra->getCachedAchievementsForGame(raGameId) : nullptr;
                            if (achs && !achs->empty()) {
                                m_trophyRoom->refreshWithData(*achs);
                                std::cout << "[App] Trophy Room (details): "
                                          << achs->size() << " achievements for "
                                          << title << "\n";
                            } else {
                                m_trophyRoom->refresh();
                                std::cout << "[App] Trophy Room (details): no cache for "
                                          << title << "\n";
                            }
                        } else {
                            m_trophyRoom->refresh();
                        }
                        m_trophyRoom->resetClose();
                        setState(AppState::TROPHY_ROOM);
                    } else if (act == DetailsPanelAction::OPEN_PER_GAME_SETTINGS) {
                        m_details->clearAction();
                        auto* game = m_browser->selectedGameEntry();
                        if (game) {
                            m_perGameSettings.load(game->serial, game->path);
                            m_perGameScreen->open(
                                game->title, game->path, game->serial,
                                m_perGameSettings.overrides());
                        }
                    } else if (act != DetailsPanelAction::NONE) {
                        m_details->clearAction();
                    }
                }
            }
            break;
        case AppState::IN_GAME:
            if (m_inGameMenu && m_inGameMenu->isOpen())
                m_inGameMenu->update(deltaMs);
            if (m_ra) m_ra->update(deltaMs);
            break;
        case AppState::SETTINGS:
            m_settings->update(deltaMs);
            if (m_settings->wantsQuit()) {
                applySettings();
                m_running = false;
            } else if (m_settings->wantsScrape()) {
                m_settings->clearScrape();
                applySettings();
                {
                    std::string mediaDir = m_haackSettings.romsPath.empty()
                        ? "media/" : m_haackSettings.romsPath + "/media/";
                    auto& lib = const_cast<std::vector<GameEntry>&>(
                        m_scanner->getLibrary());
                    m_scraper->startScraping(lib, mediaDir,
                        m_haackSettings.ssUser,
                        m_haackSettings.ssPassword,
                        m_haackSettings.ssDevId,
                        m_haackSettings.ssDevPassword);
                }
                setState(AppState::SCRAPING);
            } else if (m_settings->wantsRemap()) {
                m_settings->clearRemap();
                m_remapScreen->resetClose();
                setState(AppState::REMAPPING);
            } else if (m_settings->wantsClose()) {
                applySettings();
                setState(AppState::GAME_BROWSER);
            }
            break;
        case AppState::REMAPPING:
            m_remapScreen->update(deltaMs);
            if (m_remapScreen->wantsClose()) {
                if (m_remapScreen->isDirty()) {
                    m_inputMap.save();
                    m_remapScreen->clearDirty();
                }
                m_remapScreen->resetClose();
                setState(AppState::SETTINGS);
            }
            break;
        case AppState::TROPHY_ROOM:
            m_trophyRoom->update(deltaMs);
            if (m_trophyRoom->wantsClose()) {
                m_trophyRoom->resetClose();
                // Return to hub if we came from there, otherwise browser
                setState(m_prevState == AppState::TROPHY_HUB
                    ? AppState::TROPHY_HUB : AppState::GAME_BROWSER);
            }
            break;
        case AppState::TROPHY_HUB:
            m_trophyHub->update(deltaMs);
            if (m_trophyHub->wantsClose()) {
                m_trophyHub->resetClose();
                setState(AppState::GAME_BROWSER);
            }
            break;
        case AppState::SCRAPING:
            m_scraper->update(deltaMs);
            if (m_scraper->isDone() || m_scraper->wasCancelled()) {
                m_scanner->rescan();
                m_browser->clearCoverArtCache();
                m_browser->setLibrary(m_scanner->getLibrary());
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
        case AppState::STARTUP:      m_splash->render();  break;
        case AppState::GAME_BROWSER:
            m_browser->render();
            if (m_details && m_details->isOpen()) {
                m_details->render();
                if (m_perGameScreen && m_perGameScreen->isOpen())
                    m_perGameScreen->render();
            }
            break;
        case AppState::IN_GAME:
            m_core->blitFramebuffer(m_renderer);
            if (m_ra) {
                int w, h;
                SDL_GetRendererOutputSize(m_renderer, &w, &h);
                m_ra->render(w, h);
            }
            if (m_inGameMenu->isOpen()) m_inGameMenu->render();
            if (m_fastForward)          renderFastForwardIndicator();
            if (m_rewinding)            renderRewindIndicator();
            if (m_turboActive)          renderTurboIndicator();
            break;
        case AppState::SETTINGS:     m_settings->render();      break;
        case AppState::REMAPPING:    m_remapScreen->render();   break;
        case AppState::TROPHY_ROOM:  m_trophyRoom->render();    break;
        case AppState::TROPHY_HUB:   m_trophyHub->render();     break;
        case AppState::SCRAPING:     m_scraper->render();       break;
        default: break;
    }
    // Screenshot notification renders on top of any state
    if (m_screenshotNotifyUntil > SDL_GetTicks())
        renderScreenshotNotification();
    // Trophy auto-screenshot: capture NOW — backbuffer has game + all overlays,
    // RenderPresent hasn't flipped yet so pixels are exactly what the user sees.
    if (m_ra) m_ra->takePendingTrophyScreenshot();
    SDL_RenderPresent(m_renderer);
}

void HaackApp::renderFastForwardIndicator() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    int idx  = std::max(0, std::min(m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1));
    int mult = FF_MULTIPLIERS[idx];
    std::string label = ">>" + std::to_string(mult) + "x";

    int badgeW = 80, badgeH = 34;
    int badgeX = w - badgeW - 14;
    int badgeY = 14;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 20, 20, 40, 210);
    SDL_Rect bg = { badgeX, badgeY, badgeW, badgeH };
    SDL_RenderFillRect(m_renderer, &bg);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
    SDL_RenderDrawRect(m_renderer, &bg);

    // Centre text vertically: badgeY + (badgeH - font_pt) / 2
    int textY = badgeY + (badgeH - (int)FontSize::SMALL) / 2 - 1;
    m_theme->drawTextCentered(label,
        badgeX + badgeW / 2, textY,
        pal.accent, FontSize::SMALL);
}

// ─── renderRewindIndicator ────────────────────────────────────────────────────
// Shown top-right below the FF badge when rewinding.
// Purple/violet accent to visually distinguish from orange FF badge.
void HaackApp::renderRewindIndicator() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    // Buffer fill fraction (0.0 = empty, 1.0 = full)
    float fill = m_rewind->capacity() > 0
        ? (float)m_rewind->depth() / (float)m_rewind->capacity()
        : 0.0f;

    std::string label = "<<RW";

    int badgeW = 80, badgeH = 34;
    int badgeX = w - badgeW - 14;
    int badgeY = 14 + 42; // below FF badge

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 20, 10, 40, 210);
    SDL_Rect bg = { badgeX, badgeY, badgeW, badgeH };
    SDL_RenderFillRect(m_renderer, &bg);

    SDL_SetRenderDrawColor(m_renderer, 100, 40, 160, 120);
    SDL_Rect fillRect = { badgeX, badgeY, (int)(badgeW * fill), badgeH };
    SDL_RenderFillRect(m_renderer, &fillRect);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(m_renderer, 160, 80, 255, 255);
    SDL_RenderDrawRect(m_renderer, &bg);

    SDL_Color violet = { 200, 150, 255, 255 };
    int textY = badgeY + (badgeH - (int)FontSize::SMALL) / 2 - 1;
    m_theme->drawTextCentered(label,
        badgeX + badgeW / 2, textY,
        violet, FontSize::SMALL);
}

// ─── renderTurboIndicator ────────────────────────────────────────────────────
// Persistent green badge shown top-right whenever turbo is active.
// Stacks below the FF and rewind badges if those are also showing.
// No fill bar (turbo has no buffer) — just a steady label.
void HaackApp::renderTurboIndicator() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    const int turboMax = 4;
    int idx = m_haackSettings.turboSpeed;
    if (idx < 0)        idx = 0;
    if (idx > turboMax) idx = turboMax;
    // Display the effective speed as a fraction string: 1.5x, 2x, 3x, 4x, 6x
    
    std::string label = std::string("TRB ") + TURBO_SPEED_LABELS[idx];

    int badgeW = 86, badgeH = 34;
    int badgeX = w - badgeW - 14;
    // Stack below FF badge (14px) and RW badge (14+42=56px)
    int badgeY = 14 + 42 + 42;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 10, 30, 10, 210);
    SDL_Rect bg = { badgeX, badgeY, badgeW, badgeH };
    SDL_RenderFillRect(m_renderer, &bg);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Green border
    SDL_SetRenderDrawColor(m_renderer, 60, 200, 80, 255);
    SDL_RenderDrawRect(m_renderer, &bg);

    SDL_Color green = { 100, 230, 110, 255 };
    int textY = badgeY + (badgeH - (int)FontSize::SMALL) / 2 - 1;
    m_theme->drawTextCentered(label,
        badgeX + badgeW / 2, textY,
        green, FontSize::SMALL);
}

// ─── renderScreenshotNotification ────────────────────────────────────────────
// Brief toast in the top-left: "Screenshot saved" fades in instantly,
// stays 2 seconds, then disappears.
void HaackApp::renderScreenshotNotification() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    // Fade out in the last 400ms
    Uint32 now       = SDL_GetTicks();
    Uint32 remaining = m_screenshotNotifyUntil - now;
    Uint8  alpha     = 220;
    if (remaining < 400)
        alpha = (Uint8)(220 * remaining / 400);

    int toastW = 200, toastH = 32;
    int toastX = 12;
    int toastY = 12;

    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 20, 20, 40, alpha);
    SDL_Rect bg = { toastX, toastY, toastW, toastH };
    SDL_RenderFillRect(m_renderer, &bg);

    // Green accent border (success colour)
    SDL_SetRenderDrawColor(m_renderer, 60, 180, 60, alpha);
    SDL_RenderDrawRect(m_renderer, &bg);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_Color textCol = { 200, 255, 200, alpha };
    m_theme->drawText("Screenshot saved",
        toastX + 10, toastY + 8, textCol, FontSize::SMALL);
}

void HaackApp::setState(AppState next) {
    m_prevState = m_state;
    m_state = next;
}

void HaackApp::launchGame(const std::string& path) {
    std::cout << "[HaackStation] Launching: " << path << "\n";
    if (!m_haackSettings.biosPath.empty())
        m_core->setBiosPath(m_haackSettings.biosPath);

    // ── Fast Boot fix ─────────────────────────────────────────────────────────
    if (!m_core->isCoreLoaded()) {
        std::string corePath = LibretroBridge::defaultCorePath();
        if (!m_core->loadCore(corePath)) {
            std::cerr << "[HaackStation] Failed to load core: " << corePath << "\n";
            m_browser->resetAfterGame();
            setState(AppState::GAME_BROWSER);
            return;
        }
    }

    m_core->setCoreOption("beetle_psx_hw_skip_bios",
        m_haackSettings.fastBoot ? "enabled" : "disabled");

    if (!m_core->loadGame(path)) {
        std::cerr << "[HaackStation] Failed to load game.\n";
        std::cerr << "  Ensure BIOS files are in: bios/\n";
        m_browser->resetAfterGame();
        setState(AppState::GAME_BROWSER);
    } else {
        m_currentGamePath = path;
        fs::path p(path);
        std::string title = p.stem().string();
        m_saveStates->setCurrentGame(title, path);

        // Record in play history (moves to front if already present)
        if (m_playHistory) {
            m_playHistory->recordPlay(path, title);
            m_sessionStartTime = std::time(nullptr); // start session timer
        }
        if (m_ra && m_ra->isLoggedIn())
            m_ra->loadGame(path, m_core.get());

        // ── Apply per-game settings ───────────────────────────────────────────
        // Load and apply any per-game overrides (resolution, shader, etc.)
        // This must happen after the core is loaded and game path is set.
        applyPerGameSettings(path, ""); // serial detection can be added later

        // ── Initialise rewind buffer for this game ─────────────────────────
        if (m_rewind) {
            if (m_rewind->init(m_core.get()))
                std::cout << "[HaackStation] Rewind ready\n";
            else
                std::cout << "[HaackStation] Rewind not available for this core\n";
        }

        // ── Multi-disc: tell in-game menu about available discs ──────────────
        // If the game path is an M3U playlist, parse it to get the individual
        // disc paths. This works regardless of what GameEntry fields exist.
        if (m_inGameMenu) {
            std::vector<std::string> discPaths = parseM3uDiscs(path);
            if (discPaths.size() > 1) {
                int lastDisc = m_discMemory.getDisc(path);
                lastDisc = std::max(0, std::min(lastDisc,
                    (int)discPaths.size() - 1));

                // If the remembered disc isn't disc 1, reload the core on
                // that disc now. The M3U always starts on disc 1 so we need
                // to explicitly reload with the right individual disc path.
                if (lastDisc > 0) {
                    std::cout << "[HaackStation] Resuming on disc "
                              << (lastDisc + 1) << ", reloading core...\n";
                    m_core->unloadGame();
                    if (!m_core->loadGame(discPaths[lastDisc])) {
                        std::cerr << "[HaackStation] Failed to reload disc "
                                  << (lastDisc + 1) << ", falling back to disc 1\n";
                        m_core->loadGame(path); // fall back to M3U (disc 1)
                        lastDisc = 0;
                        m_discMemory.setDisc(path, 0);
                    }
                    // Reinit rewind for the newly loaded disc
                    if (m_rewind) m_rewind->init(m_core.get());
                }

                m_inGameMenu->setDiscInfo(discPaths, lastDisc);
                SDL_Texture* cover = m_browser->getCoverArtForGame(path);
                m_inGameMenu->setCoverTexture(cover);

                // Build disc art paths — media/discs/[safe title]_disc1.png etc.
                {
                    std::string mediaDir = m_haackSettings.romsPath.empty()
                        ? "media/" : m_haackSettings.romsPath + "/media/";
                    const GameEntry* ge = m_browser->selectedGameEntry();
                    std::string artTitle = (ge && !ge->title.empty())
                        ? ge->title
                        : stripRomRegion(fs::path(path).stem().string());
                    const std::string invalid = "\\/:*?\"<>|";
                    for (auto& c : artTitle)
                        if (invalid.find(c) != std::string::npos) c = '_';
                    std::vector<std::string> artPaths;
                    artPaths.reserve(discPaths.size());
                    for (int d = 1; d <= (int)discPaths.size(); d++)
                        artPaths.push_back(mediaDir + "discs/" + artTitle
                                           + "_disc" + std::to_string(d) + ".png");
                    m_inGameMenu->setDiscArtPaths(artPaths);
                }

                std::cout << "[HaackStation] Multi-disc: "
                          << discPaths.size() << " discs, on disc "
                          << (lastDisc + 1) << "\n";
            } else {
                m_inGameMenu->clearDiscInfo();
                m_inGameMenu->setCoverTexture(nullptr);
            }
        }

        setState(AppState::IN_GAME);
    }
}

void HaackApp::saveRaToken(const std::string& token) {
    m_haackSettings.raApiKey = token;
    SettingsManager mgr;
    mgr.save(m_haackSettings);
    std::cout << "[RA] Token saved to config\n";
}

// ─── applyPerGameSettings ─────────────────────────────────────────────────────
// Loads per-game config for the given game and applies any overrides on top
// of the current global settings. Called every time a game is launched.
void HaackApp::applyPerGameSettings(const std::string& gamePath,
                                     const std::string& serial) {
    if (!m_perGameSettings.load(serial, gamePath)) {
        std::cout << "[PerGame] No per-game config found — using global settings\n";
        return;
    }

    const GameOverrides& ov = m_perGameSettings.overrides();

    if (ov.overrideResolution) {
        static const char* resOpts[] = { "1x(native)", "2x", "4x", "8x", "16x" };
        int idx = std::max(0, std::min(ov.internalRes, 4));
        m_core->setCoreOption("beetle_psx_hw_internal_resolution", resOpts[idx]);
        std::cout << "[PerGame] Resolution override: " << resOpts[idx] << "\n";
    }

    if (ov.overrideRenderer) {
        // 0 = software, 1 = hardware (OpenGL)
        m_core->setCoreOption("beetle_psx_hw_renderer",
            ov.rendererChoice == 1 ? "hardware" : "software");
        std::cout << "[PerGame] Renderer override: "
                  << (ov.rendererChoice == 1 ? "hardware" : "software") << "\n";
    }

    // Audio/texture overrides are logged — full wiring requires AudioReplacer/TextureReplacer
    if (ov.overrideAudioReplace)
        std::cout << "[PerGame] Audio replacement override: "
                  << (ov.audioReplacement ? "on" : "off") << "\n";
    if (ov.overrideTextures)
        std::cout << "[PerGame] Texture replacement override: "
                  << (ov.textureReplacement ? "on" : "off") << "\n";

    std::cout << "[PerGame] Applied overrides for: " << gamePath << "\n";
}

// ─── revertPerGameSettings ────────────────────────────────────────────────────
// Re-applies global settings after a game exits so the next game starts clean.
void HaackApp::revertPerGameSettings() {
    if (!m_perGameSettings.isLoaded()) return; // nothing to revert

    // Re-apply global resolution and renderer
    static const char* resOpts[] = { "1x(native)", "2x", "4x", "8x", "16x" };
    int resIdx = std::max(0, std::min(m_haackSettings.internalRes, 4));
    if (m_core && m_core->isCoreLoaded())
        m_core->setCoreOption("beetle_psx_hw_internal_resolution", resOpts[resIdx]);

    m_perGameSettings.clear();
    std::cout << "[PerGame] Reverted to global settings\n";
}

void HaackApp::applySettings() {
    if (!m_haackSettings.romsPath.empty()) {
        m_scanner->addSearchPath(m_haackSettings.romsPath);
        m_scanner->rescan();
        m_browser->setLibrary(m_scanner->getLibrary());
        m_browser->setPlayHistory(m_playHistory.get());
    }
    if (!m_haackSettings.biosPath.empty())
        m_core->setBiosPath(m_haackSettings.biosPath);

    {
        Uint32 flags = SDL_GetWindowFlags(m_window);
        bool isFs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        if (m_haackSettings.fullscreen != isFs) {
            SDL_SetWindowFullscreen(m_window,
                m_haackSettings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            SDL_ShowCursor(m_haackSettings.fullscreen ? SDL_DISABLE : SDL_ENABLE);
        }
    }

    if (m_core && m_core->isGameLoaded())
        m_core->setCoreOption("beetle_psx_hw_skip_bios",
            m_haackSettings.fastBoot ? "enabled" : "disabled");

    static const char* resOptions[] = { "1x(native)", "2x", "4x", "8x", "16x" };
    int resIdx = m_haackSettings.internalRes;
    if (resIdx >= 0 && resIdx < 5)
        m_core->setCoreOption("beetle_psx_hw_internal_resolution", resOptions[resIdx]);

    int idx = std::max(0, std::min(m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1));
    std::cout << "[Settings] Fast forward: " << FF_MULTIPLIERS[idx] << "x\n";
    std::cout << "[Settings] Volume: " << m_haackSettings.audioVolume << "%\n";

    // Apply top row mode to browser immediately
    m_browser->setTopRowMode(m_haackSettings.topRowMode);

    // Apply RA settings live — no restart needed
    if (m_ra)
        m_ra->setAutoScreenshot(m_haackSettings.raAutoScreenshot);

    SettingsManager mgr;
    mgr.save(m_haackSettings);
    std::cout << "[Settings] Applied and saved\n";
}

void HaackApp::stopGame() {
    revertPerGameSettings(); // restore global before clearing game state
    m_fastForward     = false;
    m_rewinding       = false;
    m_turboActive     = false;   // ← reset turbo on game exit
    m_turboHeldSince  = 0;
    if (m_core) {
        m_core->setTurboRatio(1.0); // restore normal audio threshold
        // If turbo mute was active, ensure audio device is unpaused on exit
        if (m_haackSettings.turboMuteAudio) {
            SDL_AudioDeviceID audioDev = m_core->getAudioDevice();
            if (audioDev) { SDL_PauseAudioDevice(audioDev, 0); SDL_ClearQueuedAudio(audioDev); }
        }
    }

    // Record session playtime before clearing the path
    if (m_playHistory && m_sessionStartTime > 0 && !m_currentGamePath.empty()) {
        uint64_t elapsed = (uint64_t)(std::time(nullptr) - m_sessionStartTime);
        m_playHistory->recordStop(m_currentGamePath, elapsed);
        m_sessionStartTime = 0;
    }
    m_ffHeldSince     = 0;
    m_rewindHeldSince = 0;
    m_rumbleNextAt    = 0;
    m_currentGamePath.clear();

    // Reset rewind buffer so it doesn't hold stale state across games
    if (m_rewind) m_rewind->reset();

    // Always fully close/reset the in-game menu so it doesn't carry over
    if (m_inGameMenu) {
        m_inGameMenu->close();
        m_inGameMenu->clearDiscInfo();
        m_inGameMenu->setCoverTexture(nullptr);
    }
    if (m_saveStates && m_core->isGameLoaded()) {
        SDL_Surface* screenshot = m_saveStates->captureCleanScreenshot();
        m_saveStates->autoSave(screenshot);
        if (screenshot) SDL_FreeSurface(screenshot);
    }
    // Update global Trophy Hub with this game's final achievement state.
    // Use cached data — the cache was updated after load and stays valid here.
    if (m_ra && m_trophyHub) {
        const auto& achievements = m_ra->getCachedAchievements();
        if (!achievements.empty()) {
            GameTrophySummary summary;
            summary.gameId    = m_ra->cachedGameInfo().id;
            summary.gameTitle = m_ra->cachedGameInfo().title;
            summary.total     = (int)achievements.size();
            for (const auto& a : achievements) {
                if (a.unlocked) {
                    ++summary.unlocked;
                    summary.totalPoints += a.points;
                }
                summary.possiblePoints += a.points;
            }
            // Cover art — build path using the same safeName logic as game_browser
            {
                std::string safeName = summary.gameTitle;
                for (auto& c : safeName)
                    if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||
                        c=='"'||c=='<'||c=='>'||c=='|') c='_';
                std::string base = m_haackSettings.romsPath.empty()
                    ? "media/covers/" : m_haackSettings.romsPath + "/media/covers/";
                for (const char* ext : {".png", ".jpg"}) {
                    std::string p = base + safeName + ext;
                    if (fs::exists(p)) { summary.coverPath = p; break; }
                }
            }
            // Up to 5 most recent unlocked badge paths for the hub strip
            for (const auto& a : achievements) {
                if (a.unlocked && !a.badgeLocalPath.empty()) {
                    summary.recentBadgePaths.push_back(a.badgeLocalPath);
                    if ((int)summary.recentBadgePaths.size() >= 5) break;
                }
            }
            m_trophyHub->updateGame(summary);
        }
    }
    if (m_ra) m_ra->unloadGame();
    m_core->unloadGame();
    m_browser->resetAfterGame();
    setState(AppState::GAME_BROWSER);
}

void HaackApp::toggleFullscreen() {
    Uint32 flags = SDL_GetWindowFlags(m_window);
    bool isFs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    bool goingFs = !isFs;
    SDL_SetWindowFullscreen(m_window, goingFs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(goingFs ? SDL_DISABLE : SDL_ENABLE);
    m_haackSettings.fullscreen = goingFs;
}

// ─── stripRomRegion ───────────────────────────────────────────────────────────
// Strips region / revision tags from a ROM filename stem so it matches
// the clean name ScreenScraper uses for its media folders.
//
// Rules (applied left-to-right, all parenthetical/bracketed groups stripped):
//   (USA)  (Europe)  (Japan)  (World)  (En)  etc.  → removed
//   (Rev X)  (v1.0)  (Disc 1)                       → removed
//   [!]  [b]  [h]  [T+Eng]                          → removed
//   Leading/trailing whitespace trimmed after stripping.
//
// Examples:
//   "Crash Bandicoot (USA)"              → "Crash Bandicoot"
//   "Final Fantasy VII (USA) (Disc 1)"  → "Final Fantasy VII"
//   "Castlevania - Symphony of the Night (USA) (Track 1)" → "Castlevania - Symphony of the Night"
//   "Spyro the Dragon [!]"              → "Spyro the Dragon"
//
std::string HaackApp::stripRomRegion(const std::string& stem) {
    // Remove all parenthetical (...) and bracketed [...] groups
    static const std::regex tagPattern(R"(\s*[\(\[][^\)\]]*[\)\]])");
    std::string result = std::regex_replace(stem, tagPattern, "");

    // Trim leading/trailing whitespace
    auto start = result.find_first_not_of(" \t");
    auto end   = result.find_last_not_of(" \t");
    if (start == std::string::npos) return stem; // nothing left — keep original
    return result.substr(start, end - start + 1);
}

// ─── takeUIScreenshot ────────────────────────────────────────────────────────
// Captures whatever is currently on screen (menus, shelf, in-game — anything)
// and saves it to: media/screenshots/HaackStation/ui_YYYY-MM-DD_HH-MM-SS.png
// Uses the SDL renderer directly, so it captures the composited frame.
// Named with "ui_" prefix so it sorts separately from game captures.
void HaackApp::takeUIScreenshot() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                        SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        std::cerr << "[Screenshot] SDL_CreateRGBSurface failed\n";
        return;
    }

    if (SDL_RenderReadPixels(m_renderer, nullptr,
                              SDL_PIXELFORMAT_ARGB8888,
                              surf->pixels, surf->pitch) != 0) {
        std::cerr << "[Screenshot] SDL_RenderReadPixels failed: "
                  << SDL_GetError() << "\n";
        SDL_FreeSurface(surf);
        return;
    }

    // Save to media/screenshots/HaackStation/
    // Prefixed "ui_" so it floats to the bottom alphabetically in game folders
    // and is obviously distinct from game captures.
    std::string mediaDir = m_haackSettings.romsPath.empty()
        ? "media/" : m_haackSettings.romsPath + "/media/";
    std::string dir = mediaDir + "screenshots/HaackStation/";
    fs::create_directories(dir);

    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char timestamp[40];
    strftime(timestamp, sizeof(timestamp), "ui_%Y-%m-%d_%H-%M-%S", t);
    std::string path = dir + timestamp + ".png";

    if (IMG_SavePNG(surf, path.c_str()) == 0) {
        std::cout << "[Screenshot] UI screenshot saved: " << path << "\n";
        m_screenshotNotifyUntil = SDL_GetTicks() + 2000;
    } else {
        std::cerr << "[Screenshot] Failed: " << SDL_GetError() << "\n";
    }
    SDL_FreeSurface(surf);
}

// ─── parseM3uDiscs ───────────────────────────────────────────────────────────
// If path is an M3U playlist, returns the list of disc paths it contains.
// Non-M3U paths (single disc games) return a vector with just that one path.
// Paths in the M3U are resolved relative to the M3U's own directory.
std::vector<std::string> HaackApp::parseM3uDiscs(const std::string& path) {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext != ".m3u") return { path }; // single disc

    std::vector<std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return { path };

    std::string m3uDir = p.parent_path().string();
    if (!m3uDir.empty() && m3uDir.back() != '/' && m3uDir.back() != '\\')
        m3uDir += '/';

    std::string line;
    while (std::getline(f, line)) {
        // Strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip blank lines and comments
        if (line.empty() || line[0] == '#') continue;
        // Resolve relative paths
        if (line.find(':') == std::string::npos && // not absolute Windows path
            line[0] != '/' && line[0] != '\\') {
            line = m3uDir + line;
        }
        result.push_back(line);
    }
    return result.empty() ? std::vector<std::string>{ path } : result;
}

// ─── takeScreenshot ───────────────────────────────────────────────────────────
// Captures the clean game framebuffer and saves it to:
//   media/screenshots/[clean game title]/capture_YYYY-MM-DD_HH-MM-SS.png
//
// The folder name uses stripRomRegion() so it matches ScreenScraper's
// "Crash Bandicoot" folder (not "Crash Bandicoot (USA)").
void HaackApp::takeScreenshot() {
    if (!m_saveStates || !m_core->isGameLoaded()) return;

    SDL_Surface* shot = m_saveStates->captureCleanScreenshot();
    if (!shot) {
        std::cerr << "[Screenshot] captureCleanScreenshot() returned null\n";
        return;
    }

    // Derive clean title from the ROM path
    std::string title = "Unknown";
    if (!m_currentGamePath.empty()) {
        fs::path p(m_currentGamePath);
        title = stripRomRegion(p.stem().string());
    }

    // Sanitize for filesystem (strip invalid chars)
    const std::string invalid = "\\/:*?\"<>|";
    for (auto& c : title)
        if (invalid.find(c) != std::string::npos) c = '_';

    // Determine media dir (mirrors scraper logic)
    std::string mediaDir = m_haackSettings.romsPath.empty()
        ? "media/" : m_haackSettings.romsPath + "/media/";
    std::string dir = mediaDir + "screenshots/" + title + "/";
    fs::create_directories(dir);

    // Timestamp filename
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "capture_%Y-%m-%d_%H-%M-%S", t);
    std::string path = dir + timestamp + ".png";

    if (IMG_SavePNG(shot, path.c_str()) == 0) {
        std::cout << "[Screenshot] Saved: " << path << "\n";
        m_screenshotNotifyUntil = SDL_GetTicks() + 2000;
    } else {
        std::cerr << "[Screenshot] Failed to save: " << SDL_GetError() << "\n";
    }

    SDL_FreeSurface(shot);
}

void HaackApp::updateGameInput() {
    if (m_nav && SDL_GetTicks() < m_inputCooldownUntil) return;

    int mask = 0;

    // Use SDL_GameControllerFromInstanceID to get the already-open controller
    // handle from ControllerNav rather than opening a second handle with
    // SDL_GameControllerOpen. Opening it again each frame can return a
    // different internal state and makes the close at the end redundant.
    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            ctrl = SDL_GameControllerFromInstanceID(id);
            if (!ctrl) ctrl = SDL_GameControllerOpen(i); // fallback: open if needed
            break;
        }
    }
    bool ctrlOpenedHere = false; // track if WE opened it (so we close it)
    if (!ctrl) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                ctrl = SDL_GameControllerOpen(i);
                ctrlOpenedHere = true;
                break;
            }
        }
    }
    if (ctrl) {
        auto btn = [&](SDL_GameControllerButton b) {
            return SDL_GameControllerGetButton(ctrl, b) != 0;
        };
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

        // ── L2: rewind with hold delay ─────────────────────────────────────
        // L2 as a PS1 button is also needed in-game, so we treat a SHORT press
        // as the PS1 L2 button, and activate rewind only after REWIND_HOLD_DELAY_MS.
        // This mirrors how FF works on R2.
        if (l2 > 8000) {
            mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L2); // pass through always
            if (m_rewindHeldSince == 0)
                m_rewindHeldSince = SDL_GetTicks();
            if (SDL_GetTicks() - m_rewindHeldSince >= REWIND_HOLD_DELAY_MS) {
                m_rewinding = true;
                // Don't pass L2 to the game while rewinding
                mask &= ~(1 << RETRO_DEVICE_ID_JOYPAD_L2);
            }
        } else {
            m_rewinding       = false;
            m_rewindHeldSince = 0;
            // Clear rumble timer when released
            if (!m_fastForward) m_rumbleNextAt = 0;
            // Also clear backtick hold (they share the same rewind state)
            const Uint8* ks2 = SDL_GetKeyboardState(nullptr);
            if (!ks2[SDL_SCANCODE_GRAVE]) {
                m_rewinding       = false;
                m_rewindHeldSince = 0;
            }
        }

        // ── R1 + R2 combo = Turbo toggle (checked FIRST, takes priority over FF) ──
        // We must read R1 and R2 together before deciding what each does alone.
        // If BOTH are held for TURBO_HOLD_DELAY_MS, turbo toggles on or off.
        // While R1+R2 are both held, R2 does NOT activate fast-forward, and
        // R1 is NOT passed to the PS1 game — the combo is fully consumed.
        bool r1Held     = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        bool r2Held     = (r2 > 8000);
        bool turboCombo = r1Held && r2Held;

        if (turboCombo) {
            // R1+R2 held — suppress R button and fast-forward
            mask &= ~(1 << RETRO_DEVICE_ID_JOYPAD_R);
            m_fastForward = false;
            m_ffHeldSince = 0;
        } else {
            // R2 alone = fast-forward
            if (r2Held) {
                if (m_ffHeldSince == 0)
                    m_ffHeldSince = SDL_GetTicks();
                if (SDL_GetTicks() - m_ffHeldSince >= FF_HOLD_DELAY_MS)
                    m_fastForward = true;
            } else {
                const Uint8* ks2 = SDL_GetKeyboardState(nullptr);
                if (!ks2[SDL_SCANCODE_F]) {
                    m_fastForward = false;
                    m_ffHeldSince = 0;
                    if (!m_rewinding) m_rumbleNextAt = 0;
                }
            }
        }

        if (ctrlOpenedHere) SDL_GameControllerClose(ctrl);
    }

    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    // F key fast forward
    if (ks[SDL_SCANCODE_F]) {
        if (m_ffHeldSince == 0)
            m_ffHeldSince = SDL_GetTicks();
        if (SDL_GetTicks() - m_ffHeldSince >= FF_HOLD_DELAY_MS)
            m_fastForward = true;
    }

    // ── Unified turbo hold timer ──────────────────────────────────────────────
    // CRITICAL: Read BOTH inputs here before deciding whether to reset the timer.
    // The old design had two separate else-branches that each reset
    // m_turboHeldSince=0, so they fought each other every frame and the
    // timer could never accumulate. Now: one combined check.
    {
        // Re-read controller state (may have been closed above)
        bool ctrlCombo = false;
        for (int _i = 0; _i < SDL_NumJoysticks(); _i++) {
            if (!SDL_IsGameController(_i)) continue;
            SDL_GameController* tc = SDL_GameControllerFromInstanceID(
                SDL_JoystickGetDeviceInstanceID(_i));
            if (!tc) break;
            Sint16 tr2 = SDL_GameControllerGetAxis(tc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            bool   tr1 = SDL_GameControllerGetButton(
                tc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
            ctrlCombo = tr1 && (tr2 > 8000);
            break;
        }
        bool tKey   = (ks[SDL_SCANCODE_T] != 0);
        bool active = ctrlCombo || tKey;

        if (active) {
            if (m_turboHeldSince == 0)
                m_turboHeldSince = SDL_GetTicks();
            if (m_turboHeldSince != 0xFFFFFFFF &&
                SDL_GetTicks() - m_turboHeldSince >= TURBO_HOLD_DELAY_MS) {
                m_turboActive    = !m_turboActive;
                m_turboHeldSince = 0xFFFFFFFF;
                std::cout << "[Turbo] " << (m_turboActive ? "ON" : "OFF") << "\n";
                // Tell the bridge so the audio callback uses the right skip threshold
                if (m_core) {
                    int tIdx = std::max(0, std::min(m_haackSettings.turboSpeed, 4));
                    double ratio = m_turboActive ? TURBO_RATIOS[tIdx] : 1.0;
                    m_core->setTurboRatio(ratio);
                }
                // Mute / unmute audio according to the turbo mute setting
                if (m_haackSettings.turboMuteAudio && m_core) {
                    SDL_AudioDeviceID audioDev = m_core->getAudioDevice();
                    if (audioDev) {
                        SDL_PauseAudioDevice(audioDev, m_turboActive ? 1 : 0);
                        if (!m_turboActive)
                            SDL_ClearQueuedAudio(audioDev); // flush stale buffered audio
                    }
                }
            }
        } else {
            m_turboHeldSince = 0;  // no input → safe to reset
        }
    }

    // Backtick / grave = rewind
    if (ks[SDL_SCANCODE_GRAVE]) {
        if (m_rewindHeldSince == 0)
            m_rewindHeldSince = SDL_GetTicks();
        if (SDL_GetTicks() - m_rewindHeldSince >= REWIND_HOLD_DELAY_MS)
            m_rewinding = true;
    }

    if (ks[SDL_SCANCODE_X])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (ks[SDL_SCANCODE_Z])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    if (ks[SDL_SCANCODE_A])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_Y);
    if (ks[SDL_SCANCODE_S])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_X);
    if (ks[SDL_SCANCODE_Q])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L);
    if (ks[SDL_SCANCODE_W])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R);
    if (ks[SDL_SCANCODE_E])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);
    if (ks[SDL_SCANCODE_R])         mask |= (1 << RETRO_DEVICE_ID_JOYPAD_R2);
    if (ks[SDL_SCANCODE_RETURN])    mask |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (ks[SDL_SCANCODE_SPACE])     mask |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (ks[SDL_SCANCODE_UP])        mask |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (ks[SDL_SCANCODE_DOWN])      mask |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (ks[SDL_SCANCODE_LEFT])      mask |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (ks[SDL_SCANCODE_RIGHT])     mask |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);

    m_core->setButtonState(0, mask);
}
