#pragma once
// game_browser.h
// The main game shelf — displays the library as a scrollable card grid.
//
// SHELF-FLIP NAVIGATION (PS4/PS5 style):
//   L1 / R1 (or Page Up / Page Down on keyboard) cycles between shelves:
//
//     ◀  [ All Games ]  [ Recently Played ]  [ Favorites ]  ▶
//
//   Each shelf is a full-screen card grid — same spring scroll, same cover art,
//   same Y→Details, same launch behavior. The header label and dot indicator
//   change to show which shelf is active.
//
//   "Favorites" is a stub — shows a friendly empty state until Phase 4.
//   "Recently Played" shows games in reverse play order from PlayHistory.
//   "All Games" shows the full alphabetical library.

#include "controller_nav.h"
#include "theme_engine.h"
#include "game_scanner.h"
#include "play_history.h"
#include "favorites.h"
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

enum class ShelfMode {
    ALL_GAMES       = 0,
    RECENTLY_PLAYED = 1,
    FAVORITES       = 2,
};

class GameBrowser {
public:
    GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~GameBrowser();

    void setLibrary(const std::vector<GameEntry>& games);
    void setPlayHistory(const PlayHistory* history) { m_playHistory = history; }
    void setFavoriteManager(FavoriteManager* favs) { m_favorites = favs; }

    // Returns true when the player pressed Y to toggle a favorite
    bool wantsFavoriteToggle() const { return m_wantsFavoriteToggle; }
    void clearFavoriteToggle()       { m_wantsFavoriteToggle = false; }

    // Which shelf to start on: 0=All Games  1=Recently Played  2=Favorites
    void setShelfMode(int mode) {
        m_shelfMode = static_cast<ShelfMode>(std::max(0, std::min(mode, 2)));
        rebuildActiveList();
    }
    // Legacy compat — settings still uses setTopRowMode name
    void setTopRowMode(int mode) { setShelfMode(mode); }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    void onWindowResize(int w, int h);

    bool        hasPendingLaunch() const { return m_pendingLaunch; }
    std::string consumeLaunchPath();

    bool wantsDetails()    const { return m_wantsDetails; }
    void clearWantsDetails()     { m_wantsDetails = false; }

    const GameEntry* selectedGameEntry() const;
    int              selectedIndex()     const;

    // Cover art — keyed by index in m_allGames
    SDL_Texture* getCoverArt(int allGamesIndex);
    // Cover art by game path — safe to call regardless of active shelf
    SDL_Texture* getCoverArtForGame(const std::string& path);
    void         clearCoverArtCache();

    void resetAfterGame();

private:
    void moveSelection(NavAction action, bool isRepeat = false);
    void ensureSelectionVisible();
    void cycleShelf(int direction);  // +1=right, -1=left
    void rebuildActiveList();        // Rebuild m_activeGames for current shelf

    void renderGrid();
    void renderShelfIndicator();
    void renderEmptyState();
    void renderEmptyShelf();         // For Recently Played / Favorites when empty
    void renderScanningState();

    SDL_Rect cardRect(int row, int col) const;
    int  totalRows()    const;
    int  visibleRows()  const;
    bool selectionValid() const;
    const GameEntry* selectedGame() const;

    // Cover art for active list entry — maps through m_activeToAllIndex
    SDL_Texture* activeCoverArt(int activeIndex);

    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;

    std::vector<GameEntry> m_allGames;      // Full library
    std::vector<GameEntry> m_activeGames;   // Current shelf's list
    std::vector<int>       m_activeToAllIndex; // Maps activeGames[i] → allGames index

    BrowserState m_state     = BrowserState::EMPTY;
    ShelfMode    m_shelfMode = ShelfMode::ALL_GAMES;

    const PlayHistory*  m_playHistory         = nullptr;
    FavoriteManager*   m_favorites            = nullptr;
    bool               m_wantsFavoriteToggle  = false;

    int   m_selectedRow = 0;
    int   m_selectedCol = 0;

    float m_scrollOffset   = 0.f;
    float m_scrollTarget   = 0.f;
    float m_scrollVelocity = 0.f;

    float m_selectionAnim    = 1.f;
    bool  m_selectionChanged = false;
    float m_spinnerAngle     = 0.f;
    float m_launchAnim       = 0.f;

    bool        m_pendingLaunch = false;
    std::string m_launchPath;

    bool m_wantsDetails = false;

    std::unordered_map<int, SDL_Texture*> m_coverArtCache;

    int m_windowW = 1280;
    int m_windowH = 720;

    static constexpr int NUM_SHELVES = 3;
};
