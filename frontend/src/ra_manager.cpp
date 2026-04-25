#include "ra_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <cstring>
#include <algorithm>
#include <map>
#include <SDL2/SDL_image.h>

// rcheevos headers
extern "C" {
#include "rc_client.h"
#include "rc_hash.h"
#include "rc_api_runtime.h"
}

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <wininet.h>
    #pragma comment(lib, "wininet.lib")
#endif

namespace fs = std::filesystem;

RAManager* RAManager::s_instance = nullptr;

// ─── HTTP transport ───────────────────────────────────────────────────────────
static bool performHttpGet(const std::string& url, std::string& outBody) {
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("HaackStation/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE |
        INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return false; }

    char buf[4096]; DWORD read = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
        outBody.append(buf, read);  // binary-safe: don't treat PNG data as C-string
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
#else
    FILE* pipe = popen(("curl -s --max-time 10 \"" + url + "\"").c_str(), "r");
    if (!pipe) return false;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) outBody += buf;
    pclose(pipe);
    return true;
#endif
}

static bool performHttpPost(const std::string& url,
                             const std::string& postData,
                             std::string& outBody) {
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("HaackStation/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) return false;

    URL_COMPONENTSA uc = {};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[1024] = {};
    uc.lpszHostName   = host; uc.dwHostNameLength   = sizeof(host);
    uc.lpszUrlPath    = path; uc.dwUrlPathLength    = sizeof(path);
    InternetCrackUrlA(url.c_str(), 0, 0, &uc);

    HINTERNET hConn = InternetConnectA(hInternet, host,
        INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr,
        INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInternet); return false; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", path,
        nullptr, nullptr, nullptr,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
    if (hReq) {
        std::string hdrs = "Content-Type: application/x-www-form-urlencoded\r\n";
        HttpSendRequestA(hReq, hdrs.c_str(), (DWORD)hdrs.size(),
            (void*)postData.c_str(), (DWORD)postData.size());
        char buf[4096]; DWORD read = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf), &read) && read > 0) {
            outBody.append(buf, read);
        }
        InternetCloseHandle(hReq);
    }
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInternet);
    return !outBody.empty();
#else
    FILE* pipe = popen(("curl -s --max-time 10 -d \"" + postData +
                         "\" \"" + url + "\"").c_str(), "r");
    if (!pipe) return false;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) outBody += buf;
    pclose(pipe);
    return true;
#endif
}

// ─── rc_client callbacks ──────────────────────────────────────────────────────

// Memory read callback — rc_client calls this to peek at emulator RAM
// ─── Memory read callback ─────────────────────────────────────────────────────
// rcheevos calls this every frame to watch PS1 RAM for achievement conditions.
// PS1 memory layout (what rcheevos expects at the address it passes us):
//   0x000000 – 0x1FFFFF  =  2 MB main system RAM
// RETRO_MEMORY_SYSTEM_RAM (id 0) in mednafen_psx_hw is this 2MB block.
//
// If mem is null or memSize is 0, achievements will NEVER fire — rc_client
// will think every memory read returned 0 and no conditions will trigger.
// The diagnostic log below prints once on the first call to help catch this.

static bool s_memDiagDone = false;

static uint32_t RC_CCONV raReadMemory(uint32_t address, uint8_t* buffer,
                                       uint32_t numBytes, rc_client_t* client) {
    if (!RAManager::s_instance || !RAManager::s_instance->getCore())
        return 0;

    LibretroBridge* core = RAManager::s_instance->getCore();
    const uint8_t* mem   = core->getSystemMemory();
    size_t memSize        = core->getSystemMemorySize();

    // ── One-time diagnostic on first frame ───────────────────────────────────
    if (!s_memDiagDone) {
        s_memDiagDone = true;
        if (!mem) {
            std::cerr << "[RA] MEMORY ERROR: getSystemMemory() returned nullptr!\n"
                      << "[RA]   Achievements will NOT fire. Check that\n"
                      << "[RA]   LibretroBridge::getSystemMemory() calls\n"
                      << "[RA]   retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)\n"
                      << "[RA]   and that the core has been loaded first.\n";
        } else if (memSize == 0) {
            std::cerr << "[RA] MEMORY ERROR: getSystemMemorySize() returned 0!\n"
                      << "[RA]   Achievements will NOT fire. Check that\n"
                      << "[RA]   LibretroBridge::getSystemMemorySize() calls\n"
                      << "[RA]   retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM).\n";
        } else {
            std::cout << "[RA] Memory OK: ptr=" << (void*)mem
                      << "  size=0x" << std::hex << memSize << std::dec
                      << " (" << (memSize / 1024) << " KB)\n";
            if (memSize != 0x200000) {
                std::cout << "[RA] WARNING: PS1 system RAM should be 2MB (0x200000).\n"
                          << "[RA]   Got " << memSize << " bytes — some achievement\n"
                          << "[RA]   addresses may be out of range.\n";
            }
        }
    }

    if (!mem || address >= (uint32_t)memSize) return 0;

    uint32_t canRead = (uint32_t)std::min((size_t)numBytes,
                                           memSize - (size_t)address);
    std::memcpy(buffer, mem + address, canRead);
    return canRead;
}

