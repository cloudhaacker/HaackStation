#pragma once
// ingame_menu.h
// Overlay menu shown during gameplay (Start+Y combo).
// Semi-transparent overlay — game is still visible behind it.
//
// Menu sections:
//   Save States    — save/load with thumbnail grid
//   Resume         — close menu, return to game
//   Settings       — per-game settings (brightness, shader, etc)
//   Quit to Shelf  — same as Start+Select but from menu

#include "theme_engine.h"
#include "controller_nav.h"
#include "save_state_manager.h"
#include <SDL2/SDL.h>
#include <vector>
#include <string>
#include <functional>

enum class InGameMenuAction {
    NONE,
    RESUME,
    SAVE_STATE,
    LOAD_STATE,
    QUIT_TO_SHELF,
};

enum class InGameMenuSection {
    MAIN,
    SAVE_STATES,
    LOAD_STATES,
};

class InGameMenu {
public:
    InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
               ControllerNav* nav, SaveStateManager* saveStates);
    ~InGameMenu() = default;

    // Open/close the menu
    void open();
    void close();
    bool isOpen() const { return m_open; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render(SDL_Texture* gameFramebuffer = nullptr);

    // Check pending action
    InGameMenuAction pendingAction() const { return m_pendingAction; }
    void clearAction() { m_pendingAction = InGameMenuAction::NONE; }

    // Which slot was selected for save/load
    int selectedSlot() const { return m_selectedSlot; }

    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    void renderMain();
    void renderSaveStates(bool isSaving);
    void renderSlotCard(const SaveSlot& slot, int x, int y,
                        int w, int h, bool selected);

    void navigateMain(NavAction action);
    void navigateSaveStates(NavAction action);

    SDL_Renderer*      m_renderer  = nullptr;
    ThemeEngine*       m_theme     = nullptr;
    ControllerNav*     m_nav       = nullptr;
    SaveStateManager*  m_saveStates= nullptr;

    bool               m_open      = false;
    InGameMenuSection  m_section   = InGameMenuSection::MAIN;
    InGameMenuAction   m_pendingAction = InGameMenuAction::NONE;

    // Main menu items
    struct MenuItem {
        std::string      label;
        std::string      hint;
        InGameMenuAction action;
    };
    std::vector<MenuItem> m_items;
    int m_selectedItem  = 0;

    // Save/load state grid
    std::vector<SaveSlot>    m_slots;
    std::vector<SDL_Texture*> m_thumbTextures;
    int m_selectedSlot  = 0;

    // Animation
    float m_openAnim    = 0.f;  // 0=closed, 1=fully open
    float m_spinAngle   = 0.f;

    int m_w = 1280;
    int m_h = 720;

    // Free loaded thumbnails
    void freeThumbnails();
    void loadThumbnails();

    static constexpr int MENU_W      = 480;
    static constexpr int SLOT_COLS   = 3;
    static constexpr int SLOT_ROWS   = 2;
    static constexpr int SLOTS_PER_PAGE = SLOT_COLS * SLOT_ROWS;
};
