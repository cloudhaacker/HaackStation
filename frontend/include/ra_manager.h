#pragma once
// ra_manager.h
// RetroAchievements integration using the rc_client API.
//
// Handles:
//   - Login with RA username + API key
//   - Game identification by ROM hash
//   - Achievement tracking (called every frame)
//   - Unlock notifications (popup with icon + title)
//   - Leaderboard submissions
//   - Hardcore vs softcore mode
//
// The rc_client library does all the heavy lifting.
// We provide the HTTP transport and memory read callbacks.
//
// Usage:
//   RAManager ra;
//   ra.initialize("username", "api_key");
//   ra.loadGame("path/to/game.chd");       // hashes ROM, fetches achievements
//   ra.doFrame(core);                       // call every emulated frame
//   ra.render();                            // draw unlock notifications

#include "theme_engine.h"
#include "libretro_bridge.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

// Forward declare rc_client types to avoid including C headers in our header
struct rc_client_t;

struct AchievementInfo {
    uint32_t    id          = 0;
    std::string title;
    std::string description;
    std::string badgeUrl;           // URL to colour badge image
    std::string badgeLocalPath;     // Local path: media/badges/{name}.png
    std::string badgeLockUrl;       // URL to locked/greyscale badge
    std::string badgeLockLocalPath; // Local path: media/badges/{name}_lock.png
    uint32_t    points      = 0;
    bool        unlocked    = false;
    bool        hardcore    = false;
    SDL_Texture* badge      = nullptr; // Loaded badge texture (not used by TrophyRoom)
};

struct RAGameInfo {
    uint32_t    id          = 0;
    std::string title;
    std::string imageUrl;
    int         totalAchievements = 0;
    int         totalPoints       = 0;
    std::string richPresence;   // What you're doing in-game
};

struct UnlockNotification {
    AchievementInfo achievement;
    Uint32          showUntil   = 0;   // SDL_GetTicks() timestamp
    float           slideAnim   = 0.f; // 0=hidden, 1=fully visible
    bool            sliding_in  = true;
};

class RAManager {
public:
    RAManager();
    ~RAManager();

    // Initialize with user credentials
    bool initialize(const std::string& username,
                    const std::string& tokenOrKey,
                    const std::string& password = "");
    void setTokenSaveCallback(std::function<void(const std::string&)> cb) {
        m_tokenSaveCallback = cb;
    }
    void shutdown();
    bool isLoggedIn() const { return m_loggedIn; }

    // Set renderer for drawing notifications
    void setRenderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void setTheme(ThemeEngine* theme)         { m_theme = theme; }
    LibretroBridge* getCore() const           { return m_core; }

    // Load game — hashes the ROM and fetches achievement set from RA
    // Call after the core has loaded the game
    void loadGame(const std::string& gamePath, LibretroBridge* core);
    void unloadGame();
    bool isGameLoaded() const { return m_gameLoaded; }

    // Call every emulated frame — checks memory for achievement triggers
    void doFrame(LibretroBridge* core);

    // Draw unlock notifications on screen
    void render(int screenW, int screenH);

    // Update animations (call from main loop)
    void update(float deltaMs);

    // Settings
    void setHardcoreMode(bool hardcore);
    bool isHardcore() const { return m_hardcore; }

    // Get current game info and achievement list
    const RAGameInfo& gameInfo() const { return m_gameInfo; }
    std::vector<AchievementInfo> getAchievements() const;

    // Like getAchievements() but also populates badgeLocalPath / badgeLockLocalPath
    // for every entry so TrophyRoom can load textures directly.
    std::vector<AchievementInfo> getAchievementsWithBadgePaths() const;

    // Download all badge images for the current game into media/badges/.
    // Called automatically after loadGame() succeeds.
    // Spawns background threads — safe to call from main thread.
    void fetchAllBadges();
    int unlockedCount() const;

    // Rich presence string (what you're doing in-game)
    std::string getRichPresence() const;

    // Internal event handler (called by rc_client event callback)
    void handleEvent(const struct rc_client_event_t* event);

    // HTTP request callback — called by rcheevos to make web requests
    // This is called on a background thread
    static void httpCallback(const char* url, const char* postData,
                              void* callbackData);

    // Memory peek callback — rcheevos reads emulator RAM through this
    static uint32_t memoryPeek(uint32_t address, uint32_t numBytes,
                                void* userData);

    static RAManager* s_instance;

private:
    void queueNotification(const AchievementInfo& achievement);
    void processHttpRequest(const std::string& url,
                             const std::string& postData,
                             void* callbackData);
    bool downloadBadge(AchievementInfo& achievement);
    SDL_Texture* loadBadgeTexture(const std::string& path);

    rc_client_t*      m_client      = nullptr;
    SDL_Renderer*     m_renderer    = nullptr;
    ThemeEngine*      m_theme       = nullptr;
    LibretroBridge*   m_core        = nullptr;

    bool              m_loggedIn    = false;
    bool              m_gameLoaded  = false;
    bool              m_hardcore    = false;

    std::string       m_username;
    std::string       m_apiKey;
    std::string       m_password;
    std::string       m_sessionToken;
    std::function<void(const std::string&)> m_tokenSaveCallback;
    std::string       m_badgeDir    = "media/badges/";

    RAGameInfo        m_gameInfo;

    // Unlock notification queue
    std::mutex                    m_notifyMutex;
    std::vector<UnlockNotification> m_notifications;

    static constexpr float NOTIFY_DURATION_MS = 5000.f;
    static constexpr float NOTIFY_ANIM_MS     = 300.f;
    static constexpr int   NOTIFY_W           = 380;
    static constexpr int   NOTIFY_H           = 80;
};
