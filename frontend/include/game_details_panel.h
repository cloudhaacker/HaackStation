#pragma once
// game_details_panel.h
// The game details panel shown when pressing Y on a game in the shelf.
//
// Layout (based on author's sketch):
//
//  ┌─────────────────────┬──────────────────────────────┐
//  │                     │  [Screenshots strip]         │
//  │  Dimmed shelf       │  [Trophy row - last earned]  │
//  │  still visible      │  [Game description text]     │
//  │  behind panel       │                              │
//  │                     │  [Save System] [Shaders]     │
//  │                     │  [AI Upscale]  [Translation] │
//  └─────────────────────┴──────────────────────────────┘
//
// The left side dims the existing shelf.
// The right panel slides in from the right edge.
// Cover art displays large on the left over the dim layer.

#include "theme_engine.h"
#include "controller_nav.h"
#include "game_scanner.h"
#include "save_state_manager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <functional>

enum class DetailsPanelAction {
    NONE,
    LAUNCH,
    CLOSE,
    OPEN_SAVES,
    OPEN_SHADERS,
    OPEN_AI_UPSCALE,
    OPEN_TRANSLATION,
    OPEN_PER_GAME_SETTINGS,
};

struct DetailsMenuItem {
    std::string          icon;    // emoji/symbol for the icon
    std::string          label;
    DetailsPanelAction   action;
    bool                 enabled = true;
    std::string          hint;
};

class GameDetailsPanel {
public:
    GameDetailsPanel(SDL_Renderer* renderer, ThemeEngine* theme,
                     ControllerNav* nav);
    ~GameDetailsPanel();

    // Open the panel for a specific game
    void open(const GameEntry& game, SaveStateManager* saves = nullptr);
    void close();
    bool isOpen() const { return m_open; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);

    // Render — call AFTER rendering the shelf so we draw on top
    void render();

    DetailsPanelAction pendingAction() const { return m_pendingAction; }
    void clearAction()                       { m_pendingAction = DetailsPanelAction::NONE; }

    void onWindowResize(int w, int h);

    // Set cover art texture (loaded by game browser already)
    void setCoverTexture(SDL_Texture* tex) { m_coverTexture = tex; }

    // Set game description (from scraper)
    void setDescription(const std::string& desc) { m_description = desc; }

    // Set screenshot paths for the strip
    void setScreenshots(const std::vector<std::string>& paths);

    // Set trophy info
    void setTrophyInfo(int unlocked, int total,
                       const std::vector<std::string>& recentBadgePaths);

private:
    void buildMenuItems();
    void navigateMenu(NavAction action);
    void activateSelected();

    void renderDimLayer();
    void renderCoverHero();
    void renderPanel();
    void renderScreenshotStrip();
    void renderTrophyRow();
    void renderDescription();
    void renderMenuGrid();

    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saves     = nullptr;

    bool              m_open      = false;
    float             m_slideAnim = 0.f;   // 0=closed, 1=open
    GameEntry         m_game;

    // Content
    SDL_Texture*               m_coverTexture   = nullptr;
    std::string                m_description;
    std::vector<std::string>   m_screenshotPaths;
    std::vector<SDL_Texture*>  m_screenshotTextures;
    std::vector<std::string>   m_trophyBadgePaths;
    std::vector<SDL_Texture*>  m_trophyTextures;
    int                        m_screenshotIndex  = 0;
    int                        m_trophiesUnlocked = 0;
    int                        m_trophiesTotal    = 0;

    // Menu grid
    std::vector<DetailsMenuItem> m_items;
    int                          m_selectedItem = 0;

    DetailsPanelAction m_pendingAction = DetailsPanelAction::NONE;

    int m_w = 1280;
    int m_h = 720;

    // Panel takes up right 45% of screen
    static constexpr float PANEL_FRACTION = 0.45f;

    void loadScreenshotTextures();
    void freeScreenshotTextures();
    void loadTrophyTextures();
    void freeTrophyTextures();
};