// Server call callback — rc_client calls this to make HTTP requests
static void RC_CCONV raServerCall(const rc_api_request_t* request,
                                   rc_client_server_callback_t callback,
                                   void* callbackData, rc_client_t* client) {
    // Capture what we need for the background thread
    std::string url      = request->url      ? request->url      : "";
    std::string postData = request->post_data ? request->post_data : "";

    std::thread([url, postData, callback, callbackData]() {
        std::string body;
        bool ok;
        if (postData.empty())
            ok = performHttpGet(url, body);
        else
            ok = performHttpPost(url, postData, body);

        rc_api_server_response_t response;
        response.body             = body.c_str();
        response.body_length      = body.size();
        response.http_status_code = ok ? 200 : 0;
        callback(&response, callbackData);
    }).detach();
}

// Event handler callback
static void RC_CCONV raEventHandler(const rc_client_event_t* event,
                                     rc_client_t* client) {
    if (RAManager::s_instance)
        RAManager::s_instance->handleEvent(event);
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
RAManager::RAManager() {
    s_instance = this;
    fs::create_directories(m_badgeDir);
    loadAllCachedGamesFromDisk();
}

RAManager::~RAManager() {
    shutdown();
    s_instance = nullptr;
}

// ─── Initialize ───────────────────────────────────────────────────────────────
bool RAManager::initialize(const std::string& username,
                             const std::string& tokenOrKey,
                             const std::string& password) {
    if (username.empty() || (tokenOrKey.empty() && password.empty())) {
        std::cout << "[RA] No credentials — achievements disabled\n";
        return false;
    }
    m_username = username;
    m_apiKey   = tokenOrKey;
    m_password = password;

    m_client = rc_client_create(raReadMemory, raServerCall);
    if (!m_client) {
        std::cerr << "[RA] Failed to create rc_client\n";
        return false;
    }

    rc_client_set_event_handler(m_client, raEventHandler);

    // Enable unofficial achievements (allows unlocks while pending RA approval)
    rc_client_set_unofficial_enabled(m_client, 1);

    // Disable hardcore mode by default (softcore allows save states)
    rc_client_set_hardcore_enabled(m_client, 0);

    // Log our user agent so RA can identify HaackStation
    {
        char uaClause[256] = {};
        rc_client_get_user_agent_clause(m_client, uaClause, sizeof(uaClause));
        std::cout << "[RA] User agent: HaackStation/0.1 " << uaClause << "\n";
    }

    // Login strategy:
    // 1. If we have a session token, try that first (fast, no password needed)
    // 2. If token fails or missing, use password login
    // 3. After password login, save the new session token for next time

    // Login strategy:
    // - If password is set, always use password login (gets fresh session token)
    // - If only token is set, try token login
    // After successful login, save the session token for future use

    if (!password.empty()) {
        // Password login — most reliable, gives us a fresh session token
        std::cout << "[RA] Logging in with password...\n";
        rc_client_begin_login_with_password(m_client,
            username.c_str(), password.c_str(),
            [](int result, const char* errorMessage,
               rc_client_t* client, void* userData) {
                RAManager* self = (RAManager*)userData;
                if (result == RC_OK) {
                    std::cout << "[RA] Logged in successfully\n";
                    self->m_loggedIn = true;
                    const rc_client_user_t* user = rc_client_get_user_info(client);
                    if (user && user->token && strlen(user->token) > 0) {
                        self->m_sessionToken = user->token;
                        std::cout << "[RA] Session token: "
                                  << self->m_sessionToken << "\n";
                        if (self->m_tokenSaveCallback)
                            self->m_tokenSaveCallback(self->m_sessionToken);
                    }
                } else {
                    std::cerr << "[RA] Login failed: "
                              << (errorMessage ? errorMessage : "unknown") << "\n";
                    std::cerr << "[RA] Check ra_user and ra_password in config\n";
                }
            }, this);
    } else if (!tokenOrKey.empty()) {
        // No password — try session token
        std::cout << "[RA] Trying token login...\n";
        rc_client_begin_login_with_token(m_client,
            username.c_str(), tokenOrKey.c_str(),
            [](int result, const char* errorMessage,
               rc_client_t* client, void* userData) {
                RAManager* self = (RAManager*)userData;
                if (result == RC_OK) {
                    std::cout << "[RA] Logged in with token\n";
                    self->m_loggedIn = true;
                } else {
                    std::cerr << "[RA] Token login failed: "
                              << (errorMessage ? errorMessage : "unknown") << "\n";
                    std::cerr << "[RA] Add ra_password to config for fresh login\n";
                }
            }, this);
    } else {
        std::cerr << "[RA] No password or token — add ra_password to config\n";
    }

    std::cout << "[RA] Initializing for: " << username << "\n";
    return true;
}

void RAManager::shutdown() {
    unloadGame();
    if (m_client) {
        rc_client_destroy(m_client);
        m_client = nullptr;
    }
    m_loggedIn = false;
}

// ─── Load game ────────────────────────────────────────────────────────────────
void RAManager::loadGame(const std::string& gamePath, LibretroBridge* core) {
    if (!m_client || !m_loggedIn) return;
    m_core       = core;
    m_gameLoaded = false;
    m_gameInfo   = RAGameInfo{};
    // Reset last-game pointer while the new game loads — details panel will
    // show 0/0 briefly until the loadGame callback completes, which is correct
    // (better than showing stale data for the wrong game).
    // We do NOT clear the full m_cachedAchievementsMap — all previously played
    // game data remains available for cold-start browsing.
    m_lastGameId = 0;

    // Hash the ROM — rc_hash_generate_from_file handles BIN/CUE natively.
    // CHD support requires libchdr to be linked (HAVE_CHD=1 in CMakeLists).
    char hash[33] = {};

    // Print the file extension so we can see what format is being hashed
    std::string ext;
    {
        size_t dot = gamePath.rfind('.');
        if (dot != std::string::npos) ext = gamePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    std::cout << "[RA] Hashing " << ext << " file: " << gamePath << "\n";

    if (!rc_hash_generate_from_file(hash, RC_CONSOLE_PLAYSTATION,
                                     gamePath.c_str())) {
        std::cerr << "[RA] Could not hash ROM: " << gamePath << "\n";
        if (ext == ".chd") {
            std::cerr << "[RA]   CHD hashing failed. Possible causes:\n"
                      << "[RA]   1. libchdr not linked — check CMake output for\n"
                      << "[RA]      'libchdr found — CHD hashing enabled'\n"
                      << "[RA]   2. CHD uses ZSTD compression — older libchdr builds\n"
                      << "[RA]      don't support ZSTD. Re-compress the CHD with:\n"
                      << "[RA]      chdman createcd --input game.cue --output game.chd\n"
                      << "[RA]      (uses default zlib, which libchdr always supports)\n"
                      << "[RA]   3. HAVE_CHD not defined when rcheevos compiled —\n"
                      << "[RA]      rebuild after CMakeLists.txt fix (set_source_files)\n";
        }
        return;
    }
    std::cout << "[RA] Hash: " << hash << "  (format: " << ext << ")\n";

    // Reset memory diagnostic so we get a fresh report for this game
    s_memDiagDone = false;
    m_loadingPath = gamePath;  // captured by callback via self->m_loadingPath

    rc_client_begin_load_game(m_client, hash,
        [](int result, const char* errorMessage,
           rc_client_t* client, void* userData) {
            RAManager* self = (RAManager*)userData;
            if (result == RC_OK) {
                const rc_client_game_t* game = rc_client_get_game_info(client);
                if (game) {
                    self->m_gameInfo.id    = game->id;
                    self->m_gameInfo.title = game->title ? game->title : "";
                    self->m_gameLoaded     = true;
                    std::cout << "[RA] Game: " << self->m_gameInfo.title
                              << " (ID:" << self->m_gameInfo.id << ")\n";

                    // Populate the per-game persistent cache immediately so it's
                    // available for the details panel trophy row and stopGame() hub
                    // update. We call getAchievementsWithBadgePaths() here while
                    // m_gameLoaded is already true so the rc_client query works.
                    uint32_t gid = self->m_gameInfo.id;
                    self->m_lastGameId = gid;
                    self->m_cachedGameInfoMap[gid]    = self->m_gameInfo;
                    self->m_cachedAchievementsMap[gid] = self->getAchievementsWithBadgePaths();
                    // Record path→gameId for cold-start details panel lookup
                    if (!self->m_loadingPath.empty())
                        self->m_pathToGameId[self->m_loadingPath] = gid;
                    std::cout << "[RA] Cache populated: "
                              << self->m_cachedAchievementsMap[gid].size()
                              << " achievements\n";

                    // Count achievements (use the cache we just built)
                    {
                        int total = (int)self->m_cachedAchievementsMap[gid].size();
                        self->m_gameInfo.totalAchievements = total;
                        self->m_cachedGameInfoMap[gid].totalAchievements = total;
                        std::cout << "[RA] " << total << " achievements\n";

                        // Save to disk immediately so cold-start browsing works.
                        self->saveGameAchievementsToDisk(gid);

                        // Download game icon for the notification.
                        // RA game icons live at:
                        //   https://media.retroachievements.org/Images/{imageIcon}
                        // The rc_client API doesn't expose the imageIcon field, but
                        // the game badge (used as icon in many RA apps) is available
                        // via the undocumented but stable URL using the game ID.
                        // We cache it as media/badges/game_{id}_icon.png.
                        std::string gameIconPath = "media/badges/game_" +
                            std::to_string(self->m_gameInfo.id) + "_icon.png";
                        if (!fs::exists(gameIconPath)) {
                            // Fetch from RA CDN — same CDN as badge images.
                            // Format: /Images/{gameId}.png (the game's box/icon image)
                            std::string iconUrl = "https://media.retroachievements.org/"
                                                  "Images/" +
                                                  std::to_string(self->m_gameInfo.id) +
                                                  ".png";
                            std::string iPath = gameIconPath;
                            std::string iDir  = "media/badges/";
                            std::thread([iconUrl, iPath, iDir]() {
                                std::string body;
                                if (performHttpGet(iconUrl, body) && body.size() >= 500) {
                                    fs::create_directories(iDir);
                                    std::ofstream f(iPath, std::ios::binary);
                                    f.write(body.data(), (std::streamsize)body.size());
                                    std::cout << "[RA] Game icon cached: " << iPath << "\n";
                                }
                            }).detach();
                        }

                        // Show "game loaded" notification — include icon path so
                        // render() can display it in the left slot
                        AchievementInfo gameInfo;
                        gameInfo.id          = 0; // 0 = game-loaded notification
                        gameInfo.title       = self->m_gameInfo.title;
                        gameInfo.description = std::to_string(total) +
                            " achievements available";
                        gameInfo.points      = 0;
                        gameInfo.badgeLocalPath = gameIconPath; // show game icon
                        self->queueNotification(gameInfo);

                        // Kick off background badge downloads for the Trophy Room
                        self->fetchAllBadges();
                    }
                }
            } else {
                std::cout << "[RA] Not in RA database: "
                          << (errorMessage ? errorMessage : "") << "\n";
            }
        }, this);
}

void RAManager::unloadGame() {
    if (m_client && m_gameLoaded)
        rc_client_unload_game(m_client);
    m_gameLoaded = false;
    m_gameInfo   = RAGameInfo{};
    m_core       = nullptr;
    std::lock_guard<std::mutex> lock(m_notifyMutex);
    m_notifications.clear();
}

// ─── Per-frame ────────────────────────────────────────────────────────────────
void RAManager::doFrame(LibretroBridge* core) {
    if (!m_client || !m_gameLoaded || !core) return;
    rc_client_do_frame(m_client);
}

// ─── Event handler ────────────────────────────────────────────────────────────
void RAManager::handleEvent(const rc_client_event_t* event) {
    if (!event) return;
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
            const rc_client_achievement_t* ach = event->achievement;
            if (!ach) break;
            AchievementInfo info;
            info.id          = ach->id;
            info.title       = ach->title       ? ach->title       : "";
            info.description = ach->description ? ach->description : "";
            info.points      = ach->points;
            info.unlocked    = true;

            // Skip system warning achievements (0 points, warning in title)
            if (info.points == 0 && info.title.find("Warning") != std::string::npos) {
                std::cout << "[RA] Suppressed warning: " << info.title << "\n";
                break;
            }
            if (ach->badge_name && strlen(ach->badge_name) > 0) {
                info.badgeUrl = std::string(
                    "https://media.retroachievements.org/Badge/") +
                    ach->badge_name + ".png";
                info.badgeLocalPath = m_badgeDir + ach->badge_name + ".png";
                // Download badge in background
                std::string badgeUrl  = info.badgeUrl;
                std::string badgePath = info.badgeLocalPath;
                std::thread([badgeUrl, badgePath]() {
                    if (!fs::exists(badgePath)) {
                        std::string body;
                        performHttpGet(badgeUrl, body);
                        if (!body.empty()) {
                            fs::create_directories(
                                fs::path(badgePath).parent_path());
                            std::ofstream f(badgePath, std::ios::binary);
                            f.write(body.data(), body.size());
                        }
                    }
                }).detach();
            }
            std::cout << "[RA] Unlocked: " << info.title
                      << " (" << info.points << "pts)\n";
            queueNotification(info);

            // Keep the cache in sync so stopGame() writes the correct
            // unlocked count to the Trophy Hub even mid-session.
            // Also persist immediately so progress survives a crash.
            if (m_lastGameId != 0) {
                auto it = m_cachedAchievementsMap.find(m_lastGameId);
                if (it != m_cachedAchievementsMap.end()) {
                    for (auto& cached : it->second) {
                        if (cached.id == info.id) {
                            cached.unlocked = true;
                            break;
                        }
                    }
                }
                saveGameAchievementsToDisk(m_lastGameId);
            }
            break;
        }
        case RC_CLIENT_EVENT_GAME_COMPLETED:
            std::cout << "[RA] Game mastered!\n";
            break;
        default: break;
    }
}

