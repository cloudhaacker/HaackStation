#pragma once
// trophy_room.h
// Full-screen Trophy Room — browse all RetroAchievements for the current game.
//
// Layout:
//
//  ┌─────────────────────────────────────────────────────────────────────┐
//  │  🏆  TROPHY ROOM                                          [B] Back  │
//  │  Crash Bandicoot (Warped)                                           │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │  [ALL]  [UNLOCKED]  [LOCKED]        23 / 48  ████████░░░░  48%     │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │                                                                     │
//  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────┐ │
//  │  │  badge   │  │  badge   │  │  🔒 grey │  │  🔒 grey │  │ ...  │ │
//  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────┘ │
//  │  Title         Title         Locked         Locked                 │
//  │  100pts        50pts         ???            ???                    │
//  │                                                                     │
//  │  ┌──────────┐  ┌──────────┐  ...  (more rows scroll up/down)      │
//  │                                                                     │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │  ┌─────────────────────────────────────────────────────────────┐   │
//  │  │ [badge 96px]  SELECTED TITLE                    100 pts     │   │
//  │  │               Achievement description text here             │   │
//  │  └─────────────────────────────────────────────────────────────┘   │
//  └─────────────────────────────────────────────────────────────────────┘
//
// Badge images: media/badges/{badge_name}.png       (unlocked, colour)
//               media/badges/{badge_name}_lock.png  (locked, greyscale)
// Both are downloaded by RAManager::fetchAllBadges() after game load.
// If only the colour version is cached, we greyscale it in software.
//
// Navigation:
//   D-pad / Left stick  — move selection cursor in grid
//   A                   — (future: show full-screen badge zoom)
//   L1 / R1             — cycle filter tabs
//   B                   — back to game details / browser

#include "ra_manager.h"
#include "theme_engine.h"
#include "controller_nav.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>

enum class TrophyFilter { ALL, UNLOCKED, LOCKED };

// One display entry in the grid — may be locked or unlocked
struct TrophyEntry {
    AchievementInfo info;
    SDL_Texture*    texture     = nullptr; // colour badge (owned here)
    SDL_Texture*    textureLock = nullptr; // greyscale / lock version (owned here)
    bool            textureLoaded = false;
};

class TrophyRoom {
public:
    TrophyRoom(SDL_Renderer* renderer, ThemeEngine* theme,
               ControllerNav* nav, RAManager* ra);
    ~TrophyRoom();

    // Call after game is loaded in RAManager — populates entries + kicks
    // off background badge downloads via RAManager::fetchAllBadges().
    void refresh();

    // Populate from an explicit achievement list — use this when no game is
    // currently running (e.g. opening from hub or details panel shelf).
    // Allows showing cached data without requiring a live RA session.
    void refreshWithData(const std::vector<AchievementInfo>& achievements);

    // Set the game title shown in the header (usually RAManager::gameInfo().title)
    void setGameTitle(const std::string& title) { m_gameTitle = title; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose() const { return m_wantsClose; }
    void resetClose()       { m_wantsClose = false; }
    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderHeader();
    void renderFilterBar();
    void renderGrid();
    void renderDetailStrip();
    void renderProgressBar(int x, int y, int w, int h,
                           float fraction, SDL_Color fill, SDL_Color bg);

    // ── Grid helpers ──────────────────────────────────────────────────────────
    const std::vector<int>& visibleIndices() const;   // indices into m_entries
    void rebuildVisible();
    void clampSelection();

    // ── Texture loading ───────────────────────────────────────────────────────
    void loadTextures();       // loads from disk for all cached badge files
    void freeTextures();
    SDL_Texture* loadBadge(const std::string& path);
    SDL_Texture* makeGreyscale(SDL_Texture* src); // software greyscale copy

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;
    RAManager*     m_ra       = nullptr;

    std::string              m_gameTitle;
    std::vector<TrophyEntry> m_entries;     // ALL achievements, always
    std::vector<int>         m_visible;     // filtered indices into m_entries
    TrophyFilter             m_filter      = TrophyFilter::ALL;
    bool                     m_dirty       = true; // rebuild m_visible next frame

    int  m_selectedIdx  = 0;   // index into m_visible
    int  m_scrollRow    = 0;   // first visible grid row (in units of rows)
    bool m_wantsClose   = false;

    int m_w = 1280;
    int m_h = 720;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int COLS          = 5;    // badges per row
    static constexpr int BADGE_SIZE    = 120;  // px — badge display size in grid
    static constexpr int BADGE_PAD     = 20;   // gap between badges
    static constexpr int LABEL_H       = 42;   // title + pts below badge
    static constexpr int CELL_H        = BADGE_SIZE + LABEL_H + BADGE_PAD;
    static constexpr int HEADER_H      = 88;   // title + divider
    static constexpr int FILTER_H      = 44;   // tab bar
    static constexpr int DETAIL_H      = 124;  // selected achievement strip
    static constexpr int MARGIN        = 48;
    static constexpr int FOOTER_EXTRA  = 8;
};
