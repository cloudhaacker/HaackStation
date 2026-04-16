#pragma once
// game_scraper.h
// Fetches metadata and media art from ScreenScraper for PS1 games.
//
// Media downloaded per game:
//   Front cover   (box-2D)          → media/covers/[title].ext
//   Back cover    (box-2D-back)     → media/covers/[title]_back.ext
//   Disc art      (support-texture) → media/discs/[title]_disc1.png
//                                     media/discs/[title]_disc2.png  (multi-disc)
//                                     media/discs/[title]_disc3.png  (etc.)
//   Screenshots                     → media/screenshots/[title]/
//
// Disc art uses ScreenScraper's "support-texture" media type — the actual PS1
// disc label texture, a circular PNG with transparency. One entry per disc,
// identified by the "support" field ("1", "2", "3"...).
// Region preference (us → wor → eu → ss → jp) is applied per disc number.

#include "library/game_scanner.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

// ─── Scrape result ────────────────────────────────────────────────────────────
struct ScrapeResult {
    bool        success       = false;
    std::string errorReason;

    // Metadata
    std::string title;
    std::string description;
    std::string developer;
    std::string publisher;
    std::string releaseDate;
    std::string genre;
    float       rating        = 0.f;

    // Media paths (empty = not downloaded)
    std::string              coverPath;
    std::string              backCoverPath;
    std::vector<std::string> discArtPaths;    // index 0 = disc 1, index 1 = disc 2, etc.
    std::vector<std::string> screenshotPaths;
};

// ─── Progress tracking ────────────────────────────────────────────────────────
struct ScrapeProgress {
    int         total       = 0;
    int         done        = 0;
    int         succeeded   = 0;
    int         failed      = 0;
    int         skipped     = 0;
    std::string currentGame;
};

using ProgressCallback = std::function<bool(const ScrapeProgress&)>;

// ─── GameScraper ─────────────────────────────────────────────────────────────
class GameScraper {
public:
    GameScraper();
    ~GameScraper();

    void setCredentials(const std::string& user, const std::string& password);
    void setDevCredentials(const std::string& devId, const std::string& devPassword);
    void setMediaDir(const std::string& dir) { m_mediaDir = dir; }

    // Returns the path for a specific disc's art (discNumber is 1-based).
    // The file may or may not exist — call this after scraping to get the path.
    std::string discArtPath(const std::string& gameTitle, int discNumber = 1) const;

    // Returns all disc art paths for a game in disc order (size == discCount).
    // Entries are the expected paths whether or not they exist on disk.
    // Pass in the number of discs from the m3u/game entry.
    std::vector<std::string> discArtPaths(const std::string& gameTitle,
                                           int discCount) const;

    // Already-scraped checks
    bool isScraped(const GameEntry& game) const;
    bool screenshotsScraped(const GameEntry& game) const;
    bool discArtScraped(const GameEntry& game) const;   // true if disc 1 art present

    // Scrape a single game — returns populated ScrapeResult
    ScrapeResult scrapeGame(const GameEntry& game);

    // Scrape entire library
    void scrapeLibrary(std::vector<GameEntry>& games, ProgressCallback callback = nullptr);

    // Remove all scraped media for a game
    void clearScrapedData(const GameEntry& game);

private:
    std::string buildApiUrl(const std::string& gameName,
                             const std::string& serial) const;
    bool        downloadFile(const std::string& url,
                              const std::string& localPath) const;
    std::string extractField(const std::string& json,
                              const std::string& key) const;

    // Extract best URL for a standard (non-disc) media type with region preference
    std::string extractMediaUrl(const std::string& json,
                                 const std::string& mediaType) const;

    // Extract game description from ScreenScraper's synopsis array.
    // ScreenScraper returns synopsis as: [{"langue":"en","text":"..."},...]
    // extractField() cannot handle arrays so this dedicated function is required.
    // Falls back to the first entry if no English entry is found.
    std::string extractSynopsis(const std::string& json) const;

    // Extract disc art URLs from "support-texture" entries.
    // Returns map of discNumber (1-based int) → best URL for that disc.
    // Falls back to "support-2D" if support-texture entries are absent.
    std::map<int, std::string> extractDiscUrls(const std::string& json) const;

    ScrapeResult parseResponse(const std::string& json,
                                const GameEntry& game) const;
    std::string  safeFilename(const std::string& name) const;
    void         respectRateLimit() const;

    std::string  m_user;
    std::string  m_password;
    std::string  m_devId;
    std::string  m_devPassword;
    std::string  m_mediaDir = "media/";

    mutable uint32_t m_lastRequestTime = 0;
    static constexpr uint32_t MIN_REQUEST_INTERVAL_MS = 1000;
    static constexpr int      PS1_SYSTEM_ID           = 57;
	
	// ════════════════════════════════════════════════════════════════════════════
// ADD TO game_scraper.h private section:
   void saveMetadataSidecar(const ScrapeResult& result,
                             const std::string& safeTitle) const;
							 
};




