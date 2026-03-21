#include "ra_manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <cstring>
#include <algorithm>
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
    while (InternetReadFile(hUrl, buf, sizeof(buf)-1, &read) && read > 0) {
        buf[read] = 0; outBody += buf;
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
        while (InternetReadFile(hReq, buf, sizeof(buf)-1, &read) && read > 0) {
            buf[read] = 0; outBody += buf;
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
static uint32_t RC_CCONV raReadMemory(uint32_t address, uint8_t* buffer,
                                       uint32_t numBytes, rc_client_t* client) {
    if (!RAManager::s_instance || !RAManager::s_instance->getCore())
        return 0;

    LibretroBridge* core = RAManager::s_instance->getCore();
    const uint8_t* mem   = core->getSystemMemory();
    size_t memSize        = core->getSystemMemorySize();

    if (!mem || address >= memSize) return 0;

    uint32_t canRead = (uint32_t)std::min((size_t)numBytes, memSize - address);
    memcpy(buffer, mem + address, canRead);
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

    // Hash the ROM — rc_hash_generate_from_file handles CHD, BIN/CUE, ISO
    char hash[33] = {};
    if (!rc_hash_generate_from_file(hash, RC_CONSOLE_PLAYSTATION,
                                     gamePath.c_str())) {
        std::cerr << "[RA] Could not hash ROM: " << gamePath << "\n";
        return;
    }
    std::cout << "[RA] ROM hash: " << hash << "\n";

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

                    // Count achievements
                    rc_client_achievement_list_t* list =
                        rc_client_create_achievement_list(client,
                            RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
                    if (list) {
                        int total = 0;
                        for (uint32_t b = 0; b < list->num_buckets; b++)
                            total += list->buckets[b].num_achievements;
                        self->m_gameInfo.totalAchievements = total;
                        std::cout << "[RA] " << total << " achievements\n";
                        rc_client_destroy_achievement_list(list);
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

        // Badge image
        int textX = x + 12;
        if (!n.achievement.badgeLocalPath.empty() &&
            fs::exists(n.achievement.badgeLocalPath)) {
            SDL_Texture* badge = IMG_LoadTexture(m_renderer,
                n.achievement.badgeLocalPath.c_str());
            if (badge) {
                SDL_Rect br = { x + 8, y + 8, 64, 64 };
                SDL_RenderCopy(m_renderer, badge, nullptr, &br);
                SDL_DestroyTexture(badge);
                textX = x + 80;
            }
        }

        // Text
        SDL_Color gold = { 255, 215, 0, 255 };
        m_theme->drawText("Achievement Unlocked!",
            textX, y + 8, gold, FontSize::SMALL);
        m_theme->drawText(n.achievement.title,
            textX, y + 28, pal.textPrimary, FontSize::BODY);
        m_theme->drawText(std::to_string(n.achievement.points) + " pts",
            textX, y + 52, pal.textSecond, FontSize::TINY);

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
