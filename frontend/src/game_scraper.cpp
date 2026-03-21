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

// ─── Already scraped check ────────────────────────────────────────────────────
bool GameScraper::isScraped(const GameEntry& game) const {
    std::string p = m_mediaDir + "covers/" + safeFilename(game.title) + ".png";
    if (fs::exists(p)) return true;
    p = m_mediaDir + "covers/" + safeFilename(game.title) + ".jpg";
    return fs::exists(p);
}

// ─── Rate limiting ────────────────────────────────────────────────────────────
void GameScraper::respectRateLimit() const {
    uint32_t now     = SDL_GetTicks();
    uint32_t elapsed = now - m_lastRequestTime;
    if (elapsed < MIN_REQUEST_INTERVAL_MS)
        SDL_Delay(MIN_REQUEST_INTERVAL_MS - elapsed);
    m_lastRequestTime = SDL_GetTicks();
}

// ─── Safe filename ────────────────────────────────────────────────────────────
std::string GameScraper::safeFilename(const std::string& name) const {
    std::string safe = name;
    const std::string invalid = "\\/:*?\"<>|";
    for (auto& c : safe)
        if (invalid.find(c) != std::string::npos) c = '_';
    return safe;
}

// ─── Build API URL ────────────────────────────────────────────────────────────
std::string GameScraper::buildApiUrl(const std::string& gameName,
                                      const std::string& serial) const {
    std::string url = "https://www.screenscraper.fr/api2/jeuInfos.php";

    // User credentials
    if (!m_user.empty()) {
        url += "?ssid="       + m_user;
        url += "&sspassword=" + m_password;
    } else {
        url += "?ssid=&sspassword=";
    }

    // Dev credentials
    if (!m_devId.empty()) {
        url += "&devid="       + m_devId;
        url += "&devpassword=" + m_devPassword;
    }

    url += "&softname=HaackStation";
    url += "&output=json";
    url += "&systemeid=" + std::to_string(PS1_SYSTEM_ID);
    url += "&regioneid=21";  // 21 = USA preferred
    url += "&langeid=en";

    // URL-encode the search name
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
    // Inject user credentials into ScreenScraper media URLs
    std::string url = urlIn;
    if (!m_user.empty() &&
        url.find("screenscraper.fr") != std::string::npos) {
        // Replace empty ssid= with actual user credentials
        size_t pos = url.find("ssid=&");
        if (pos != std::string::npos) {
            std::string replacement = "ssid=" + m_user +
                                      "&sspassword=" + m_password + "&";
            url.replace(pos, 6, replacement);
        }
        // Also handle ssid= at end of string
        pos = url.find("ssid=\r");
        if (pos == std::string::npos) pos = url.find("ssid=\n");
        if (pos != std::string::npos)
            url.replace(pos, 5, "ssid=" + m_user);
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
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

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
           && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
    }
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
    // Build: "key"
    std::string search;
    search += '"'; search += key; search += '"';

    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace (SS uses "key": "value" with spaces)
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
    // ScreenScraper embeds media as:
    // {"type": "box-2D", "url": "https://...", "region": "us", ...}
    // We prefer: us -> wor -> eu -> ss -> jp -> any

    std::vector<std::string> regionPref = {"us", "wor", "eu", "ss", "jp"};
    std::string bestUrl;
    int bestRank = 999;

    // Build type search strings (with and without space after colon)
    std::string t1, t2;
    t1 += '"'; t1 += "type"; t1 += '"'; t1 += ':'; t1 += '"';
    t1 += mediaType; t1 += '"';
    t2 += '"'; t2 += "type"; t2 += '"'; t2 += ':'; t2 += ' '; t2 += '"';
    t2 += mediaType; t2 += '"';

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

        // Find enclosing JSON object
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
        for (int i = 0; i < (int)regionPref.size(); i++) {
            if (region == regionPref[i]) { rank = i; break; }
        }

        if (bestUrl.empty() || rank < bestRank) {
            bestRank = rank;
            bestUrl  = url;
        }

        if (bestRank == 0) break; // US found, best possible
    }

    return bestUrl;
}

