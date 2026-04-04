#include "game_scraper.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <SDL2/SDL.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <wininet.h>
    #pragma comment(lib, "wininet.lib")
#endif

namespace fs = std::filesystem;

GameScraper::GameScraper() {}
GameScraper::~GameScraper() {}

void GameScraper::setCredentials(const std::string& user,
                                  const std::string& password) {
    m_user     = user;
    m_password = password;
}

void GameScraper::setDevCredentials(const std::string& devId,
                                     const std::string& devPassword) {
    m_devId       = devId;
    m_devPassword = devPassword;
}

// ─── Filename sanitizer ───────────────────────────────────────────────────────
std::string GameScraper::safeFilename(const std::string& name) const {
    std::string safe = name;
    const std::string invalid = "\\/:*?\"<>|";
    for (auto& c : safe)
        if (invalid.find(c) != std::string::npos) c = '_';
    return safe;
}

// ─── Screenshot folder path for a game ───────────────────────────────────────
// All screenshots live in: media/screenshots/[safe title]/
// This is the canonical location the details panel looks in.
static std::string screenshotDir(const std::string& mediaDir,
                                  const std::string& safeTitle) {
    return mediaDir + "screenshots/" + safeTitle + "/";
}

// ─── Already-scraped checks ───────────────────────────────────────────────────
bool GameScraper::isScraped(const GameEntry& game) const {
    std::string safe = safeFilename(game.title);
    // Cover check (front only)
    if (fs::exists(m_mediaDir + "covers/" + safe + ".png")) return true;
    if (fs::exists(m_mediaDir + "covers/" + safe + ".jpg")) return true;
    return false;
}

bool GameScraper::screenshotsScraped(const GameEntry& game) const {
    std::string dir = screenshotDir(m_mediaDir, safeFilename(game.title));
    if (!fs::exists(dir)) return false;
    // Non-empty folder = already scraped
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".png") return true;
    }
    return false;
}

// ─── Rate limiting ────────────────────────────────────────────────────────────
void GameScraper::respectRateLimit() const {
    uint32_t now     = SDL_GetTicks();
    uint32_t elapsed = now - m_lastRequestTime;
    if (elapsed < MIN_REQUEST_INTERVAL_MS)
        SDL_Delay(MIN_REQUEST_INTERVAL_MS - elapsed);
    m_lastRequestTime = SDL_GetTicks();
}

// ─── Build API URL ────────────────────────────────────────────────────────────
std::string GameScraper::buildApiUrl(const std::string& gameName,
                                      const std::string& serial) const {
    std::string url = "https://www.screenscraper.fr/api2/jeuInfos.php";

    url += !m_user.empty()
        ? "?ssid=" + m_user + "&sspassword=" + m_password
        : "?ssid=&sspassword=";

    if (!m_devId.empty())
        url += "&devid=" + m_devId + "&devpassword=" + m_devPassword;

    url += "&softname=HaackStation";
    url += "&output=json";
    url += "&systemeid=" + std::to_string(PS1_SYSTEM_ID);
    url += "&regioneid=21";
    url += "&langeid=en";

    std::string searchName = serial.empty() ? gameName : serial;
    std::string encoded;
    for (unsigned char c : searchName) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            encoded += (char)c;
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    url += "&romnom=" + encoded;
    return url;
}

// ─── Download file ────────────────────────────────────────────────────────────
bool GameScraper::downloadFile(const std::string& urlIn,
                                const std::string& localPath) const {
    std::string url = urlIn;

    // Inject user credentials into ScreenScraper media URLs
    if (!m_user.empty() && url.find("screenscraper.fr") != std::string::npos) {
        size_t pos = url.find("ssid=&");
        if (pos != std::string::npos) {
            url.replace(pos, 6,
                "ssid=" + m_user + "&sspassword=" + m_password + "&");
        }
    }

#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("HaackStation/1.0",
                                         INTERNET_OPEN_TYPE_PRECONFIG,
                                         nullptr, nullptr, 0);
    if (!hInternet) return false;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(),
                                       nullptr, 0,
                                       INTERNET_FLAG_RELOAD |
                                       INTERNET_FLAG_SECURE |
                                       INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return false; }

    fs::create_directories(fs::path(localPath).parent_path());
    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead)
           && bytesRead > 0)
        outFile.write(buffer, bytesRead);

    outFile.close();
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
#else
    std::string cmd = "curl -s -L --max-time 30 -o \"" +
                      localPath + "\" \"" + url + "\"";
    fs::create_directories(fs::path(localPath).parent_path());
    return system(cmd.c_str()) == 0;
