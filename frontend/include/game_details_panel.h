#pragma once
// game_details_panel.h
// The game details panel shown when pressing Y (or F2) on a game in the shelf.
//
// Layout (right 45% panel slides in from edge):
//
//  ┌─────────────────────┬──────────────────────────────────┐
//  │                     │  [Screenshot strip — 40% height] │
//  │  Dimmed shelf       │  ─────────────────────────────   │
//  │  still visible      │  [Trophy / achievement row]      │
//  │  behind panel       │  ─────────────────────────────   │
//  │                     │  [Game description text]         │
//  │  Cover art large    │  ─────────────────────────────   │
//  │  centered left      │  [Save System] [Shaders    ]     │
//  │                     │  [AI Upscale ] [Translation]     │
//  │                     │  [Game Settings]                 │
//  └─────────────────────┴──────────────────────────────────┘
//
// Metadata (description, developer, etc.) is loaded from a sidecar JSON file
// at media/info/[safe game title].json, written by the scraper.

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
    std::string          icon;
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

    void open(const GameEntry& game, SaveStateManager* saves = nullptr);
    void close();
    bool isOpen() const { return m_open; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    DetailsPanelAction pendingAction() const { return m_pendingAction; }
    void clearAction()                       { m_pendingAction = DetailsPanelAction::NONE; }

    void onWindowResize(int w, int h);

    // Called by app.cpp after opening — cover texture already loaded by browser
    void setCoverTexture(SDL_Texture* tex) { m_coverTexture = tex; }

    // Set the media root directory — must be called before open() so that
    // screenshot and metadata sidecar paths resolve correctly.
    // Defaults to "media/" if not set (works when no romsPath is configured).
    void setMediaDir(const std::string& dir) { m_mediaDir = dir; }

    // Optionally override the description (e.g. set from a ScrapeResult in memory).
    // If not called, description is loaded from the media/info/ sidecar on open().
    void setDescription(const std::string& desc) { m_description = desc; }

    // Supplement the auto-discovered folder screenshots with explicit paths
    void setScreenshots(const std::vector<std::string>& paths);

    // RetroAchievements trophy data
    void setTrophyInfo(int unlocked, int total,
                       const std::vector<std::string>& recentBadgePaths);

    static constexpr int MAX_SCREENSHOTS = 10;

private:
    void buildMenuItems();
    void navigateMenu(NavAction action);
    void activateSelected();

    // Load description + metadata from media/info/[title].json sidecar
    void loadMetadataSidecar();

    void renderDimLayer();
    void renderCoverHero();
    void renderPanel();
    void renderScreenshotStrip(int contentX, int contentW, int topY);
    void renderTrophyRow(int contentX, int contentW, int y);
    void renderDescription(int contentX, int contentW, int y);
    void renderMenuGrid(int contentX, int contentW);

    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saves     = nullptr;

    bool              m_open      = false;
    float             m_slideAnim = 0.f;
    GameEntry         m_game;

    SDL_Texture*               m_coverTexture    = nullptr;

    // Media root directory — set by setMediaDir() before open()
    std::string                m_mediaDir        = "media/";

    // Metadata — loaded from sidecar JSON or set externally
    std::string                m_description;
    std::string                m_developer;
    std::string                m_publisher;
    std::string                m_releaseDate;
    std::string                m_genre;

    std::vector<std::string>   m_screenshotPaths;
    std::vector<SDL_Texture*>  m_screenshotTextures;
    std::vector<std::string>   m_trophyBadgePaths;
    std::vector<SDL_Texture*>  m_trophyTextures;
    int                        m_screenshotIndex  = 0;
    int                        m_trophiesUnlocked = 0;
    int                        m_trophiesTotal    = 0;

    std::vector<DetailsMenuItem> m_items;
    int                          m_selectedItem = 0;

    DetailsPanelAction m_pendingAction = DetailsPanelAction::NONE;

    int m_w = 1280;
    int m_h = 720;

    static constexpr float PANEL_FRACTION = 0.45f;

    void loadScreenshotTextures();
    void freeScreenshotTextures();
    void loadTrophyTextures();
    void freeTrophyTextures();
};
