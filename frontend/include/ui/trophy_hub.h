#pragma once
// trophy_hub.h
// Global Trophy Hub — shows RetroAchievements progress across ALL games
// that have been played with RA enabled.
//
// Layout:
//
//  ┌─────────────────────────────────────────────────────────────────────┐
//  │  🏆  TROPHY HUB                    Total: 143 / 891    [B] Back    │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │  ████████████████░░░░░░░░░░░░░░  16%  across 12 games              │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │                                                                     │
//  │  ┌───────────────────────────────────────────────────────────────┐  │
//  │  │ [cover] CRASH BANDICOOT WARPED          23/48  ████████░░ 48% │  │
//  │  │         🏅🏅🏅🏅🏅  (recent 5 unlocked badges)               │  │
//  │  └───────────────────────────────────────────────────────────────┘  │
//  │  ┌───────────────────────────────────────────────────────────────┐  │
//  │  │ [cover] SPYRO THE DRAGON               12/40  ████░░░░░░ 30%  │  │
//  │  │         🏅🏅🏅  (3 unlocked)                                   │  │
//  │  └───────────────────────────────────────────────────────────────┘  │
//  │  ...scrollable...                                                   │
//  │                                                                     │
//  │  [A] View Trophies                                                  │
//  └─────────────────────────────────────────────────────────────────────┘
//
// Data sources:
//   - Per-game achievement data comes from saves/trophy_hub.json, which
//     is written by RAManager::saveGameSummary() after each game session.
//   - Badge images come from media/badges/ (already downloaded by RA).
//   - Cover art is loaded from media/{game_title}/cover.* (same as browser).
//
// Navigation:
//   UP / DOWN      — scroll game rows
//   A / CONFIRM    — open that game's individual TrophyRoom
//   B / BACK       — return to previous screen

#pragma once
#include "theme_engine.h"
#include "controller_nav.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <functional>

// ─── Persisted per-game trophy summary ────────────────────────────────────────
// Loaded from saves/trophy_hub.json. Written by RAManager after each session.
struct GameTrophySummary {
    uint32_t    gameId          = 0;
    std::string gameTitle;
    std::string coverPath;      // local path to cover art, may be empty
    int         unlocked        = 0;
    int         total           = 0;
    uint32_t    totalPoints     = 0;
    uint32_t    possiblePoints  = 0;

    // Up to 5 most recently unlocked badge paths (for the badge strip)
    std::vector<std::string> recentBadgePaths;

    // Loaded textures (not serialised)
    SDL_Texture* coverTex       = nullptr;
    std::vector<SDL_Texture*> badgeTextures;
};

class TrophyHub {
public:
    TrophyHub(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~TrophyHub();

    // Load summary data from saves/trophy_hub.json.
    // Call before opening the screen.
    void refresh();

    // Update or insert a game's summary (call from RAManager after session ends).
    // Saves to disk immediately.
    void updateGame(const GameTrophySummary& summary);

    // Set a callback invoked when user presses A on a row —
    // provides the gameId so app.cpp can open TrophyRoom for that game.
    void setOnViewGame(std::function<void(uint32_t gameId, const std::string& title)> cb) {
        m_onViewGame = cb;
    }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose() const { return m_wantsClose; }
    void resetClose()       { m_wantsClose = false; }
    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    void loadTextures();
    void freeTextures();
    void renderHeader();
    void renderGlobalBar();
    void renderRows();
    void renderRow(const GameTrophySummary& g, int x, int y, int rowW, bool selected);
    void renderProgressBar(int x, int y, int w, int h,
                           float fraction, SDL_Color fill, SDL_Color bg);
    void clampScroll();

    // Persistence
    void loadFromDisk();
    void saveToDisk() const;

    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;

    std::vector<GameTrophySummary> m_games;
    int  m_selectedIdx = 0;
    int  m_scrollOffset = 0;   // pixel scroll offset
    bool m_wantsClose   = false;

    std::function<void(uint32_t, const std::string&)> m_onViewGame;

    int m_w = 1280;
    int m_h = 720;

    static constexpr int ROW_H      = 110;  // height of each game row
    static constexpr int ROW_PAD    = 10;   // vertical gap between rows
    static constexpr int HEADER_H   = 120;  // title + global progress bar
    static constexpr int MARGIN     = 48;
    static constexpr int COVER_W    = 72;
    static constexpr int COVER_H    = 90;
    static constexpr int BADGE_SIZE = 36;
    static constexpr const char* SAVE_PATH = "saves/trophy_hub.json";
};
