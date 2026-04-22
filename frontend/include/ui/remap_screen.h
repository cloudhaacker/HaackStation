#pragma once
// remap_screen.h
// Full-screen input remapping UI — two-panel layout.
//
//  ┌──────────────────────────────────────────────────────────────────┐
//  │  INPUT REMAPPING                                     (title bar) │
//  │  ────────────────────────────────────────────────────────────── │
//  │  ┌─────────────────────────┐  ┌───────────────────────────────┐ │
//  │  │                         │  │ [CONTROLLER]  [KEYBOARD]      │ │
//  │  │   Controller SVG        │  │ ─────────────────────────── │ │
//  │  │   large, centred        │  │ PS1 Button    Binding        │ │
//  │  │   in left ~52% panel    │  │ Cross (×)     A (Cross)      │ │
//  │  │                         │  │ ● Circle (○)  B (Circle)     │ │
//  │  │  Detected: Xbox Ser. X  │  │ ...                           │ │
//  │  │  Family: Xbox           │  │ ─────────────────────────── │ │
//  │  │                         │  │ Hotkeys (read-only):          │ │
//  │  └─────────────────────────┘  │ FF: Hold R2 / Hold F          │ │
//  │                               └───────────────────────────────┘ │
//  │  [A] Edit  [Y] Configure All  [X] Reset Defaults  [B] Back      │
//  └──────────────────────────────────────────────────────────────────┘
//
// SVG controller images live in assets/controllers/.
// Loaded and rasterised once via nanosvg + nanosvgrast (header-only, MIT).
// CSS-class-based paths are pre-processed to inject fill="#ffffff" so that
// nanosvg — which doesn't parse CSS — can render them correctly.

#include "input_map.h"
#include "theme_engine.h"
#include "controller_nav.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

enum class RemapTab { CONTROLLER, KEYBOARD };

class RemapScreen {
public:
    RemapScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                ControllerNav* nav, InputMap* map);
    ~RemapScreen();

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose() const { return m_wantsClose; }
    void resetClose()       { m_wantsClose = false; }
    void onWindowResize(int w, int h);

    bool isDirty() const { return m_dirty; }
    void clearDirty()    { m_dirty = false; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderLeftPanel (int panelX, int panelY, int panelW, int panelH);
    void renderRightPanel(int panelX, int panelY, int panelW, int panelH);
    void renderFooter();
    void renderListenOverlay();

    // ── SVG ───────────────────────────────────────────────────────────────────
    void        loadControllerSvg();
    void        freeSvgTexture();
    static std::string preprocessSvg(const std::string& raw,
                                      const std::string& fillHex);
    static std::string svgPathForFamily(const std::string& family,
                                        const std::string& controllerName);

    // ── Navigation ────────────────────────────────────────────────────────────
    void navigate(NavAction action);
    void beginListen();
    void beginConfigureAll();
    void resetDefaults();

    // ── Listen state ──────────────────────────────────────────────────────────
    bool   m_listening      = false;
    bool   m_configuringAll = false;
    int    m_listenIndex    = 0;
    float  m_listenPulse    = 0.f;
    Uint32 m_listenStartMs  = 0;  // when listen began — used for cancel debounce
    Uint32 m_listenReadyMs  = 0;  // earliest tick we'll ACCEPT a new press.
                                  // Kept separate from m_listenStartMs so that
                                  // Configure All can insert a delay between
                                  // bindings without breaking the cancel check.

    void pollListen();
    bool applyBinding(SDL_GameController* ctrl, const Uint8* ks);

    // ── Controller detection ──────────────────────────────────────────────────
    std::string detectControllerName()   const;
    std::string detectControllerFamily() const;

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;
    InputMap*      m_map      = nullptr;

    RemapTab m_tab          = RemapTab::CONTROLLER;
    int      m_selectedRow  = 0;
    int      m_scrollOffset = 0;
    bool     m_wantsClose   = false;
    bool     m_dirty        = false;

    int m_w = 1280;
    int m_h = 720;

    // ── SVG state ─────────────────────────────────────────────────────────────
    SDL_Texture* m_svgTexture       = nullptr;
    std::string  m_svgLoadedFamily;
    bool         m_svgLoadAttempted = false;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr float LEFT_PANEL_FRAC = 0.52f; // left panel % of content width
    static constexpr int   TITLE_H         = 68;    // title text + divider
    static constexpr int   FOOTER_H_EXTRA  = 48;    // hint bar at screen bottom
    static constexpr int   ROW_H           = 46;    // one binding row
    static constexpr int   TAB_H           = 36;    // tab bar
    static constexpr int   HOTKEY_BLOCK_H  = 80;    // divider + label + 2 rows × 20px
    static constexpr int   MARGIN          = 60;    // left/right screen margin
    static constexpr int   PANEL_GAP       = 16;    // gap between the two panels
    static constexpr int   PANEL_PAD       = 16;    // inner padding inside panels
};
