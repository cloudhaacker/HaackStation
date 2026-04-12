#pragma once
// ingame_menu.h
// Overlay menu shown during gameplay (Start+Y combo).
//
// Disc Select animation — three deliberate phases:
//
//   HOLD    Cover art shown large and centred, stationary (~600ms)
//   SLIDE   Cover slides left and fades out completely (~600ms, ease-out)
//   STACK   All discs appear stacked as one pile, stationary (~400ms)
//   FAN     Discs fan out slowly and smoothly to final positions (~800ms)
//   SETTLED Selected disc lifted, others overlapping; L/R cycles selection
//   LOAD    On confirm: selected disc spins up and glides off screen top

#include "theme_engine.h"
#include "controller_nav.h"
#include "save_state_manager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include <functional>

enum class InGameMenuAction {
    NONE,
    RESUME,
    SAVE_STATE,
    LOAD_STATE,
    CHANGE_DISC,
    QUIT_TO_SHELF,
};

enum class InGameMenuSection {
    MAIN,
    SAVE_STATES,
    LOAD_STATES,
    DISC_SELECT,
};

// Phases of the disc select entry animation (played in order)
enum class DiscAnimPhase {
    HOLD_COVER,   // Cover shown, stationary
    SLIDE_COVER,  // Cover slides left and fades out
    HOLD_STACK,   // Discs visible as one pile, stationary
    FAN_OUT,      // Discs fan out to final positions
    SETTLED,      // Interactive — player chooses a disc
    LOAD_DISC,    // Confirmed — chosen disc spins and flies off screen top
};

class InGameMenu {
public:
    InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
               ControllerNav* nav, SaveStateManager* saveStates);
    ~InGameMenu();

    void open();
    void close();
    bool isOpen() const { return m_open; }

    // ── Multi-disc setup ──────────────────────────────────────────────────────
    void setDiscInfo(const std::vector<std::string>& discPaths, int currentDisc);
    void clearDiscInfo();

    // ── Media textures ────────────────────────────────────────────────────────
    // Box art — borrowed, do NOT free
    void setCoverTexture(SDL_Texture* tex) { m_coverTexture = tex; }

    // Disc art paths — one per disc in disc order. InGameMenu loads and owns
    // these textures. Pass "" for discs with no scraped art.
    void setDiscArtPaths(const std::vector<std::string>& paths);

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render(SDL_Texture* gameFramebuffer = nullptr);

    InGameMenuAction pendingAction()    const { return m_pendingAction; }
    void             clearAction()            { m_pendingAction = InGameMenuAction::NONE; }
    int              selectedSlot()     const { return m_selectedSlot; }
    int              pendingDiscIndex() const { return m_pendingDiscIndex; }

    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderMain();
    void renderSaveStates(bool isSaving);
    void renderDiscSelect();

    // Draw one disc at (cx,cy) with given radius and opacity.
    // discTexture: real art mapped onto circle; null = procedural fallback.
    // selected / lifted control accent ring and drop shadow.
    // spinAngle: rotation for the load animation (radians, 0 = no rotation).
    void renderDiscGraphic(int discIndex, int cx, int cy, int radius,
                           float opacity, bool selected, bool lifted,
                           SDL_Texture* discTexture = nullptr,
                           float spinAngle = 0.f);

    // Render srcTex mapped onto a filled circle at (cx,cy,radius).
    // Uses per-scanline chord rendering — no SDL_gfx required.
    void renderTextureAsDisc(SDL_Texture* srcTex, int cx, int cy,
                             int radius, Uint8 alpha, float spinAngle = 0.f);

    void renderSlotCard(const SaveSlot& slot, int x, int y,
                        int w, int h, bool selected);

    // ── Navigation ────────────────────────────────────────────────────────────
    void rebuildMenuItems();
    void navigateMain(NavAction action);
    void navigateSaveStates(NavAction action);
    void navigateDiscSelect(NavAction action);

    // ── Thumbnails ────────────────────────────────────────────────────────────
    void freeThumbnails();
    void loadThumbnails();

    // ── Disc art textures ─────────────────────────────────────────────────────
    void loadDiscTextures();
    void freeDiscTextures();

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*     m_renderer   = nullptr;
    ThemeEngine*      m_theme      = nullptr;
    ControllerNav*    m_nav        = nullptr;
    SaveStateManager* m_saveStates = nullptr;

    bool              m_open          = false;
    InGameMenuSection m_section       = InGameMenuSection::MAIN;
    InGameMenuAction  m_pendingAction = InGameMenuAction::NONE;

    struct MenuItem {
        std::string      label;
        std::string      hint;
        InGameMenuAction action;
    };
    std::vector<MenuItem> m_items;
    int m_selectedItem = 0;

    std::vector<SaveSlot>     m_slots;
    std::vector<SDL_Texture*> m_thumbTextures;
    int m_selectedSlot = 0;

    // ── Disc select ───────────────────────────────────────────────────────────
    std::vector<std::string>  m_discPaths;       // game file paths
    std::vector<std::string>  m_discArtPaths;    // scraped art paths (parallel)
    std::vector<SDL_Texture*> m_discTextures;    // loaded textures (owned here)
    int  m_currentDisc     = 0;
    int  m_highlightedDisc = 0;
    int  m_pendingDiscIndex= 0;

    SDL_Texture* m_coverTexture = nullptr;  // borrowed, do NOT free

    // ── Disc select animation state ───────────────────────────────────────────
    DiscAnimPhase m_discPhase    = DiscAnimPhase::HOLD_COVER;
    float         m_phaseTimer   = 0.f;     // ms elapsed in current phase

    // Cover animation
    float m_coverX      = 0.f;   // current X offset from centre (0 = centred)
    float m_coverAlpha  = 1.f;   // 0.0 = invisible, 1.0 = fully visible

    // Fan animation: 0 = all stacked, 1 = fully spread
    float m_fanProgress = 0.f;

    // Smooth selection tracking: lerps toward m_highlightedDisc
    float m_fanCentre   = 0.f;   // fractional disc index the fan centres on

    // Load (exit) animation
    float m_loadTimer   = 0.f;   // ms into load animation
    float m_loadSpinAngle = 0.f; // accumulated spin angle (radians)

    // Phase durations (ms)
    static constexpr float DUR_HOLD_COVER  = 650.f;
    static constexpr float DUR_SLIDE_COVER = 620.f;
    static constexpr float DUR_HOLD_STACK  = 420.f;
    static constexpr float DUR_FAN_OUT     = 850.f;
    static constexpr float DUR_LOAD        = 900.f;  // disc flies off screen

    // Fan centre lerp speed (fraction per second — higher = snappier)
    static constexpr float FAN_LERP_SPEED  = 6.f;

    // ── General ───────────────────────────────────────────────────────────────
    float m_openAnim  = 0.f;
    float m_spinAngle = 0.f;   // save state spinner

    int m_w = 1280;
    int m_h = 720;

    static constexpr int MENU_W         = 480;
    static constexpr int SLOT_COLS      = 3;
    static constexpr int SLOT_ROWS      = 2;
    static constexpr int SLOTS_PER_PAGE = SLOT_COLS * SLOT_ROWS;
};
