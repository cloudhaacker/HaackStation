#include "game_scraper.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>
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

// ─── Disc art path helpers ────────────────────────────────────────────────────
// Canonical path for a single disc's art. discNumber is 1-based.
// e.g. media/discs/Legend of Dragoon_disc1.png
std::string GameScraper::discArtPath(const std::string& gameTitle,
                                      int discNumber) const {
    return m_mediaDir + "discs/" + safeFilename(gameTitle)
           + "_disc" + std::to_string(discNumber) + ".png";
}

// Returns paths for all discs (discCount entries, index 0 = disc 1).
std::vector<std::string> GameScraper::discArtPaths(const std::string& gameTitle,
                                                    int discCount) const {
    std::vector<std::string> paths;
    paths.reserve(discCount);
    for (int i = 1; i <= discCount; i++)
        paths.push_back(discArtPath(gameTitle, i));
    return paths;
}

// ─── Path helpers ─────────────────────────────────────────────────────────────
static std::string screenshotDir(const std::string& mediaDir,
                                  const std::string& safeTitle) {
    return mediaDir + "screenshots/" + safeTitle + "/";
}

// ─── Already-scraped checks ───────────────────────────────────────────────────
bool GameScraper::isScraped(const GameEntry& game) const {
    std::string safe = safeFilename(game.title);
    if (fs::exists(m_mediaDir + "covers/" + safe + ".png")) return true;
    if (fs::exists(m_mediaDir + "covers/" + safe + ".jpg")) return true;
    return false;
}

bool GameScraper::screenshotsScraped(const GameEntry& game) const {
    std::string dir = screenshotDir(m_mediaDir, safeFilename(game.title));
    if (!fs::exists(dir)) return false;
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".png") return true;
    }
    return false;
}

// Disc art is considered scraped when disc 1 is present.
// (If the game is single-disc, that's the only one. Multi-disc games always
// have a disc 1, so this is a reliable "we've tried to scrape discs" sentinel.)
bool GameScraper::discArtScraped(const GameEntry& game) const {
    return fs::exists(discArtPath(game.title, 1));
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

// ─── Extract standard media URL with region preference ────────────────────────
std::string GameScraper::extractMediaUrl(const std::string& json,
                                          const std::string& mediaType) const {
    std::vector<std::string> regionPref = {"us", "wor", "eu", "ss", "jp"};
    std::string bestUrl;
    int bestRank = 999;

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

// ─── Extract disc art URLs ────────────────────────────────────────────────────
// ScreenScraper stores per-disc art under media type "support-texture".
// Each entry has a "support" field containing the 1-based disc number as a
// string ("1", "2", "3", "4"...) and a "region" field.
// We pick the best region per disc number using the same preference order.
// Falls back to "support-2D" if no support-texture entries are found.
//
// Returns: map of discNumber (int, 1-based) → best URL string
std::map<int, std::string> GameScraper::extractDiscUrls(
        const std::string& json) const {

    std::vector<std::string> regionPref = {"us", "wor", "eu", "ss", "jp"};

    // Try "support-texture" first, then "support-2D" as fallback
    const std::vector<std::string> discTypes = { "support-texture", "support-2D" };

    // bestUrl[discNum] = {url, regionRank}
    std::map<int, std::pair<std::string, int>> best;

    for (const auto& mediaType : discTypes) {
        std::string t1, t2;
        t1 += '"'; t1 += "type"; t1 += '"'; t1 += ':';
        t1 += '"'; t1 += mediaType; t1 += '"';
        t2 += '"'; t2 += "type"; t2 += '"'; t2 += ':'; t2 += ' ';
        t2 += '"'; t2 += mediaType; t2 += '"';

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

            // Find the enclosing JSON object
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

            std::string obj     = json.substr(objStart, objEnd - objStart);
            std::string url     = extractField(obj, "url");
            std::string region  = extractField(obj, "region");
            std::string support = extractField(obj, "support");

            if (url.empty() || url.find("http") == std::string::npos) continue;

            // Parse disc number from the "support" field ("1", "2", "3"...).
            // Single-disc games have NO "support" field at all — ScreenScraper
            // simply omits it. Treat absent/empty support as disc 1.
            int discNum = 1;
            if (!support.empty()) {
                try { discNum = std::stoi(support); } catch (...) { continue; }
                if (discNum < 1) continue;
            }

            int rank = (int)regionPref.size();
            for (int i = 0; i < (int)regionPref.size(); i++)
                if (region == regionPref[i]) { rank = i; break; }

            auto it = best.find(discNum);
            if (it == best.end() || rank < it->second.second)
                best[discNum] = { url, rank };
        }

        // If we found any entries for this type, don't try the fallback
        if (!best.empty()) break;
    }

    // Convert to plain url map
    std::map<int, std::string> result;
    for (const auto& [discNum, urlRank] : best)
        result[discNum] = urlRank.first;

    if (result.empty())
        std::cout << "[Scraper] No support-texture or support-2D entries found "
                     "(game may not have disc art on ScreenScraper)\n";
    else
        for (const auto& [disc, url] : result)
            std::cout << "[Scraper] Disc " << disc << " art URL found\n";

    return result;
}

// ─── Save metadata sidecar ────────────────────────────────────────────────────
// Writes a small JSON file to media/info/[safe title].json containing the
// game's text metadata so GameDetailsPanel can load description, developer,
// publisher, release date, and genre without keeping the full API response.
//
// Format:
// {
//   "title": "Legend of Dragoon, The",
//   "description": "The story of ...",
//   "developer": "Sony Computer Entertainment",
//   "publisher": "Sony Computer Entertainment America",
//   "releaseDate": "1999",
//   "genre": "Role-Playing"
// }
//
// Call this from parseResponse() after all metadata fields are populated.
void GameScraper::saveMetadataSidecar(const ScrapeResult& result,
                                       const std::string& safeTitle) const {
    std::string infoDir  = m_mediaDir + "info/";
    std::string infoPath = infoDir + safeTitle + ".json";
    fs::create_directories(infoDir);

    // Helper: escape a string for JSON (backslashes, quotes, control chars)
    auto escJson = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 16);
        for (unsigned char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) { /* skip other control chars */ }
            else out += (char)c;
        }
        return out;
    };

    std::ofstream f(infoPath);
    if (!f.is_open()) {
        std::cerr << "[Scraper] Cannot write info sidecar: " << infoPath << "\n";
        return;
    }

    f << "{\n";
    f << "  \"title\": \""       << escJson(result.title)       << "\",\n";
    f << "  \"description\": \"" << escJson(result.description) << "\",\n";
    f << "  \"developer\": \""   << escJson(result.developer)   << "\",\n";
    f << "  \"publisher\": \""   << escJson(result.publisher)   << "\",\n";
    f << "  \"releaseDate\": \"" << escJson(result.releaseDate) << "\",\n";
    f << "  \"genre\": \""       << escJson(result.genre)       << "\"\n";
    f << "}\n";

    std::cout << "[Scraper] Metadata sidecar: " << infoPath << "\n";
}



