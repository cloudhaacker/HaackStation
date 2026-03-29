#pragma once
// game_browser.h
// The main game shelf — displays the library as a scrollable card grid.
// Also renders the optional "top row" (Recently Played or Favorites)
// above the main grid when enabled in settings.
//
// Top row layout:
//   ┌──────────────────────────────────────────────────────────────┐
//   │  Recently Played  ›  [Card] [Card] [Card] [Card] [Card]     │  ← topRowMode=0
//   ├──────────────────────────────────────────────────────────────┤
//   │  [Main grid scrolls here]                                    │
//   └──────────────────────────────────────────────────────────────┘
//
// Navigation:
//   - When top row is active and cursor is in main grid:
//     UP from row 0 → moves cursor into top row
//   - When cursor is in top row:
//     DOWN → returns to main grid row 0
//     LEFT/RIGHT → scrolls within top row
//     CONFIRM → launches game
//     OPTIONS → opens details panel

#include "controller_nav.h"
#include "theme_engine.h"
#include "game_scanner.h"
#include "play_history.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <unordered_map>

enum class BrowserState {
    SCANNING,
    BROWSING,
    EMPTY,
    LAUNCHING,
};

// Which row is shown above the main grid
enum class TopRowMode {
    RECENTLY_PLAYED = 0,
    FAVORITES       = 1,
    NONE            = 2,
};

class GameBrowser {
public:
    GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~GameBrowser();

    void setLibrary(const std::vector<GameEntry>& games);
    void setPlayHistory(const PlayHistory* history) { m_playHistory = history; }
    void setTopRowMode(TopRowMode mode) { m_topRowMode = mode; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    void onWindowResize(int w, int h);

    // Launch
    bool        hasPendingLaunch()  const { return m_pendingLaunch; }
    std::string consumeLaunchPath();

    // Details panel
    bool  wantsDetails()     const { return m_wantsDetails; }
    void  clearWantsDetails()      { m_wantsDetails = false; }

    // Selected game access
    const GameEntry* selectedGameEntry() const;
    int              selectedIndex()     const;

    // Cover art — loads lazily and caches
    SDL_Texture* getCoverArt(int gameIndex);
    void         clearCoverArtCache();

    void resetAfterGame();

private:
    void moveSelection(NavAction action);
    void ensureSelectionVisible();

    // Render helpers
    void renderGrid();
    void renderTopRow();
    void renderEmptyState();
    void renderScanningState();

    // Card geometry
    SDL_Rect cardRect(int row, int col) const;
    int      totalRows()    const;
    int      visibleRows()  const;
    bool     selectionValid() const;
    const GameEntry* selectedGame() const;

    // Top row helpers
    int  topRowCardCount()   const;   // How many cards fit in one row
    bool topRowHasContent()  const;   // Whether the top row has anything to show
    int  topRowHeight()      const;   // Pixel height of the top row section
    SDL_Texture* topRowCoverArt(int topRowIndex);

    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;

    std::vector<GameEntry>  m_games;
    BrowserState            m_state       = BrowserState::EMPTY;
    TopRowMode              m_topRowMode  = TopRowMode::RECENTLY_PLAYED;
    const PlayHistory*      m_playHistory = nullptr;

    // Main grid selection
    int   m_selectedRow = 0;
    int   m_selectedCol = 0;

    // Top row selection — separate cursor, only active when m_inTopRow=true
    bool  m_inTopRow       = false;
    int   m_topRowSelected = 0;

    // Scroll
    float m_scrollOffset   = 0.f;
    float m_scrollTarget   = 0.f;
    float m_scrollVelocity = 0.f;

    // Animations
    float m_selectionAnim    = 1.f;
    bool  m_selectionChanged = false;
    float m_spinnerAngle     = 0.f;
    float m_launchAnim       = 0.f;

    // Launch
    bool        m_pendingLaunch = false;
    std::string m_launchPath;

    // Details panel signal
    bool m_wantsDetails = false;

    // Cover art cache — keyed by game index in m_games
    std::unordered_map<int, SDL_Texture*> m_coverArtCache;

    // Top row cover art cache — keyed by position in recently played list
    std::unordered_map<int, SDL_Texture*> m_topRowCoverCache;

    int m_windowW = 1280;
    int m_windowH = 720;

    // Top row visual constants
    static constexpr int TOP_ROW_H        = 110;  // Height of the entire top row section
    static constexpr int TOP_ROW_CARD_H   = 80;   // Card height in top row
    static constexpr int TOP_ROW_LABEL_H  = 20;   // "Recently Played" label height
    static constexpr int TOP_ROW_PAD      = 8;    // Padding between top row cards
};
