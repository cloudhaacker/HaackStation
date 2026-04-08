#pragma once
// per_game_settings_screen.h
// A focused settings overlay for per-game overrides.
// Opens from the Game Details Panel → "Game Settings" menu item.
//
// Shows only the settings that make sense to override per-game:
//   - Internal Resolution
//   - Renderer (software/hardware)
//   - Shader Pack
//   - Texture Replacement on/off
//   - Audio Replacement on/off
//   - Fast Boot on/off
//
// Each setting has a checkbox "Override" toggle. If override is OFF,
// the global setting applies. If ON, the per-game value is used.
// Saves to saves/per_game/[serial_or_stem].cfg on close.

#include "theme_engine.h"
#include "controller_nav.h"
#include "per_game_settings.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>

class PerGameSettingsScreen {
public:
    PerGameSettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                          ControllerNav* nav);
    ~PerGameSettingsScreen() = default;

    // Open for a specific game. serial can be empty — will use stem of gamePath.
    void open(const std::string& gameTitle,
              const std::string& gamePath,
              const std::string& serial,
              GameOverrides currentOverrides);
    void close();
    bool isOpen() const { return m_open; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    // Returns true when user pressed Back/Save — caller should re-apply settings
    bool wantsClose() const { return m_wantsClose; }
    void clearClose()        { m_wantsClose = false; }

    // Get the overrides as edited by the user
    const GameOverrides& overrides()   const { return m_overrides; }
    const std::string&   gameSerial()  const { return m_serial; }
    const std::string&   gamePath()    const { return m_gamePath; }

    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

private:
    struct Row {
        std::string label;
        std::string description;
        bool*       enabled;     // points into m_overrides — is this override active?
        // For choice/int settings:
        int*        value;       // nullptr for toggles
        std::vector<std::string> choices;
        // For bool settings when enabled:
        bool*       boolValue;   // nullptr for choice settings
    };

    void buildRows();
    void navigateAction(NavAction action);
    void renderRow(const Row& row, int x, int y, int w, bool selected);

    SDL_Renderer*  m_renderer = nullptr;
    ThemeEngine*   m_theme    = nullptr;
    ControllerNav* m_nav      = nullptr;

    bool        m_open       = false;
    bool        m_wantsClose = false;
    std::string m_gameTitle;
    std::string m_gamePath;
    std::string m_serial;

    GameOverrides        m_overrides;
    std::vector<Row>     m_rows;
    int                  m_selectedRow = 0;

    int m_w = 1280;
    int m_h = 720;

    static constexpr int PANEL_W  = 700;
    static constexpr int ITEM_H   = 64;
    static constexpr int PANEL_X_MARGIN = 60;
};
