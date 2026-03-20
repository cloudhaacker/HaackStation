#include "scrape_screen.h"
#include <iostream>

ScrapeScreen::ScrapeScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                            ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
}

ScrapeScreen::~ScrapeScreen() {}

void ScrapeScreen::startScraping(std::vector<GameEntry>& games,
                                  const std::string& mediaDir,
                                  const std::string& ssUser,
                                  const std::string& ssPassword) {
    m_done      = false;
    m_cancelled = false;
    m_progress  = ScrapeProgress{};
    m_progress.total = (int)games.size();

    // Run scraper in a background thread so UI stays responsive
    std::thread([this, &games, mediaDir, ssUser, ssPassword]() {
        GameScraper scraper;
        scraper.setMediaDir(mediaDir);
        if (!ssUser.empty())
            scraper.setCredentials(ssUser, ssPassword);

        scraper.scrapeLibrary(games, [this](const ScrapeProgress& p) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_progress = p;
            return !m_cancelled;  // Return false to cancel
        });

        std::lock_guard<std::mutex> lock(m_mutex);
        m_done = true;
    }).detach();
}

void ScrapeScreen::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
            e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            m_cancelled = true;
        }
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        m_cancelled = true;
    }
}

void ScrapeScreen::update(float deltaMs) {
    m_spinAngle += 3.f * (deltaMs / 1000.f);
    if (m_spinAngle > 6.28f) m_spinAngle -= 6.28f;

    SDL_GetRendererOutputSize(m_renderer, &m_windowW, &m_windowH);
}

void ScrapeScreen::render() {
    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    m_theme->drawHeader(m_windowW, m_windowH, "Scraping Game Art", "", 0);

    ScrapeProgress prog;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        prog = m_progress;
    }

    int cx = m_windowW / 2;
    int cy = m_windowH / 2;

    if (m_done) {
        m_theme->drawTextCentered("Scraping complete!", cx, cy - 40,
            pal.multiDisc, FontSize::TITLE);
        std::string summary = std::to_string(prog.succeeded) + " scraped, " +
                              std::to_string(prog.skipped)   + " skipped, " +
                              std::to_string(prog.failed)    + " failed";
        m_theme->drawTextCentered(summary, cx, cy, pal.textSecond, FontSize::BODY);
        m_theme->drawTextCentered("Press B to return", cx, cy + 40,
            pal.textDisable, FontSize::SMALL);
        return;
    }

    if (m_cancelled) {
        m_theme->drawTextCentered("Cancelled", cx, cy - 20,
            pal.accent, FontSize::TITLE);
        m_theme->drawTextCentered("Press B to return", cx, cy + 20,
            pal.textDisable, FontSize::SMALL);
        return;
    }

    // Spinner
    m_theme->drawLoadingSpinner(cx, cy - 60, m_spinAngle, pal.accent);

    // Current game
    m_theme->drawTextCentered("Scraping: " + prog.currentGame,
        cx, cy - 20, pal.textPrimary, FontSize::BODY);

    // Progress bar
    float pct = prog.total > 0 ? (float)prog.done / prog.total : 0.f;
    int barW = m_windowW / 2;
    int barX = cx - barW / 2;
    int barY = cy + 20;

    // Track
    SDL_Rect track = { barX, barY, barW, 8 };
    m_theme->drawRect(track, pal.bgCard);

    // Fill
    int fillW = (int)(barW * pct);
    if (fillW > 0) {
        SDL_Rect fill = { barX, barY, fillW, 8 };
        m_theme->drawRect(fill, pal.accent);
    }

    // Count
    std::string countStr = std::to_string(prog.done) + " / " +
                           std::to_string(prog.total);
    m_theme->drawTextCentered(countStr, cx, barY + 20,
        pal.textSecond, FontSize::SMALL);

    // Stats
    std::string stats = std::to_string(prog.succeeded) + " done  " +
                        std::to_string(prog.skipped)   + " skipped  " +
                        std::to_string(prog.failed)    + " failed";
    m_theme->drawTextCentered(stats, cx, barY + 44,
        pal.textDisable, FontSize::TINY);

    // Cancel hint
    m_theme->drawFooterHints(m_windowW, m_windowH, "", "Cancel");
}

void ScrapeScreen::renderProgressBar(int x, int y, int w, int h,
                                      float progress, SDL_Color fill) {
    SDL_Rect track = { x, y, w, h };
    m_theme->drawRect(track, m_theme->palette().bgCard);
    int fw = (int)(w * progress);
    if (fw > 0) {
        SDL_Rect bar = { x, y, fw, h };
        m_theme->drawRect(bar, fill);
    }
}
