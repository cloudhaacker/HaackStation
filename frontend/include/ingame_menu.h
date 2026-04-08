#pragma once
// ingame_menu.h
// Overlay menu shown during gameplay (Start+Y combo).
// Semi-transparent overlay — game is still visible behind it.
//
// Menu sections:
//   Main           — Resume / Save State / Load State / Change Disc / Quit
//   Save States    — save/load with thumbnail grid
//   Disc Select    — stacked disc UI, only shown for multi-disc games

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
    CHANGE_DISC,    // ← NEW: disc index in pendingDiscIndex
    QUIT_TO_SHELF,
};

enum class InGameMenuSection {
    MAIN,
    SAVE_STATES,
    LOAD_STATES,
    DISC_SELECT,    // ← NEW
};

class InGameMenu {
public:
    InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
               ControllerNav* nav, SaveStateManager* saveStates);
    ~InGameMenu() = default;

    void open();
    void close();
    bool isOpen() const { return m_open; }

    // ── Multi-disc setup ──────────────────────────────────────────────────────
    // Call after launchGame() when the game is multi-disc.
    // discPaths: ordered list of disc image paths (disc 1 first).
    // currentDisc: 0-based index of the disc currently inserted.
    void setDiscInfo(const std::vector<std::string>& discPaths,
                     int currentDisc);
    void clearDiscInfo();   // Call on stopGame()

    // ── Cover art for disc select screen ──────────────────────────────────────
    // Pass the cover art texture so the disc select screen can show it.
    void setCoverTexture(SDL_Texture* tex) { m_coverTexture = tex; }

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
    void renderDiscGraphic(int discIndex, int cx, int cy, int radius,
                           float opacity, bool selected, bool lifted);
    void renderSlotCard(const SaveSlot& slot, int x, int y,
                        int w, int h, bool selected);

    // ── Navigation ────────────────────────────────────────────────────────────
    void rebuildMenuItems();  // Rebuilds m_items (call when disc info changes)
    void navigateMain(NavAction action);
    void navigateSaveStates(NavAction action);
    void navigateDiscSelect(NavAction action);

    // ── Thumbnails ────────────────────────────────────────────────────────────
    void freeThumbnails();
    void loadThumbnails();

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saveStates= nullptr;

    bool              m_open          = false;
    InGameMenuSection m_section       = InGameMenuSection::MAIN;
    InGameMenuAction  m_pendingAction = InGameMenuAction::NONE;

    // Main menu items
    struct MenuItem {
        std::string      label;
        std::string      hint;
        InGameMenuAction action;
    };
    std::vector<MenuItem> m_items;
    int m_selectedItem = 0;

    // Save/load state grid
    std::vector<SaveSlot>     m_slots;
    std::vector<SDL_Texture*> m_thumbTextures;
    int m_selectedSlot = 0;

    // Disc select state
    std::vector<std::string> m_discPaths;     // ordered disc image paths
    int  m_currentDisc    = 0;               // currently inserted disc (0-based)
    int  m_highlightedDisc= 0;               // disc the cursor is on
    int  m_pendingDiscIndex = 0;             // disc chosen at CONFIRM

    SDL_Texture* m_coverTexture = nullptr;   // borrowed — do NOT free

    // Animation
    float m_openAnim  = 0.f;
    float m_spinAngle = 0.f;

    int m_w = 1280;
    int m_h = 720;

    static constexpr int MENU_W         = 480;
    static constexpr int SLOT_COLS      = 3;
    static constexpr int SLOT_ROWS      = 2;
    static constexpr int SLOTS_PER_PAGE = SLOT_COLS * SLOT_ROWS;
};
