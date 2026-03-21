#pragma once
// scrape_screen.h
// Progress screen shown while scraping game metadata.
// Accessible from Settings > General > "Scrape Game Art"
// Shows: current game, progress bar, count, cancel button

#include "theme_engine.h"
#include "controller_nav.h"
#include "game_scraper.h"
#include "game_scanner.h"
#include <SDL2/SDL.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

class ScrapeScreen {
public:
    ScrapeScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                 ControllerNav* nav);
    ~ScrapeScreen();

    // Start scraping the given library
    void startScraping(std::vector<GameEntry>& games,
                       const std::string& mediaDir,
                       const std::string& ssUser = "",
                       const std::string& ssPassword = "",
                       const std::string& devId = "",
                       const std::string& devPassword = "");

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool isDone()       const { return m_done; }
    bool wasCancelled() const { return m_cancelled; }

private:
    void renderProgressBar(int x, int y, int w, int h, float progress,
                            SDL_Color fill);

    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;

    // Scraping state (updated from scraper thread)
    std::mutex        m_mutex;
    ScrapeProgress    m_progress;
    bool              m_done      = false;
    bool              m_cancelled = false;

    // Spinner animation
    float m_spinAngle = 0.f;

    int m_windowW = 1280;
    int m_windowH = 720;
};