// ─── Notifications ────────────────────────────────────────────────────────────
void RAManager::queueNotification(const AchievementInfo& achievement) {
    std::cout << "[RA] Queuing notification: " << achievement.title << "\n";
    UnlockNotification n;
    n.achievement = achievement;
    n.showUntil   = SDL_GetTicks() + (Uint32)NOTIFY_DURATION_MS;
    n.slideAnim   = 0.f;
    n.sliding_in  = true;
    std::lock_guard<std::mutex> lock(m_notifyMutex);
    m_notifications.push_back(n);
}

void RAManager::update(float deltaMs) {
    std::lock_guard<std::mutex> lock(m_notifyMutex);
    Uint32 now = SDL_GetTicks();
    for (auto& n : m_notifications) {
        if (n.sliding_in) {
            n.slideAnim += deltaMs / NOTIFY_ANIM_MS;
            if (n.slideAnim >= 1.f) { n.slideAnim = 1.f; n.sliding_in = false; }
        } else if ((float)(n.showUntil - now) < NOTIFY_ANIM_MS && now < n.showUntil) {
            n.slideAnim = (float)(n.showUntil - now) / NOTIFY_ANIM_MS;
        }
    }
    m_notifications.erase(
        std::remove_if(m_notifications.begin(), m_notifications.end(),
            [now](const UnlockNotification& n) {
                return now > n.showUntil && n.slideAnim <= 0.f;
            }),
        m_notifications.end());
}

