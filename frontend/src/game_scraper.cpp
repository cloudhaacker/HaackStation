#include "game_scraper.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <SDL2/SDL.h>

// Windows HTTP via WinINet (no extra deps needed on Windows)
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

// ─── Check if already scraped ─────────────────────────────────────────────────
bool GameScraper::isScraped(const GameEntry& game) const {
    std::string coverPath = m_mediaDir + "covers/" +
                            safeFilename(game.title) + ".png";
    return fs::exists(coverPath);
}

// ─── Rate limiting ────────────────────────────────────────────────────────────
void GameScraper::respectRateLimit() const {
    uint32_t now     = SDL_GetTicks();
    uint32_t elapsed = now - m_lastRequestTime;
    if (elapsed < MIN_REQUEST_INTERVAL_MS) {
        SDL_Delay(MIN_REQUEST_INTERVAL_MS - elapsed);
    }
    m_lastRequestTime = SDL_GetTicks();
}

// ─── Safe filename ────────────────────────────────────────────────────────────
std::string GameScraper::safeFilename(const std::string& name) const {
    std::string safe = name;
    const std::string invalid = R"(\/:*?"<>|)";
    for (auto& c : safe) {
        if (invalid.find(c) != std::string::npos) c = '_';
    }
    return safe;
}

// ─── Build API URL ────────────────────────────────────────────────────────────
std::string GameScraper::buildApiUrl(const std::string& gameName,
                                      const std::string& serial) const {
    // ScreenScraper API v2 endpoint
    // Requires a registered user account (free at screenscraper.fr)
    // Dev credentials are optional and require separate registration
    std::string url = "https://www.screenscraper.fr/api2/jeuInfos.php";

    // User credentials (required for reliable access)
    if (!m_user.empty()) {
        url += "?ssid="       + m_user;
        url += "&sspassword=" + m_password;
    } else {
        // Anonymous access — very limited, often fails
        url += "?ssid=&sspassword=";
    }

    // Dev credentials omitted — use personal account only
    // (Dev API registration is separate from user accounts at screenscraper.fr)

    url += "&softname=HaackStation";
    url += "&output=json";
    url += "&systemeid=" + std::to_string(PS1_SYSTEM_ID);

    // Search by ROM filename (most reliable for PS1)
    // Use serial if available, otherwise encode the game name
    std::string searchName = serial.empty() ? gameName : serial;
    std::string encoded;
    for (unsigned char c : searchName) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ') {
            if (c == ' ') encoded += "%20";
            else encoded += (char)c;
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
bool GameScraper::downloadFile(const std::string& url,
                                const std::string& localPath) const {
#ifdef _WIN32
    // Use WinINet for HTTP downloads — no curl/wget dependency needed
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

    // Create output directory
    fs::create_directories(fs::path(localPath).parent_path());

    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[4096];
    DWORD bytesRead = 0;
    bool success = true;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead)
           && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
    }

    outFile.close();
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return success;

#else
    // Linux/Android: use system curl
    std::string cmd = "curl -s -L --max-time 30 -o \"" +
                      localPath + "\" \"" + url + "\"";
    fs::create_directories(fs::path(localPath).parent_path());
    return system(cmd.c_str()) == 0;
#endif
}

// ─── Extract JSON field ───────────────────────────────────────────────────────
// Simple JSON extraction without a full parser dependency
std::string GameScraper::extractField(const std::string& json,
                                       const std::string& key) const {
    // Look for "key":"value" or "key": "value"
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Skip to the colon
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        pos++;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++; // skip escape
                if (json[pos] == 'n')       value += '\n';
                else if (json[pos] == 't')  value += '\t';
                else                         value += json[pos];
            } else {
                value += json[pos];
            }
            pos++;
        }
        return value;
    } else if (isdigit(json[pos]) || json[pos] == '-') {
        // Numeric value
        std::string value;
        while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '.' || json[pos] == '-'))
            value += json[pos++];
        return value;
    }

    return "";
}

// ─── Extract media URL from ScreenScraper response ───────────────────────────
std::string GameScraper::extractMediaUrl(const std::string& json,
                                          const std::string& mediaType) const {
    // ScreenScraper v2 medias array format:
    // {"type":"box-2D","parent":"jeu","url":"https://...","region":"wor",...}
    // Search for the type value, then find the nearest "url" field

    std::string typeSearch = "\"type\":\"" + mediaType + "\"";
    size_t pos = json.find(typeSearch);

    // Try alternate format: "type": "box-2D" (with space)
    if (pos == std::string::npos) {
        typeSearch = "\"type\": \"" + mediaType + "\"";
        pos = json.find(typeSearch);
    }
    if (pos == std::string::npos) return "";

    // Look backwards and forwards up to 300 chars for the url field
    size_t start = (pos > 300) ? pos - 300 : 0;
    size_t end   = std::min(json.size(), pos + 600);
    std::string nearby = json.substr(start, end - start);

    // Find "url" in this region
    std::string url = extractField(nearby, "url");

    // Filter out non-image URLs
    if (url.find("screenscraper.fr") == std::string::npos &&
        url.find("http") == std::string::npos) {
        return "";
    }
    return url;
}

