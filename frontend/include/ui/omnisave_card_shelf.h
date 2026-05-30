#pragma once
// omnisave_card_shelf.h
// Global save browser — shows a grid of every game that has save data on disk.
// Accessible from the HaackStation Hub (OmniSave tile) when no game is running.
// Selecting a game tile opens OmniSaveVault for that game in BROWSE mode.
//
// Discovery strategy:
//   PRIMARY — cross-reference the scanner library so titles, serials, and
//             multi-disc deduplication are all handled consistently.
//   FALLBACK — orphaned saves/states/ folders (game removed from library but
//             saves still on disk) appear with their raw folder name.
//
// Layout: 5-column grid of square cover-art tiles. Each tile shows cover art
// (or disc placeholder), game title, save state count badge, card count badge.

#include "theme_engine.h"
#include "controller_nav.h"
#include "library/game_scanner.h"   // for GameEntry
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <set>

// ─── One entry per game that has any save data ────────────────────────────────
struct ShelfEntry {
    std::string title;          // Display name (region tags stripped)
    std::string serial;         // e.g. SLUS-00553 — used to open Vault + find cards
    std::string folderName;     // Raw saves/states/ folder name — for setCurrentGame()
    std::string coverArtPath;   // Path to scraped cover PNG/JPG (may be empty)
    int         stateCount = 0;
    int         cardCount  = 0;
    bool        fromLibrary = false; // false = orphaned (not in scanner library)

    SDL_Texture* coverTex = nullptr;
};

class OmniSaveCardShelf {
public:
    OmniSaveCardShelf(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~OmniSaveCardShelf();

    // Call once before displaying — rescans disk each time so data is fresh.
    void open();

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose() const { return m_wantsClose; }
    void resetClose()       { m_wantsClose = false; }

    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

    // Provide the scanner library so the shelf can resolve serials and
    // deduplicate multi-disc games. Call before open().
    void setLibrary(const std::vector<GameEntry>* library) { m_library = library; }

    // Cover art base directory — matches the path used by GameBrowser.
    void setCoverArtDir(const std::string& dir) { m_coverArtDir = dir; }

    // Non-empty when the user confirmed a game. App opens OmniSaveVault,
    // then calls consumePendingOpen().
    struct PendingOpen {
        std::string title;       // stripped display title (for Vault header)
        std::string serial;      // for memory card lookup
        std::string folderName;  // raw folder name for SaveStateManager::setCurrentGame()
    };
    bool        hasPendingOpen()  const { return m_pendingOpen.has_value(); }
    PendingOpen consumePendingOpen()    { auto p = *m_pendingOpen; m_pendingOpen.reset(); return p; }

private:
    // ── Data ─────────────────────────────────────────────────────────────────
    void scanSaveData();
    void loadCoverTextures();
    void freeCoverTextures();

    // Strip region/revision tags — mirrors HaackApp::stripRomRegion().
    static std::string stripRegion(const std::string& stem);

    // Count .state files (excluding .undo) in a save state folder.
    int countStateSlots(const std::string& folderName) const;

    // Count .mcr files in memcards/per_game/ for a given serial.
    int countCardSlots(const std::string& serial) const;

    // Try common cover art filename conventions.
    std::string findCoverArt(const std::string& title,
                             const std::string& serial) const;

    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderHeader();
    void renderGrid();
    void renderTile(const ShelfEntry& entry, int x, int y, int w, int h, bool selected);
    void renderPlaceholder(int x, int y, int w, int h);
    void renderFooter();

    // ── Members ───────────────────────────────────────────────────────────────
    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;

    const std::vector<GameEntry>* m_library = nullptr;

    std::vector<ShelfEntry> m_entries;
    int m_sel    = 0;
    int m_scroll = 0;

    std::string m_coverArtDir = "media/covers/";

    std::optional<PendingOpen> m_pendingOpen;
    bool m_wantsClose = false;

    int m_w = 1280;
    int m_h = 720;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int HEADER_H    = 64;
    static constexpr int FOOTER_H    = 48;
    static constexpr int GRID_COLS   = 5;
    static constexpr int GRID_MARGIN = 20;
    static constexpr int GRID_GAP    = 10;
    static constexpr int META_H      = 42;  // title + badges strip below cover
};