// ─── Render notifications ─────────────────────────────────────────────────────
void RAManager::render(int screenW, int screenH) {
    if (!m_renderer || !m_theme) return;
    std::lock_guard<std::mutex> lock(m_notifyMutex);
    if (m_notifications.empty()) return;

    const auto& pal = m_theme->palette();
    int notifY = screenH - NOTIFY_H - 20;

    for (auto& n : m_notifications) {
        if (n.slideAnim <= 0.f) continue;
        int slideX = (int)((1.f - n.slideAnim) * (NOTIFY_W + 40));
        int x = screenW - NOTIFY_W - 20 + slideX;
        int y = notifY;

        // Background
        SDL_Rect panel = { x, y, NOTIFY_W, NOTIFY_H };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer,
            pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 230);
        SDL_RenderFillRect(m_renderer, &panel);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        // RA gold accent border
        SDL_SetRenderDrawColor(m_renderer, 255, 215, 0, 255);
        SDL_Rect border = { x, y, 4, NOTIFY_H };
        SDL_RenderFillRect(m_renderer, &border);

        SDL_Color gold = { 255, 215, 0, 255 };
        SDL_Color raBlue = { 32, 144, 255, 255 };

        // ── Icon / badge — shown in both game-load and achievement notifs ───
        // For achievements: colour badge image.
        // For game-loaded (id==0): game icon downloaded at load time.
        // If the image file exists, render it in the 64×64 left slot.
        int textX = x + 12;
        if (!n.achievement.badgeLocalPath.empty() &&
            fs::exists(n.achievement.badgeLocalPath)) {
            SDL_Texture* icon = IMG_LoadTexture(m_renderer,
                n.achievement.badgeLocalPath.c_str());
            if (icon) {
                SDL_Rect br = { x + 8, y + 8, 64, 64 };
                SDL_RenderCopy(m_renderer, icon, nullptr, &br);
                SDL_DestroyTexture(icon);
                textX = x + 80;
            }
        }

        // Game-loaded notification (id == 0) vs achievement unlock
        if (n.achievement.id == 0) {
            // Game connected to RA
            m_theme->drawText("RetroAchievements",
                textX, y + 8, raBlue, FontSize::SMALL);
            m_theme->drawText(n.achievement.title,
                textX, y + 28, pal.textPrimary, FontSize::BODY);
            m_theme->drawText(n.achievement.description,
                textX, y + 52, pal.textSecond, FontSize::TINY);
        } else {
            // Achievement unlock
            m_theme->drawText("Achievement Unlocked!",
                textX, y + 8, gold, FontSize::SMALL);
            m_theme->drawText(n.achievement.title,
                textX, y + 28, pal.textPrimary, FontSize::BODY);
            m_theme->drawText(std::to_string(n.achievement.points) + " pts",
                textX, y + 52, pal.textSecond, FontSize::TINY);
        }

        notifY -= NOTIFY_H + 8;
    }
}