// ─── Synopsis extractor ───────────────────────────────────────────────────────
// ScreenScraper returns synopsis as a JSON array of language objects:
//   "synopsis": [{"langue": "en", "text": "..."}, {"langue": "fr", "text": "..."}, ...]
// extractField() stops at the '[' and returns empty. This function walks the
// array, preferring "langue":"en", falling back to the first entry found.
std::string GameScraper::extractSynopsis(const std::string& json) const {
    std::string search = "\"synopsis\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Advance to the opening '[' of the array
    pos = json.find('[', pos + search.size());
    if (pos == std::string::npos) return "";

    std::string firstText;

    while (true) {
        // Find next object in the array
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        // Find the matching closing brace
        size_t objEnd = objStart;
        int depth = 0;
        for (size_t i = objStart; i < json.size(); i++) {
            if      (json[i] == '{') depth++;
            else if (json[i] == '}') {
                depth--;
                if (depth == 0) { objEnd = i + 1; break; }
            }
        }

        std::string obj  = json.substr(objStart, objEnd - objStart);
        std::string lang = extractField(obj, "langue");
        std::string text = extractField(obj, "text");

        if (firstText.empty() && !text.empty())
            firstText = text;

        if (lang == "en" && !text.empty())
            return text;    // Preferred: English entry found

        pos = objEnd;
        if (pos >= json.size() || json[pos] == ']') break;
    }

    return firstText;   // Fallback: first language entry
}


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

    result.description = extractSynopsis(json);   // synopsis is an array, not a plain string
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

    std::string safe  = safeFilename(game.title);
    std::string ssDir = screenshotDir(m_mediaDir, safe);

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

    // ── Disc art — one file per disc ──────────────────────────────────────────
    // extractDiscUrls() reads "support-texture" entries (falling back to
    // "support-2D"), keyed by 1-based disc number. We download each one to
    // media/discs/[title]_disc1.png, _disc2.png, etc.
    {
        auto discUrls = extractDiscUrls(json);
        if (discUrls.empty()) {
            std::cout << "[Scraper] No disc art found for: " << game.title << "\n";
        } else {
            std::cout << "[Scraper] Found disc art for "
                      << discUrls.size() << " disc(s)\n";
        }

        for (const auto& [discNum, url] : discUrls) {
            std::string path = discArtPath(game.title, discNum);
            if (downloadFile(url, path)) {
                // Grow result.discArtPaths to fit discNum (0-based index = discNum-1)
                if ((int)result.discArtPaths.size() < discNum)
                    result.discArtPaths.resize(discNum);
                result.discArtPaths[discNum - 1] = path;
                std::cout << "[Scraper] Disc " << discNum << " art: " << path << "\n";
            } else {
                std::cerr << "[Scraper] Disc " << discNum
                          << " art download failed\n";
            }
        }
    }

    // ── Screenshots ───────────────────────────────────────────────────────────
    struct MediaSlot { std::string type; std::string filename; };
    const std::vector<MediaSlot> screenshotSlots = {
        { "fanart",     "01_fanart.jpg"      },
        { "screenshot", "02_screenshot.jpg"  },
        { "ss",         "03_titlescreen.jpg" },
    };

    for (const auto& slot : screenshotSlots) {
        std::string url = extractMediaUrl(json, slot.type);
        if (url.empty()) {
            std::cout << "[Scraper] No " << slot.type
                      << " for: " << game.title << "\n";
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


    // ── Metadata sidecar ──────────────────────────────────────────────────────
    // Always save — even if description is empty, the file records title/genre
    // so GameDetailsPanel knows a scrape was attempted and shows whatever is there.
    saveMetadataSidecar(result, safe);

    result.success = true;
    return result;
}

// ─── Scrape single game ───────────────────────────────────────────────────────
ScrapeResult GameScraper::scrapeGame(const GameEntry& game) {
    std::cout << "[Scraper] Scraping: " << game.title << "\n";

    bool coverDone       = isScraped(game);
    bool screenshotsDone = screenshotsScraped(game);
    bool discDone        = discArtScraped(game);

    if (coverDone && screenshotsDone && discDone) {
        std::string safe = safeFilename(game.title);

        // Write metadata sidecar if it doesn't exist yet — read from the
        // cached API JSON (always present after first scrape) so we don't
        // need to hit the network again.
        std::string infoPath   = m_mediaDir + "info/" + safe + ".json";
        std::string jsonCached = m_mediaDir + "cache/" + safe + ".json";
        if (!fs::exists(infoPath) && fs::exists(jsonCached)) {
            std::ifstream cf(jsonCached);
            if (cf.is_open()) {
                std::stringstream css; css << cf.rdbuf();
                // Build a minimal ScrapeResult just for the sidecar fields
                ScrapeResult tmp;
                tmp.title       = extractField(css.str(), "text");
                if (tmp.title.empty()) tmp.title = extractField(css.str(), "nom");
                if (tmp.title.empty()) tmp.title = game.title;
                tmp.description = extractField(css.str(), "synopsis");
                tmp.developer   = extractField(css.str(), "developpeur");
                tmp.publisher   = extractField(css.str(), "editeur");
                tmp.releaseDate = extractField(css.str(), "date");
                if (tmp.releaseDate.empty())
                    tmp.releaseDate = extractField(css.str(), "dateSortie");
                tmp.genre       = extractField(css.str(), "genre");
                saveMetadataSidecar(tmp, safe);
            }
        }

        std::cout << "[Scraper] Already fully scraped, skipping\n";
        ScrapeResult r;
        r.success = true;

        r.coverPath = m_mediaDir + "covers/" + safe + ".png";
        if (!fs::exists(r.coverPath))
            r.coverPath = m_mediaDir + "covers/" + safe + ".jpg";

        for (int d = 1; d <= 10; d++) {
            std::string p = discArtPath(game.title, d);
            if (!fs::exists(p)) break;
            r.discArtPaths.push_back(p);
        }

        std::string dir = screenshotDir(m_mediaDir, safe);
        if (fs::exists(dir)) {
            for (const auto& e : fs::directory_iterator(dir)) {
                std::string ext = e.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".jpg" || ext == ".png")
                    r.screenshotPaths.push_back(e.path().string());
            }
            std::sort(r.screenshotPaths.begin(), r.screenshotPaths.end());
        }
        return r;
    }

    respectRateLimit();

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
        ScrapeResult r; r.errorReason = "Cannot read API response"; return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();

    ScrapeResult result = parseResponse(ss.str(), game);

    // Restore already-present cover path if parseResponse didn't download it
    if (coverDone && result.coverPath.empty()) {
        std::string safe = safeFilename(game.title);
        result.coverPath = m_mediaDir + "covers/" + safe + ".png";
        if (!fs::exists(result.coverPath))
            result.coverPath = m_mediaDir + "covers/" + safe + ".jpg";
    }

    // Restore already-present disc paths if parseResponse skipped them
    if (discDone && result.discArtPaths.empty()) {
        for (int d = 1; d <= 10; d++) {
            std::string p = discArtPath(game.title, d);
            if (!fs::exists(p)) break;
            result.discArtPaths.push_back(p);
        }
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
        bool discDone        = discArtScraped(game);

        if (coverDone && screenshotsDone && discDone) {
            progress.skipped++;
            progress.done++;
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

    // Remove all disc art files (probe up to 10 discs)
    for (int d = 1; d <= 10; d++) {
        std::string p = discArtPath(game.title, d);
        if (!fs::exists(p)) break;
        fs::remove(p);
    }

    std::string dir = screenshotDir(m_mediaDir, safe);
    if (fs::exists(dir)) fs::remove_all(dir);
	
	// ADD TO clearScrapedData() after the screenshot folder removal:
     fs::remove(m_mediaDir + "info/" + safe + ".json");
// ════════════════════════════════════════════════════════════════════════════

}
