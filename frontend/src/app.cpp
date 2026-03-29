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
#include "splash_screen.h"
#include "libretro_bridge.h"
#include "libretro_types.h"
#include "game_scanner.h"
#include "theme_engine.h"
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
#include <filesystem>
namespace fs = std::filesystem;

static constexpr int DEFAULT_W = 1280;
static constexpr int DEFAULT_H = 720;

// Fast forward multiplier table — index matches fastForwardSpeed setting
// 0=2x  1=4x  2=6x  3=8x
static constexpr int FF_MULTIPLIERS[] = { 2, 4, 6, 8 };
static constexpr int FF_TABLE_SIZE    = 4;

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

    // Play history — load from disk and pass to browser
    m_playHistory = std::make_unique<PlayHistory>();
    m_playHistory->load();
    m_ra->setRenderer(m_renderer);
    m_ra->setTheme(m_theme.get());
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
    m_browser->setTopRowMode(static_cast<TopRowMode>(m_haackSettings.topRowMode));
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
              << "           F=Fast Forward (hold)  F1=In-game menu  Esc=Quit to shelf\n"
              << "  GLOBAL:  F11=Fullscreen\n"
              << "[HaackStation] Fast forward: " << ffMult << "x (hold R2 or F)\n";
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
                    if (m_fastForward) {
                        // Run N frames, mute audio to avoid queue overflow
                        int idx  = std::max(0, std::min(
                            m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1));
                        int mult = FF_MULTIPLIERS[idx];
                        SDL_PauseAudioDevice(0, 1);
                        for (int ff = 0; ff < mult; ++ff) {
                            m_core->runFrame();
                            if (m_ra && m_ra->isGameLoaded())
                                m_ra->doFrame(m_core.get());
                        }
                        SDL_PauseAudioDevice(0, 0);
                    } else {
                        m_core->runFrame();
                        if (m_ra && m_ra->isGameLoaded())
                            m_ra->doFrame(m_core.get());
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
                }
                break;

            case SDL_KEYDOWN: {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_F11) { toggleFullscreen(); continue; }

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
                    if (key == SDLK_f) {
                        // Start hold timer — FF activates after FF_HOLD_DELAY_MS
                        if (m_ffHeldSince == 0)
                            m_ffHeldSince = SDL_GetTicks();
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
                    if (key == SDLK_F2) {
                        auto* game = m_browser->selectedGameEntry();
                        if (game && m_details && !m_details->isOpen()) {
                            m_details->open(*game, m_saveStates.get());
                            int idx = m_browser->selectedIndex();
                            if (idx >= 0)
                                m_details->setCoverTexture(m_browser->getCoverArt(idx));
                        }
                        continue;
                    }
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
                }
                break;

            case SDL_CONTROLLERDEVICEADDED:
                m_nav->onControllerAdded(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                m_nav->onControllerRemoved(e.cdevice.which);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
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
                if (m_state == AppState::IN_GAME &&
                    e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
                    SDL_GameController* ctrl = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(0));
                    if (ctrl && SDL_GameControllerGetButton(
                            ctrl, SDL_CONTROLLER_BUTTON_START)) {
                        if (m_inGameMenu->isOpen()) m_inGameMenu->close();
                        else                        m_inGameMenu->open();
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
            m_details->handleEvent(e);
        } else if (m_state == AppState::IN_GAME && m_inGameMenu->isOpen()) {
            m_inGameMenu->handleEvent(e);
        } else {
            switch (m_state) {
                case AppState::GAME_BROWSER: m_browser->handleEvent(e);  break;
                case AppState::SETTINGS:     m_settings->handleEvent(e); break;
                case AppState::SCRAPING:     m_scraper->handleEvent(e);  break;
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
    } else if (action == InGameMenuAction::SAVE_STATE) {
        int slot = m_inGameMenu->selectedSlot();
        SDL_Surface* shot = m_saveStates->captureCleanScreenshot();
        m_saveStates->saveState(slot, shot);
        if (shot) SDL_FreeSurface(shot);
        m_inGameMenu->close();
    } else if (action == InGameMenuAction::LOAD_STATE) {
        int slot = m_inGameMenu->selectedSlot();
        auto slots = m_saveStates->listSlots();
        if (slot < (int)slots.size())
            m_saveStates->loadState(slots[slot].slotNumber);
        m_inGameMenu->close();
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
            m_browser->update(deltaMs);
            if (m_browser->hasPendingLaunch())
                launchGame(m_browser->consumeLaunchPath());
            if (m_browser->wantsDetails() && m_details && !m_details->isOpen()) {
                m_browser->clearWantsDetails();
                auto* game = m_browser->selectedGameEntry();
                if (game) {
                    m_details->open(*game, m_saveStates.get());
                    int idx = m_browser->selectedIndex();
                    if (idx >= 0)
                        m_details->setCoverTexture(m_browser->getCoverArt(idx));
                }
            } else if (m_browser->wantsDetails()) {
                m_browser->clearWantsDetails();
            }
            if (m_details && m_details->isOpen()) {
                m_details->update(deltaMs);
                auto act = m_details->pendingAction();
                if (act == DetailsPanelAction::CLOSE) {
                    m_details->clearAction();
                } else if (act != DetailsPanelAction::NONE) {
                    m_details->clearAction();
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
            } else if (m_settings->wantsClose()) {
                applySettings();
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
            if (m_details && m_details->isOpen()) m_details->render();
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
            break;
        case AppState::SETTINGS:     m_settings->render(); break;
        case AppState::SCRAPING:     m_scraper->render();  break;
        default: break;
    }
    SDL_RenderPresent(m_renderer);
}

void HaackApp::renderFastForwardIndicator() {
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);

    int idx  = std::max(0, std::min(m_haackSettings.fastForwardSpeed, FF_TABLE_SIZE - 1));
    int mult = FF_MULTIPLIERS[idx];
    std::string label = ">>" + std::to_string(mult) + "x";

    int badgeW = 64, badgeH = 28;
    int badgeX = w - badgeW - 12;
    int badgeY = 12;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 20, 20, 40, 200);
    SDL_Rect bg = { badgeX, badgeY, badgeW, badgeH };
    SDL_RenderFillRect(m_renderer, &bg);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
    SDL_RenderDrawRect(m_renderer, &bg);

    m_theme->drawTextCentered(label,
        badgeX + badgeW / 2, badgeY + (badgeH - 14) / 2,
        pal.accent, FontSize::SMALL);
}

void HaackApp::setState(AppState next) { m_state = next; }

void HaackApp::launchGame(const std::string& path) {
    std::cout << "[HaackStation] Launching: " << path << "\n";
    if (!m_haackSettings.biosPath.empty())
        m_core->setBiosPath(m_haackSettings.biosPath);

    // ── Fast Boot fix ─────────────────────────────────────────────────────────
    // beetle_psx_hw_skip_bios must be in m_coreOptions BEFORE retro_init()
    // fires. On first launch the core isn't loaded yet, so loadGame() would
    // call loadCore() internally — which calls retro_init() before we ever
    // get to set the option. Fix: explicitly load the core here first so our
    // setCoreOption call happens before retro_init(), every time.
    if (!m_core->isCoreLoaded()) {
        std::string corePath = LibretroBridge::defaultCorePath();
        if (!m_core->loadCore(corePath)) {
            std::cerr << "[HaackStation] Failed to load core: " << corePath << "\n";
            m_browser->resetAfterGame();
            setState(AppState::GAME_BROWSER);
            return;
        }
    }

    // Now set fast boot — core is loaded, option will be read at game load time
    m_core->setCoreOption("beetle_psx_hw_skip_bios",
        m_haackSettings.fastBoot ? "enabled" : "disabled");

    if (!m_core->loadGame(path)) {
        std::cerr << "[HaackStation] Failed to load game.\n";
        std::cerr << "  Ensure BIOS files are in: bios/\n";
        m_browser->resetAfterGame();
        setState(AppState::GAME_BROWSER);
    } else {
        fs::path p(path);
        std::string title = p.stem().string();
        m_saveStates->setCurrentGame(title, path);
        // Record in play history (moves to front if already present)
        if (m_playHistory)
            m_playHistory->recordPlay(path, title);
        if (m_ra && m_ra->isLoggedIn())
            m_ra->loadGame(path, m_core.get());
        setState(AppState::IN_GAME);
    }
}

void HaackApp::saveRaToken(const std::string& token) {
    m_haackSettings.raApiKey = token;
    SettingsManager mgr;
    mgr.save(m_haackSettings);
    std::cout << "[RA] Token saved to config\n";
}

void HaackApp::applySettings() {
    if (!m_haackSettings.romsPath.empty()) {
        m_scanner->addSearchPath(m_haackSettings.romsPath);
        m_scanner->rescan();
        m_browser->setLibrary(m_scanner->getLibrary());
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
    m_browser->setTopRowMode(static_cast<TopRowMode>(m_haackSettings.topRowMode));

    SettingsManager mgr;
    mgr.save(m_haackSettings);
    std::cout << "[Settings] Applied and saved\n";
}

void HaackApp::stopGame() {
    m_fastForward = false;
    if (m_saveStates && m_core->isGameLoaded()) {
        SDL_Surface* screenshot = m_saveStates->captureCleanScreenshot();
        m_saveStates->autoSave(screenshot);
        if (screenshot) SDL_FreeSurface(screenshot);
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

void HaackApp::updateGameInput() {
    if (m_nav && SDL_GetTicks() < m_inputCooldownUntil) return;

    int mask = 0;

    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            ctrl = SDL_GameControllerOpen(i);
            break;
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
        if (l2 > 8000) mask |= (1 << RETRO_DEVICE_ID_JOYPAD_L2);

        // R2 = fast forward with hold delay — prevents accidental triggers.
        // Track how long R2 has been held; only activate after FF_HOLD_DELAY_MS.
        if (r2 > 8000) {
            if (m_ffHeldSince == 0)
                m_ffHeldSince = SDL_GetTicks();
            if (SDL_GetTicks() - m_ffHeldSince >= FF_HOLD_DELAY_MS)
                m_fastForward = true;
        } else {
            // R2 released — clear FF only if F key also not held
            const Uint8* ks2 = SDL_GetKeyboardState(nullptr);
            if (!ks2[SDL_SCANCODE_F]) {
                m_fastForward = false;
                m_ffHeldSince = 0;
            }
        }

        SDL_GameControllerClose(ctrl);
    }

    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    // F key fast forward — activate only after hold threshold
    if (ks[SDL_SCANCODE_F]) {
        if (m_ffHeldSince == 0)
            m_ffHeldSince = SDL_GetTicks();
        if (SDL_GetTicks() - m_ffHeldSince >= FF_HOLD_DELAY_MS)
            m_fastForward = true;
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