#endif
}

// ─── Extract JSON field ───────────────────────────────────────────────────────
std::string GameScraper::extractField(const std::string& json,
                                       const std::string& key) const {
    std::string search;
    search += '"'; search += key; search += '"';

    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\r' || json[pos] == '\n')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        pos++;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                if      (json[pos] == 'n') value += '\n';
                else if (json[pos] == 't') value += '\t';
                else                       value += json[pos];
            } else {
                value += json[pos];
            }
            pos++;
        }
        return value;
    } else if (isdigit(json[pos]) || json[pos] == '-') {
        std::string value;
        while (pos < json.size() &&
               (isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-'))
            value += json[pos++];
        return value;
    }
    return "";
}

// ─── Extract media URL with region preference ─────────────────────────────────
std::string GameScraper::extractMediaUrl(const std::string& json,
                                          const std::string& mediaType) const {
    std::vector<std::string> regionPref = {"us", "wor", "eu", "ss", "jp"};
    std::string bestUrl;
    int bestRank = 999;

    // Build type search strings — SS uses both "key":"val" and "key": "val"
    std::string t1, t2;
    t1 += '"'; t1 += "type"; t1 += '"'; t1 += ':';       t1 += '"'; t1 += mediaType; t1 += '"';
    t2 += '"'; t2 += "type"; t2 += '"'; t2 += ':'; t2 += ' '; t2 += '"'; t2 += mediaType; t2 += '"';

    size_t searchFrom = 0;
    while (true) {
        size_t p1 = json.find(t1, searchFrom);
        size_t p2 = json.find(t2, searchFrom);
        if (p1 == std::string::npos && p2 == std::string::npos) break;

        size_t pos;
        if      (p1 == std::string::npos) pos = p2;
        else if (p2 == std::string::npos) pos = p1;
        else pos = std::min(p1, p2);
        searchFrom = pos + 1;

        size_t objStart = pos;
        while (objStart > 0 && json[objStart] != '{') objStart--;
        size_t objEnd = objStart;
        int depth = 0;
        for (size_t i = objStart; i < json.size(); i++) {
            if      (json[i] == '{') depth++;
            else if (json[i] == '}') {
                depth--;
                if (depth == 0) { objEnd = i + 1; break; }
            }
        }

        std::string obj    = json.substr(objStart, objEnd - objStart);
        std::string url    = extractField(obj, "url");
        std::string region = extractField(obj, "region");

        if (url.empty() || url.find("http") == std::string::npos) continue;

        int rank = (int)regionPref.size();
        for (int i = 0; i < (int)regionPref.size(); i++)
            if (region == regionPref[i]) { rank = i; break; }

        if (bestUrl.empty() || rank < bestRank) {
            bestRank = rank;
            bestUrl  = url;
        }
        if (bestRank == 0) break;
    }
    return bestUrl;
}

// ─── Parse ScreenScraper response ────────────────────────────────────────────
// Downloads all available media from the single API response:
//   Front cover (box-2D)         → covers/[title].ext
//   Back cover  (box-2D-back)    → covers/[title]_back.ext
//   Screenshot  (screenshot)     → screenshots/[title]/01_screenshot.jpg
//   Title screen (ss)            → screenshots/[title]/02_titlescreen.jpg
//   Fan art      (fanart)        → screenshots/[title]/03_fanart.jpg
//
// Each media type is downloaded independently — a failure on one doesn't
// block the others. The screenshot folder skip check happens in scrapeGame().
ScrapeResult GameScraper::parseResponse(const std::string& json,
                                         const GameEntry& game) const {
    ScrapeResult result;

    if (json.find("Erreur") != std::string::npos) {
        auto errPos = json.find("Erreur");
        result.errorReason = "ScreenScraper: " +
            json.substr(errPos, std::min((size_t)120, json.size() - errPos));
        std::cerr << "[Scraper] Error: " << result.errorReason << "\n";
        return result;
    }

    bool hasData = json.find("\"jeu\"")    != std::string::npos ||
                   json.find("\"noms\"")   != std::string::npos ||
                   json.find("\"medias\"") != std::string::npos;
    if (!hasData || json.size() < 50) {
        result.errorReason = "No game data in response";
        std::cerr << "[Scraper] No data for: " << game.title << "\n";
        return result;
    }

    // ── Metadata ──────────────────────────────────────────────────────────────
    result.title = extractField(json, "text");
    if (result.title.empty()) result.title = extractField(json, "nom");
    if (result.title.empty()) result.title = game.title;

    result.description = extractField(json, "synopsis");
    result.developer   = extractField(json, "developpeur");
    result.publisher   = extractField(json, "editeur");
    result.releaseDate = extractField(json, "date");
    if (result.releaseDate.empty())
        result.releaseDate = extractField(json, "dateSortie");
    result.genre = extractField(json, "genre");

    std::string ratingStr = extractField(json, "note");
    if (!ratingStr.empty()) {
        try { result.rating = std::stof(ratingStr) / 20.f; } catch (...) {}
    }

    std::string safe      = safeFilename(game.title);
    std::string ssDir     = screenshotDir(m_mediaDir, safe);

    // ── Front cover ───────────────────────────────────────────────────────────
    {
        std::string url = extractMediaUrl(json, "box-2D");
        if (url.empty()) url = extractMediaUrl(json, "mixrbv2");
        if (url.empty()) url = extractMediaUrl(json, "box-3D");

        if (!url.empty()) {
            std::string ext = (url.find(".jpg") != std::string::npos) ? ".jpg" : ".png";
            std::string path = m_mediaDir + "covers/" + safe + ext;
            if (downloadFile(url, path)) {
                result.coverPath = path;
                std::cout << "[Scraper] Front cover: " << path << "\n";
            } else {
                std::cerr << "[Scraper] Front cover download failed\n";
            }
        } else {
            std::cout << "[Scraper] No front cover found for: " << game.title << "\n";
        }
    }

    // ── Back cover ────────────────────────────────────────────────────────────
    {
        std::string url = extractMediaUrl(json, "box-2D-back");
        if (!url.empty()) {
            std::string ext = (url.find(".jpg") != std::string::npos) ? ".jpg" : ".png";
            std::string path = m_mediaDir + "covers/" + safe + "_back" + ext;
            if (downloadFile(url, path)) {
                result.backCoverPath = path;
                std::cout << "[Scraper] Back cover: " << path << "\n";
            }
        }
    }

    // ── Screenshots (per-game folder) ─────────────────────────────────────────
    // We try three distinct media types and save each to its own numbered file.
    // The folder is created only if at least one download succeeds.
    struct MediaSlot {
        std::string type;       // ScreenScraper media type key
        std::string filename;   // Output filename inside the per-game folder
    };
    // Order: fanart first (most visually striking), then in-game screenshot,
    // then title screen. User capture screenshots sort after by timestamp prefix.
    const std::vector<MediaSlot> screenshotSlots = {
        { "fanart",     "01_fanart.jpg"      },  // shown first in details panel
        { "screenshot", "02_screenshot.jpg"  },  // in-game action shot
        { "ss",         "03_titlescreen.jpg" },  // title screen last
    };

    for (const auto& slot : screenshotSlots) {
        std::string url = extractMediaUrl(json, slot.type);
        if (url.empty()) {
            std::cout << "[Scraper] No " << slot.type
                      << " found for: " << game.title << "\n";
            continue;
        }
        std::string path = ssDir + slot.filename;
        if (downloadFile(url, path)) {
            result.screenshotPaths.push_back(path);
            std::cout << "[Scraper] " << slot.type << ": " << path << "\n";
        } else {
            std::cerr << "[Scraper] " << slot.type << " download failed\n";
        }
    }

    result.success = true;
    return result;
}

// ─── Scrape single game ───────────────────────────────────────────────────────
ScrapeResult GameScraper::scrapeGame(const GameEntry& game) {
    std::cout << "[Scraper] Scraping: " << game.title << "\n";

    // Build a "fully skipped" result for when everything is already present
    bool coverDone       = isScraped(game);
    bool screenshotsDone = screenshotsScraped(game);

    if (coverDone && screenshotsDone) {
        std::cout << "[Scraper] Already fully scraped, skipping\n";
        ScrapeResult r;
        r.success   = true;
        std::string safe = safeFilename(game.title);
        // Reconstruct cover path
        r.coverPath = m_mediaDir + "covers/" + safe + ".png";
        if (!fs::exists(r.coverPath))
            r.coverPath = m_mediaDir + "covers/" + safe + ".jpg";
        // Reconstruct screenshot paths from folder
        std::string dir = screenshotDir(m_mediaDir, safe);
        for (const auto& e : fs::directory_iterator(dir)) {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".png")
                r.screenshotPaths.push_back(e.path().string());
        }
        std::sort(r.screenshotPaths.begin(), r.screenshotPaths.end());
        return r;
    }

    respectRateLimit();

    // Download and cache the API JSON response
    std::string url      = buildApiUrl(game.title, game.serial);
    std::string jsonPath = m_mediaDir + "cache/" +
                           safeFilename(game.title) + ".json";

    if (!downloadFile(url, jsonPath)) {
        ScrapeResult r;
        r.errorReason = "Network error downloading from ScreenScraper";
        std::cerr << "[Scraper] API request failed: " << game.title << "\n";
        return r;
    }

    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        ScrapeResult r;
        r.errorReason = "Cannot read API response";
        return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();

    ScrapeResult result = parseResponse(ss.str(), game);

    // If cover was already present, restore that path (parseResponse may have
    // skipped it since we only call downloadFile, not check existence first)
    if (coverDone && result.coverPath.empty()) {
        std::string safe = safeFilename(game.title);
        result.coverPath = m_mediaDir + "covers/" + safe + ".png";
        if (!fs::exists(result.coverPath))
            result.coverPath = m_mediaDir + "covers/" + safe + ".jpg";
    }

    return result;
}