// ─── Accessors ────────────────────────────────────────────────────────────────
std::vector<AchievementInfo> RAManager::getAchievements() const {
    std::vector<AchievementInfo> result;
    if (!m_client || !m_gameLoaded) return result;
    rc_client_achievement_list_t* list =
        rc_client_create_achievement_list(m_client,
            RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!list) return result;
    for (uint32_t b = 0; b < list->num_buckets; b++) {
        for (uint32_t i = 0; i < list->buckets[b].num_achievements; i++) {
            const rc_client_achievement_t* a = list->buckets[b].achievements[i];
            AchievementInfo info;
            info.id          = a->id;
            info.title       = a->title       ? a->title       : "";
            info.description = a->description ? a->description : "";
            info.points      = a->points;
            info.unlocked    = (a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);
            result.push_back(info);
        }
    }
    rc_client_destroy_achievement_list(list);
    return result;
}

// ─── getAchievementsWithBadgePaths ────────────────────────────────────────────
// Like getAchievements() but fills in badge path fields so TrophyRoom can
// load textures without making any additional RA API calls.
std::vector<AchievementInfo> RAManager::getAchievementsWithBadgePaths() const {
    std::vector<AchievementInfo> result;
    if (!m_client || !m_gameLoaded) return result;

    rc_client_achievement_list_t* list =
        rc_client_create_achievement_list(m_client,
            RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!list) return result;

    for (uint32_t b = 0; b < list->num_buckets; b++) {
        for (uint32_t i = 0; i < list->buckets[b].num_achievements; i++) {
            const rc_client_achievement_t* a = list->buckets[b].achievements[i];
            AchievementInfo info;
            info.id          = a->id;
            info.title       = a->title       ? a->title       : "";
            info.description = a->description ? a->description : "";
            info.points      = a->points;
            info.unlocked    = (a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);

            if (a->badge_name && strlen(a->badge_name) > 0) {
                std::string name = a->badge_name;
                info.badgeUrl          = "https://media.retroachievements.org/Badge/" + name + ".png";
                info.badgeLocalPath    = m_badgeDir + name + ".png";
                info.badgeLockUrl      = "https://media.retroachievements.org/Badge/" + name + "_lock.png";
                info.badgeLockLocalPath= m_badgeDir + name + "_lock.png";
            }
            result.push_back(std::move(info));
        }
    }
    rc_client_destroy_achievement_list(list);
    return result;
}