// ─── Parse ScreenScraper response ────────────────────────────────────────────
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

    // Title
    result.title = extractField(json, "text");
    if (result.title.empty()) result.title = extractField(json, "nom");
    if (result.title.empty()) result.title = game.title;

    // Metadata
    result.description = extractField(json, "synopsis");
    result.developer   = extractField(json, "developpeur");
    result.publisher   = extractField(json, "editeur");
    result.releaseDate = extractField(json, "date");
    if (result.releaseDate.empty())
        result.releaseDate = extractField(json, "dateSortie");
    result.genre = extractField(json, "genre");

    std::string ratingStr = extractField(json, "note");
    if (!ratingStr.empty()) {
        try { result.rating = std::stof(ratingStr) / 20.f; }
        catch (...) {}
    }

    // Cover art — try box art types in preference order
    std::string coverUrl = extractMediaUrl(json, "box-2D");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "mixrbv2");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "box-3D");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "screenshot");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "ss");

    std::cout << "[Scraper] Cover URL: "
              << (coverUrl.empty() ? "NOT FOUND" : coverUrl.substr(0,80))
              << "\n";

    if (!coverUrl.empty()) {
        std::string ext = ".png";
        if (coverUrl.find(".jpg") != std::string::npos) ext = ".jpg";
        std::string coverPath = m_mediaDir + "covers/" +
                                safeFilename(game.title) + ext;
        if (downloadFile(coverUrl, coverPath)) {
            result.coverPath = coverPath;
            std::cout << "[Scraper] Cover saved: " << coverPath << "\n";
        } else {
            std::cerr << "[Scraper] Cover download failed\n";
        }
    }

    // Screenshot
    std::string ssUrl = extractMediaUrl(json, "screenshot");
    if (!ssUrl.empty()) {
        std::string ssPath = m_mediaDir + "screenshots/" +
                             safeFilename(game.title) + ".jpg";
        if (downloadFile(ssUrl, ssPath))
            result.screenshotPath = ssPath;
    }

    result.success = true;
    return result;
}

// ─── Scrape single game ───────────────────────────────────────────────────────
ScrapeResult GameScraper::scrapeGame(const GameEntry& game) {
    std::cout << "[Scraper] Scraping: " << game.title << "\n";

    if (isScraped(game)) {
        std::cout << "[Scraper] Already scraped, skipping\n";
        ScrapeResult r;
        r.success   = true;
        r.coverPath = m_mediaDir + "covers/" + safeFilename(game.title) + ".png";
        return r;
    }

    respectRateLimit();

    std::string url      = buildApiUrl(game.title, game.serial);
    std::string jsonPath = m_mediaDir + "cache/" +
                           safeFilename(game.title) + ".json";

    if (!downloadFile(url, jsonPath)) {
        ScrapeResult r;
        r.errorReason = "Network error downloading from ScreenScraper";
        std::cerr << "[Scraper] Download failed: " << game.title << "\n";
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
    return parseResponse(ss.str(), game);
}

// ─── Scrape library ───────────────────────────────────────────────────────────
void GameScraper::scrapeLibrary(std::vector<GameEntry>& games,
                                 ProgressCallback callback) {
    ScrapeProgress progress;
    progress.total = (int)games.size();

    for (auto& game : games) {
        progress.currentGame = game.title;
        if (callback && !callback(progress)) break;

        if (isScraped(game)) {
            progress.skipped++;
            progress.done++;
            game.coverArtPath = m_mediaDir + "covers/" +
                                safeFilename(game.title) + ".png";
            if (!fs::exists(game.coverArtPath)) {
                game.coverArtPath = m_mediaDir + "covers/" +
                                    safeFilename(game.title) + ".jpg";
            }
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

void GameScraper::clearScrapedData(const GameEntry& game) {
    fs::remove(m_mediaDir + "covers/"      + safeFilename(game.title) + ".png");
    fs::remove(m_mediaDir + "covers/"      + safeFilename(game.title) + ".jpg");
    fs::remove(m_mediaDir + "screenshots/" + safeFilename(game.title) + ".jpg");
    fs::remove(m_mediaDir + "cache/"       + safeFilename(game.title) + ".json");
}