// ─── Parse ScreenScraper JSON response ───────────────────────────────────────
ScrapeResult GameScraper::parseResponse(const std::string& json,
                                         const GameEntry& game) const {
    ScrapeResult result;

    // Check for known error responses
    if (json.find("Erreur") != std::string::npos) {
        // Extract the error message
        auto errPos = json.find("Erreur");
        result.errorReason = "ScreenScraper error: " +
            json.substr(errPos, std::min((size_t)100, json.size() - errPos));
        std::cerr << "[Scraper] SS Error: " << result.errorReason << "\n";
        return result;
    }
    if (json.size() < 50) {
        result.errorReason = "Empty response from ScreenScraper";
        return result;
    }
    // Check for "jeu" key which indicates a valid game response
    bool hasGameData = json.find("\"jeu\"") != std::string::npos ||
                       json.find("\"noms\"") != std::string::npos ||
                       json.find("\"medias\"") != std::string::npos;
    if (!hasGameData) {
        result.errorReason = "No game data in response (game not found in DB)";
        std::cerr << "[Scraper] No game data. Response: "
                  << json.substr(0, 200) << "\n";
        return result;
    }

    // ScreenScraper response structure:
    // response -> jeu -> noms[] -> text (for title)
    // response -> jeu -> synopsis[] -> text (for description)
    // response -> jeu -> medias[] -> url (for images)

    // Extract title — try multiple field names SS uses
    result.title = extractField(json, "text");  // inside noms array
    if (result.title.empty()) result.title = extractField(json, "nom");
    if (result.title.empty()) result.title = game.title;

    // Extract description
    result.description = extractField(json, "synopsis");
    if (result.description.empty())
        result.description = extractField(json, "text"); // inside synopsis

    // Developer/publisher
    result.developer   = extractField(json, "developpeur");
    result.publisher   = extractField(json, "editeur");
    result.releaseDate = extractField(json, "date");
    if (result.releaseDate.empty())
        result.releaseDate = extractField(json, "dateSortie");
    result.genre       = extractField(json, "genre");

    std::string ratingStr = extractField(json, "note");
    if (!ratingStr.empty()) {
        try { result.rating = std::stof(ratingStr) / 20.f; }
        catch (...) {}
    }

    // Extract cover art URL — SS uses "box-2D", "mixrbv2", "screenshot"
    std::string coverUrl = extractMediaUrl(json, "box-2D");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "mixrbv2");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "box-3D");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "screenshot");
    if (coverUrl.empty()) coverUrl = extractMediaUrl(json, "ss");



    // Download cover art
    if (!coverUrl.empty()) {
        std::string coverPath = m_mediaDir + "covers/" +
                                safeFilename(game.title) + ".png";
        if (downloadFile(coverUrl, coverPath)) {
            result.coverPath = coverPath;
        }
    }

    // Extract and download screenshot
    std::string ssUrl = extractMediaUrl(json, "screenshot");
    if (!ssUrl.empty()) {
        std::string ssPath = m_mediaDir + "screenshots/" +
                             safeFilename(game.title) + ".png";
        if (downloadFile(ssUrl, ssPath)) {
            result.screenshotPath = ssPath;
        }
    }

    result.success = true;
    return result;
}

// ─── Scrape a single game ─────────────────────────────────────────────────────
ScrapeResult GameScraper::scrapeGame(const GameEntry& game) {
    std::cout << "[Scraper] Scraping: " << game.title << "\n";

    // Skip if already scraped
    if (isScraped(game)) {
        std::cout << "[Scraper] Already scraped, skipping\n";
        ScrapeResult r;
        r.success   = true;
        r.coverPath = m_mediaDir + "covers/" + safeFilename(game.title) + ".png";
        return r;
    }

    respectRateLimit();

    // Build and download the API response
    std::string url      = buildApiUrl(game.title, game.serial);
    std::string jsonPath = m_mediaDir + "cache/" +
                           safeFilename(game.title) + ".json";

    if (!downloadFile(url, jsonPath)) {
        ScrapeResult r;
        r.errorReason = "Failed to download from ScreenScraper API";
        std::cerr << "[Scraper] Download failed for: " << game.title << "\n";
        return r;
    }

    // Read the JSON response
    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        ScrapeResult r;
        r.errorReason = "Could not read API response file";
        return r;
    }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    return parseResponse(json, game);
}

// ─── Scrape entire library ────────────────────────────────────────────────────
void GameScraper::scrapeLibrary(std::vector<GameEntry>& games,
                                 ProgressCallback callback) {
    ScrapeProgress progress;
    progress.total = (int)games.size();

    for (auto& game : games) {
        progress.currentGame = game.title;

        if (callback && !callback(progress)) {
            std::cout << "[Scraper] Cancelled by user\n";
            break;
        }

        // Skip already scraped
        if (isScraped(game)) {
            progress.skipped++;
            progress.done++;
            // Update cover path in the game entry
            game.coverArtPath = m_mediaDir + "covers/" +
                                safeFilename(game.title) + ".png";
            if (callback) callback(progress);
            continue;
        }

        ScrapeResult result = scrapeGame(game);
        if (result.success) {
            progress.succeeded++;
            // Update the game entry with scraped data
            if (!result.coverPath.empty())
                game.coverArtPath = result.coverPath;
            if (!result.title.empty() && result.title != game.title)
                game.title = result.title;
        } else {
            progress.failed++;
            std::cerr << "[Scraper] Failed: " << game.title
                      << " — " << result.errorReason << "\n";
        }

        progress.done++;
        if (callback) callback(progress);
    }

    std::cout << "[Scraper] Done: "
              << progress.succeeded << " scraped, "
              << progress.skipped   << " skipped, "
              << progress.failed    << " failed\n";
}

void GameScraper::clearScrapedData(const GameEntry& game) {
    std::string coverPath = m_mediaDir + "covers/" +
                            safeFilename(game.title) + ".png";
    std::string jsonPath  = m_mediaDir + "cache/" +
                            safeFilename(game.title) + ".json";
    fs::remove(coverPath);
    fs::remove(jsonPath);
}