// ─── fetchAllBadges ───────────────────────────────────────────────────────────
// Downloads colour + lock badge images for every achievement in the current
// game. Skips files that are already cached. Each badge spawns its own
// detached thread to avoid blocking the main loop.
void RAManager::fetchAllBadges() {
    if (!m_client || !m_gameLoaded) return;

    rc_client_achievement_list_t* list =
        rc_client_create_achievement_list(m_client,
            RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!list) return;

    std::cout << "[RA] Fetching badges for all achievements...\n";
    int queued = 0;

    for (uint32_t b = 0; b < list->num_buckets; b++) {
        for (uint32_t i = 0; i < list->buckets[b].num_achievements; i++) {
            const rc_client_achievement_t* a = list->buckets[b].achievements[i];
            if (!a->badge_name || strlen(a->badge_name) == 0) continue;

            std::string name      = a->badge_name;
            std::string colourUrl = "https://media.retroachievements.org/Badge/" + name + ".png";
            std::string lockUrl   = "https://media.retroachievements.org/Badge/" + name + "_lock.png";
            std::string colourPath= m_badgeDir + name + ".png";
            std::string lockPath  = m_badgeDir + name + "_lock.png";

            // Colour badge
            if (!fs::exists(colourPath)) {
                std::string url  = colourUrl;
                std::string path = colourPath;
                std::string dir  = m_badgeDir;
                std::thread([url, path, dir]() {
                    std::string body;
                    if (performHttpGet(url, body)) {
                        std::cout << "[RA] Badge body: " << body.size() << " bytes\n";
                        if (body.size() >= 500) {  // reject HTML error pages
                            fs::create_directories(dir);
                            std::ofstream f(path, std::ios::binary);
                            f.write(body.data(), (std::streamsize)body.size());
                            std::cout << "[RA] Badge cached: " << path << "\n";
                        } else {
                            std::cout << "[RA] Badge skipped (too small, error page?): "
                                      << body.substr(0, 80) << "\n";
                        }
                    }
                }).detach();
                queued++;
            }

            // Lock badge
            if (!fs::exists(lockPath)) {
                std::string url  = lockUrl;
                std::string path = lockPath;
                std::string dir  = m_badgeDir;
                std::thread([url, path, dir]() {
                    std::string body;
                    if (performHttpGet(url, body) && body.size() >= 500) {
                        fs::create_directories(dir);
                        std::ofstream f(path, std::ios::binary);
                        f.write(body.data(), (std::streamsize)body.size());
                        std::cout << "[RA] Badge (lock) cached: " << path << "\n";
                    }
                }).detach();
                queued++;
            }
        }
    }

    rc_client_destroy_achievement_list(list);
    std::cout << "[RA] Badge fetch: " << queued << " downloads queued\n";
}

