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
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include <functional>

enum class InGameMenuAction {
    NONE,
    RESUME,
    OPEN_OMNISAVE,   // single entry point for all save features incl. card swap
    SOFT_RESET,      // retro_reset() — restarts game, SRAM untouched
    CHANGE_DISC,
    QUIT_TO_SHELF,
};

enum class InGameMenuSection {
    MAIN,
    DISC_SELECT,
};

// Phases of the disc select entry animation (played in order)
enum class DiscAnimPhase {
    HOLD_COVER,
    SLIDE_COVER,
    HOLD_STACK,
    FAN_OUT,
    SETTLED,
    LOAD_DISC,
};

class InGameMenu {
public:
    InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~InGameMenu();

    void open();
    void close();
    bool isOpen() const { return m_open; }

    // ── Multi-disc setup ──────────────────────────────────────────────────────
    void setDiscInfo(const std::vector<std::string>& discPaths, int currentDisc);
    void clearDiscInfo();

    // ── Media textures ────────────────────────────────────────────────────────
    void setCoverTexture(SDL_Texture* tex) { m_coverTexture = tex; }
    void setDiscArtPaths(const std::vector<std::string>& paths);

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render(SDL_Texture* gameFramebuffer = nullptr);

    InGameMenuAction pendingAction()    const { return m_pendingAction; }
    void             clearAction()            { m_pendingAction = InGameMenuAction::NONE; }
    int              pendingDiscIndex() const { return m_pendingDiscIndex; }

    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    void renderMain();
    void renderDiscSelect();
    void renderSoftResetConfirm();

    void renderDiscGraphic(int discIndex, int cx, int cy, int radius,
                           float opacity, bool selected, bool lifted,
                           SDL_Texture* discTexture = nullptr,
                           float spinAngle = 0.f);
    void renderTextureAsDisc(SDL_Texture* srcTex, int cx, int cy,
                             int radius, Uint8 alpha, float spinAngle = 0.f);

    void rebuildMenuItems();
    void navigateMain(NavAction action);
    void navigateDiscSelect(NavAction action);

    void loadDiscTextures();
    void freeDiscTextures();

    SDL_Renderer*     m_renderer   = nullptr;
    ThemeEngine*      m_theme      = nullptr;
    ControllerNav*    m_nav        = nullptr;

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

    bool m_confirmSoftReset = false;

    std::vector<std::string>  m_discPaths;
    std::vector<std::string>  m_discArtPaths;
    std::vector<SDL_Texture*> m_discTextures;
    int  m_currentDisc      = 0;
    int  m_highlightedDisc  = 0;
    int  m_pendingDiscIndex = 0;

    SDL_Texture* m_coverTexture = nullptr;  // borrowed, do NOT free

    DiscAnimPhase m_discPhase     = DiscAnimPhase::HOLD_COVER;
    float         m_phaseTimer    = 0.f;
    float         m_coverX        = 0.f;
    float         m_coverAlpha    = 1.f;
    float         m_fanProgress   = 0.f;
    float         m_fanCentre     = 0.f;
    float         m_loadTimer     = 0.f;
    float         m_loadSpinAngle = 0.f;

    static constexpr float DUR_HOLD_COVER  = 650.f;
    static constexpr float DUR_SLIDE_COVER = 620.f;
    static constexpr float DUR_HOLD_STACK  = 420.f;
    static constexpr float DUR_FAN_OUT     = 850.f;
    static constexpr float DUR_LOAD        = 900.f;
    static constexpr float FAN_LERP_SPEED  = 6.f;

    int m_settledCentreY = 0;

    float m_openAnim  = 0.f;
    float m_spinAngle = 0.f;

    int m_w = 1280;
    int m_h = 720;

    static constexpr int MENU_W         = 480;
    static constexpr int SLOT_COLS      = 3;
    static constexpr int SLOT_ROWS      = 2;
    static constexpr int SLOTS_PER_PAGE = SLOT_COLS * SLOT_ROWS;
};
