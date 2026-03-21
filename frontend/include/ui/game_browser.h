#pragma once
// game_browser.h
// The main game shelf screen — the first thing the user sees.
//
// Layout:
//   [Header bar — "HaackStation" + game count         ]
//   [                                                  ]
//   [  [Card][Card][Card][Card][Card][Card]             ]
//   [  [Card][Card][Card][Card][Card][Card]             ]
//   [  [Card][Card]...                                  ]
//   [                                                  ]
//   [Footer bar — A:Launch  B:Back  Y:Options  scrollbar]
//
// Navigation:
//   Left/Right — move between cards in a row
//   Up/Down    — move between rows
//   L1/R1      — jump one full row at a time (fast scroll)
//   A/Confirm  — launch the selected game
//   Y/Options  — open per-game options menu
//   Start/Menu — open main menu / settings

#include "game_scanner.h"
#include "controller_nav.h"
#include "theme_engine.h"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <memory>

class GameBrowser {
public:
    GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~GameBrowser();

    // Set the game library (from scanner)
    void setLibrary(const std::vector<GameEntry>& games);

    // Main loop interface
    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    // ── Launch pending ─────────────────────────────────────────────────────────
    // app.cpp polls these after each update()
    bool        hasPendingLaunch()   const { return m_pendingLaunch; }
    void        resetAfterGame();          // Call when returning from in-game
    void        clearCoverArtCache();      // Force reload of cover art textures
    std::string consumeLaunchPath();       // Returns path and clears flag

    // Window resize notification
    void onWindowResize(int w, int h);

private:
    // Navigation helpers
    void        moveSelection(NavAction action);
    int         selectedIndex() const { return m_selectedRow * m_theme->layout().cardsPerRow + m_selectedCol; }
    bool        selectionValid() const;
    const GameEntry* selectedGame() const;

    // Scrolling
    void        ensureSelectionVisible();
    int         totalRows() const;
    int         visibleRows() const;

    // Rendering helpers
    void        renderGrid();
    void        renderEmptyState();
    void        renderScanningState();
    SDL_Rect    cardRect(int row, int col) const;

    // Cover art loading (lazy, async-friendly)
    SDL_Texture* getCoverArt(int gameIndex);
    void         loadCoverArtAsync(int gameIndex);

    // State
    enum class BrowserState {
        SCANNING,    // Scanner is running
        EMPTY,       // No games found
        BROWSING,    // Normal browsing
        LAUNCHING,   // Brief launch animation before handing off
    };

    BrowserState m_state = BrowserState::SCANNING;

    // Game library
    std::vector<GameEntry>    m_games;

    // Selection
    int m_selectedRow = 0;
    int m_selectedCol = 0;

    // Scroll offset (in rows, can be fractional for smooth scroll)
    float m_scrollOffset     = 0.f;
    float m_scrollTarget     = 0.f;
    float m_scrollVelocity   = 0.f;

    // Selection animation (0..1, animates on selection change)
    float m_selectionAnim    = 1.f;
    bool  m_selectionChanged = false;

    // Launch animation
    float m_launchAnim       = 0.f;
    bool  m_pendingLaunch    = false;
    std::string m_launchPath;

    // Loading spinner angle (for scanning state)
    float m_spinnerAngle     = 0.f;

    // Window dimensions
    int m_windowW = 1280;
    int m_windowH = 720;

    // Cover art texture cache (index -> texture)
    std::unordered_map<int, SDL_Texture*> m_coverArtCache;

    // Owned references (not owned, just borrowed)
    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;
};