int RAManager::unlockedCount() const {
    int count = 0;
    for (const auto& a : getAchievements())
        if (a.unlocked) count++;
    return count;
}

std::string RAManager::getRichPresence() const {
    if (!m_client || !m_gameLoaded) return "";
    char buf[256] = {};
    rc_client_get_rich_presence_message(m_client, buf, sizeof(buf));
    return buf;
}

void RAManager::setHardcoreMode(bool hardcore) {
    m_hardcore = hardcore;
    if (m_client)
        rc_client_set_hardcore_enabled(m_client, hardcore ? 1 : 0);
}

// ─── Achievement persistence ──────────────────────────────────────────────────
// Hand-rolled minimal JSON — same pattern used elsewhere in the project.
// No external JSON library required. Format is simple enough that this is safe.

// Escape a string for embedding in JSON — handles the only characters that
// can appear in RA titles/descriptions/paths.
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

void RAManager::saveGameAchievementsToDisk(uint32_t gameId) {
    auto git = m_cachedGameInfoMap.find(gameId);
    auto ait = m_cachedAchievementsMap.find(gameId);
    if (git == m_cachedGameInfoMap.end() || ait == m_cachedAchievementsMap.end()) {
        std::cerr << "[RA] saveGameAchievementsToDisk: no data for gameId "
                  << gameId << "\n";
        return;
    }
    const RAGameInfo&                   info = git->second;
    const std::vector<AchievementInfo>& achs = ait->second;

    fs::create_directories("saves/");
    std::string path = "saves/ra_achievements_" + std::to_string(gameId) + ".json";

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[RA] Could not write: " << path << "\n";
        return;
    }

    f << "{\n";
    f << "  \"gameId\": "      << gameId << ",\n";
    f << "  \"title\": \""     << jsonEscape(info.title) << "\",\n";
    f << "  \"totalPoints\": " << info.totalPoints << ",\n";

    // Write the path→gameId map as a flat list of paths so we can rebuild
    // m_pathToGameId on cold start without needing a separate index file.
    f << "  \"romPaths\": [";
    bool firstPath = true;
    for (const auto& kv : m_pathToGameId) {
        if (kv.second == gameId) {
            if (!firstPath) f << ", ";
            f << "\"" << jsonEscape(kv.first) << "\"";
            firstPath = false;
        }
    }
    f << "],\n";

    f << "  \"achievements\": [\n";
    for (size_t i = 0; i < achs.size(); i++) {
        const AchievementInfo& a = achs[i];
        f << "    {\n";
        f << "      \"id\": "             << a.id << ",\n";
        f << "      \"title\": \""        << jsonEscape(a.title) << "\",\n";
        f << "      \"description\": \""  << jsonEscape(a.description) << "\",\n";
        f << "      \"points\": "         << a.points << ",\n";
        f << "      \"unlocked\": "       << (a.unlocked ? "true" : "false") << ",\n";
        f << "      \"hardcore\": "       << (a.hardcore ? "true" : "false") << ",\n";
        f << "      \"badgeLocalPath\": \""     << jsonEscape(a.badgeLocalPath) << "\",\n";
        f << "      \"badgeLockLocalPath\": \"" << jsonEscape(a.badgeLockLocalPath) << "\"\n";
        f << "    }";
        if (i + 1 < achs.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    std::cout << "[RA] Saved " << achs.size() << " achievements → " << path << "\n";
}

// ─── Minimal JSON field extractors for loading ────────────────────────────────

static std::string jsonStr(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case 'n': val += '\n'; break;
                case 'r': val += '\r'; break;
                case 't': val += '\t'; break;
                default:  val += json[pos]; break;
            }
        } else {
            val += json[pos];
        }
        pos++;
    }
    return val;
}

