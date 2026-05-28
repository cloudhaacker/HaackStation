#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <functional>

// Forward declarations
struct HaackSettings;
class  ThemeEngine;
class  ControllerNav;

// ─────────────────────────────────────────────────────────────────────────────
//  HubTile — one entry in the Hub tile row
// ─────────────────────────────────────────────────────────────────────────────

enum class HubTileID {
    NONE = -1,
    TROPHY_HUB,
    OMNISAVE,
    COLLECTIONS,    // stub — v0.6
    PLAY_HISTORY,   // stub — v0.6
    PROFILE,        // stub — v0.8
};

struct HubTile {
    HubTileID   id;
    std::string label;          // e.g. "Trophy Hub"
    std::string description;    // shown in info zone below tiles
    std::string stubLabel;      // e.g. "coming soon" or "v0.8" — empty = enabled
    SDL_Color   iconBg;         // small icon background square
    SDL_Color   iconFg;         // icon glyph / symbol color
    bool        enabled() const { return stubLabel.empty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
//  HaackHub
// ─────────────────────────────────────────────────────────────────────────────

class HaackHub {
public:
    HaackHub(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~HaackHub() = default;

    // Lifecycle — called by app.cpp
    void open(const HaackSettings& settings);
    void close();
    bool isOpen() const { return m_open; }

    // Per-frame
    void update(float dt);
    void render();

    // Input — matches the handleEvent(SDL_Event) pattern used by all other screens
    void handleEvent(const SDL_Event& e);

    // Result query — check each frame after handleEvent
    bool      hasPendingAction() const  { return m_hasPendingAction; }
    HubTileID pendingAction()    const  { return m_pendingAction; }
    void      clearPendingAction()      { m_pendingAction = HubTileID::NONE;
                                          m_hasPendingAction = false; }

    // Settings sync
    void setRaEnabled(bool enabled);

    // Window resize
    void onWindowResize(int w, int h) { m_screenW = w; m_screenH = h; }

private:
    // ── SDL / theme refs (not owned) ───────────────────────────────────────
    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;

    // ── Screen dimensions ─────────────────────────────────────────────────
    int m_screenW = 1280;
    int m_screenH = 720;

    // ── State ──────────────────────────────────────────────────────────────
    bool      m_open             = false;
    bool      m_hasPendingAction = false;
    HubTileID m_pendingAction    = HubTileID::NONE;
    bool      m_raEnabled        = true;

    // ── Tile data ──────────────────────────────────────────────────────────
    std::vector<HubTile> m_tiles;
    int                  m_selectedIdx = 0;

    // ── Animation ─────────────────────────────────────────────────────────
    float m_openAnim    = 0.0f;   // 0→1 fade/slide on open
    float m_selectAnim  = 0.0f;   // pulses on selection change (0→1→0)
    int   m_animFromIdx = 0;

    // ── Layout constants ───────────────────────────────────────────────────
    static constexpr int TILE_W      = 120;
    static constexpr int TILE_H      = 130;
    static constexpr int TILE_GAP    = 16;
    static constexpr int ICON_SIZE   = 48;
    static constexpr int HEADER_H    = 52;
    static constexpr int FOOTER_H    = 42;
    static constexpr int INFO_ZONE_H = 52;
    static constexpr int DOT_ZONE_H  = 20;

    // ── Helpers ───────────────────────────────────────────────────────────
    void buildTiles();
    void moveLeft();
    void moveRight();
    int  firstEnabledIdx() const;

    // Rendering helpers (all use m_renderer / m_theme internally)
    void renderBackground  (int w, int h) const;
    void renderHeader      (int w) const;
    void renderTileRow     (int w, int h) const;
    void renderSingleTile  (const HubTile& tile, int x, int y,
                            bool selected, float selAnim) const;
    void renderIconSymbol  (const HubTile& tile, int cx, int cy) const;
    void renderDots        (int w, int dotY) const;
    void renderInfoZone    (int w, int y) const;
    void renderFooter      (int w, int h) const;

    // SDL draw helpers
    void drawRect        (int x, int y, int w, int h,
                          SDL_Color c, Uint8 alpha = 255) const;
    void drawRectOutline (int x, int y, int w, int h,
                          SDL_Color c, Uint8 alpha = 255, int thickness = 1) const;
    void renderText      (const std::string& text, int x, int y, SDL_Color c,
                          bool centerX = false, int centerXWidth = 0) const;
};
