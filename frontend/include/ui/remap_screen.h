#pragma once
// remap_screen.h
// Full-screen input remapping UI.
//
// Layout:
//
//  ┌──────────────────────────────────────────────────────────────┐
//  │  INPUT REMAPPING                                              │
//  │  ──────────────────────────────────────────────────────────  │
//  │                                                              │
//  │  [Controller image placeholder / SVG]                        │
//  │  Detected: DualShock 4 Wireless Controller                   │
//  │                                                              │
//  │  ┌──────────────────────────────────────────────────────┐    │
//  │  │ Tab: [CONTROLLER]  [KEYBOARD]                        │    │
//  │  ├─────────────────┬──────────────┬────────────────┤    │    │
//  │  │ PS1 Button      │ Bound To     │                │    │    │
//  │  ├─────────────────┼──────────────┼────────────────┤    │    │
//  │  │ Cross (×)       │ A (Cross)    │                │    │    │
//  │  │ Circle (○)  ◄── │ B (Circle)   │  ← selected   │    │    │
//  │  │ ...             │ ...          │                │    │    │
//  │  └──────────────────────────────────────────────────────┘    │
//  │                                                              │
//  │  Frontend hotkeys (read-only):                               │
//  │  Fast forward: R2 hold     Turbo: R1+R2 hold                 │
//  │  Rewind: L2 hold           Menu: Start+Y                     │
//  │                                                              │
//  │  [A] Edit   [Y] Configure All   [X] Reset Defaults   [B] Back│
//  └──────────────────────────────────────────────────────────────┘
//
// SVG controller images live in assets/controllers/ (see handoff doc).
// Until SVGs are dropped in, a labelled placeholder rect is shown.

#include "input_map.h"
#include "theme_engine.h"
#include "controller_nav.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

// Which tab is active
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
    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

    // True when the map was modified and should be saved by the caller
    bool isDirty() const { return m_dirty; }
    void clearDirty()    { m_dirty = false; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderControllerPlaceholder();
    void renderTable();
    void renderHotkeys();
    void renderFooter();
    void renderListenOverlay();  // shown while waiting for a button press

    // ── Navigation ────────────────────────────────────────────────────────────
    void navigate(NavAction action);
    void beginListen();          // enter "press a button" state for current row
    void beginConfigureAll();    // sequential wizard through all bindings
    void resetDefaults();

    // ── Listen state ──────────────────────────────────────────────────────────
    // When m_listening = true, the next button/key press (not B/Esc) sets the binding.
    bool          m_listening      = false;
    bool          m_configuringAll = false;  // true during "Configure All" wizard
    int           m_listenIndex    = 0;      // which PS1Button we're listening for
    float         m_listenPulse    = 0.f;    // for the pulsing red animation
    Uint32        m_listenStartMs  = 0;

    // Poll for new button press each frame while listening
    void pollListen();
    bool applyBinding(SDL_GameController* ctrl, const Uint8* ks);

    // ── Controller detection ──────────────────────────────────────────────────
    std::string detectControllerName() const;
    // Returns a label like "PlayStation", "Xbox", "Switch", or "Unknown"
    std::string detectControllerFamily() const;

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;
    InputMap*     m_map      = nullptr;

    RemapTab m_tab          = RemapTab::CONTROLLER;
    int      m_selectedRow  = 0;
    int      m_scrollOffset = 0;     // first visible row index
    bool     m_wantsClose   = false;
    bool     m_dirty        = false;

    int m_w = 1280;
    int m_h = 720;

    // Rows visible in the table at once (computed from window height)
    static constexpr int ROW_H           = 52;
    static constexpr int TABLE_TOP_PAD   = 16;
    static constexpr int VISIBLE_ROWS    = 9;   // max rows shown without scrolling
};