static uint32_t jsonU32(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    uint32_t val = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        val = val * 10 + (json[pos++] - '0');
    return val;
}

static bool jsonBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    return (pos + 3 < json.size() && json.substr(pos, 4) == "true");
}

void RAManager::loadAllCachedGamesFromDisk() {
    if (!fs::exists("saves/")) return;

    int gamesLoaded = 0;
    for (const auto& entry : fs::directory_iterator("saves/")) {
        std::string fname = entry.path().filename().string();
        // Match saves/ra_achievements_12345.json
        if (fname.rfind("ra_achievements_", 0) != 0) continue;
        if (entry.path().extension() != ".json") continue;

        std::ifstream f(entry.path());
        if (!f.is_open()) continue;

        std::string json((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

        uint32_t gameId = jsonU32(json, "gameId");
        if (gameId == 0) continue;

        RAGameInfo info;
        info.id          = gameId;
        info.title       = jsonStr(json, "title");
        info.totalPoints = jsonU32(json, "totalPoints");

        // Parse romPaths array to rebuild m_pathToGameId
        {
            auto arrStart = json.find("\"romPaths\"");
            if (arrStart != std::string::npos) {
                arrStart = json.find('[', arrStart);
                auto arrEnd = json.find(']', arrStart);
                if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                    std::string arrStr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
                    // Extract each quoted string from the array
                    size_t p = 0;
                    while (p < arrStr.size()) {
                        auto q1 = arrStr.find('"', p);
                        if (q1 == std::string::npos) break;
                        q1++;
                        std::string romPath;
                        while (q1 < arrStr.size() && arrStr[q1] != '"') {
                            if (arrStr[q1] == '\\' && q1 + 1 < arrStr.size()) {
                                q1++;
                                switch (arrStr[q1]) {
                                    case 'n': romPath += '\n'; break;
                                    case 'r': romPath += '\r'; break;
                                    case 't': romPath += '\t'; break;
                                    default:  romPath += arrStr[q1]; break;
                                }
                            } else {
                                romPath += arrStr[q1];
                            }
                            q1++;
                        }
                        if (!romPath.empty())
                            m_pathToGameId[romPath] = gameId;
                        p = q1 + 1;
                    }
                }
            }
        }

        // Parse achievements array
        std::vector<AchievementInfo> achs;
        {
            auto arrStart = json.find("\"achievements\"");
            if (arrStart != std::string::npos) {
                arrStart = json.find('[', arrStart);
                if (arrStart != std::string::npos) {
                    // Walk through each achievement object {...}
                    size_t p = arrStart + 1;
                    while (p < json.size()) {
                        auto objStart = json.find('{', p);
                        if (objStart == std::string::npos) break;
                        auto objEnd = json.find('}', objStart);
                        if (objEnd == std::string::npos) break;
                        std::string obj = json.substr(objStart, objEnd - objStart + 1);

                        AchievementInfo a;
                        a.id                = jsonU32(obj, "id");
                        a.title             = jsonStr(obj, "title");
                        a.description       = jsonStr(obj, "description");
                        a.points            = jsonU32(obj, "points");
                        a.unlocked          = jsonBool(obj, "unlocked");
                        a.hardcore          = jsonBool(obj, "hardcore");
                        a.badgeLocalPath    = jsonStr(obj, "badgeLocalPath");
                        a.badgeLockLocalPath= jsonStr(obj, "badgeLockLocalPath");

                        if (a.id != 0)
                            achs.push_back(std::move(a));

                        p = objEnd + 1;
                    }
                }
            }
        }

        info.totalAchievements = (int)achs.size();
        m_cachedGameInfoMap[gameId]    = std::move(info);
        m_cachedAchievementsMap[gameId] = std::move(achs);
        gamesLoaded++;
    }

    if (gamesLoaded > 0)
        std::cout << "[RA] Loaded achievement cache for " << gamesLoaded
                  << " game(s) from disk\n";
}