// ─── Scrape library ───────────────────────────────────────────────────────────
void GameScraper::scrapeLibrary(std::vector<GameEntry>& games,
                                 ProgressCallback callback) {
    ScrapeProgress progress;
    progress.total = (int)games.size();

    for (auto& game : games) {
        progress.currentGame = game.title;
        if (callback && !callback(progress)) break;

        bool coverDone       = isScraped(game);
        bool screenshotsDone = screenshotsScraped(game);

        if (coverDone && screenshotsDone) {
            progress.skipped++;
            progress.done++;
            // Restore cover path so browser can display it
            std::string safe = safeFilename(game.title);
            game.coverArtPath = m_mediaDir + "covers/" + safe + ".png";
            if (!fs::exists(game.coverArtPath))
                game.coverArtPath = m_mediaDir + "covers/" + safe + ".jpg";
            if (callback) callback(progress);
            continue;
        }

        ScrapeResult result = scrapeGame(game);
        if (result.success) {
            progress.succeeded++;
            if (!result.coverPath.empty())
                game.coverArtPath = result.coverPath;
            if (!result.title.empty() && result.title != game.title)
                game.title = result.title;
        } else {
            progress.failed++;
        }

        progress.done++;
        if (callback) callback(progress);
    }

    std::cout << "[Scraper] Complete: "
              << progress.succeeded << " scraped, "
              << progress.skipped   << " skipped, "
              << progress.failed    << " failed\n";
}

// ─── Clear scraped data ───────────────────────────────────────────────────────
void GameScraper::clearScrapedData(const GameEntry& game) {
    std::string safe = safeFilename(game.title);
    fs::remove(m_mediaDir + "covers/" + safe + ".png");
    fs::remove(m_mediaDir + "covers/" + safe + ".jpg");
    fs::remove(m_mediaDir + "covers/" + safe + "_back.jpg");
    fs::remove(m_mediaDir + "covers/" + safe + "_back.png");
    fs::remove(m_mediaDir + "cache/"  + safe + ".json");
    // Remove entire screenshot folder
    std::string dir = screenshotDir(m_mediaDir, safe);
    if (fs::exists(dir)) fs::remove_all(dir);
}
