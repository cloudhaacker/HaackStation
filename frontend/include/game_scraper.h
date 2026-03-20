#pragma once
// game_scraper.h
// Scrapes game metadata from ScreenScraper.fr
//
// ScreenScraper is a free community database used by EmulationStation,
// Batocera, and most major frontends. It has cover art, descriptions,
// ratings, release dates, and more for virtually every PS1 game.
//
// API docs: https://www.screenscraper.fr/webapi2.php
//
// Usage:
//   GameScraper scraper;
//   scraper.setCredentials("your_username", "your_password"); // optional
//   scraper.setMediaDir("media/");
//   ScrapeResult result = scraper.scrapeGame("Crash Bandicoot", "SCUS-94900");
//   if (result.success) {
//       // result.coverPath has the downloaded cover art
//       // result.description has the game description
//   }
//
// ScreenScraper allows anonymous access with rate limiting.
// Users can register a free account for higher limits.

#include "game_scanner.h"
#include <string>
#include <functional>
#include <cstdint>

struct ScrapeResult {
    bool        success      = false;
    std::string errorReason;

    // Game info
    std::string title;
    std::string description;
    std::string developer;
    std::string publisher;
    std::string releaseDate;  // YYYY-MM-DD
    std::string genre;
    std::string region;
    float       rating       = 0.f;  // 0.0 - 1.0
    int         players      = 1;

    // Media paths (downloaded locally)
    std::string coverPath;      // Box art front
    std::string screenshotPath; // In-game screenshot
    std::string videoPath;      // Video preview (if available)
};

struct ScrapeProgress {
    int  total     = 0;
    int  done      = 0;
    int  succeeded = 0;
    int  failed    = 0;
    int  skipped   = 0;   // Already scraped
    std::string currentGame;
};

class GameScraper {
public:
    GameScraper();
    ~GameScraper();

    // Optional: set ScreenScraper account credentials for higher rate limits
    // Anonymous access works but is limited to ~20,000 requests/day total
    void setCredentials(const std::string& user, const std::string& password);

    // Directory where cover art and screenshots are saved
    void setMediaDir(const std::string& dir) { m_mediaDir = dir; }

    // Scrape a single game — returns immediately with result
    ScrapeResult scrapeGame(const GameEntry& game);

    // Scrape entire library — calls progressCallback for each game
    // Can be cancelled by returning false from the callback
    using ProgressCallback = std::function<bool(const ScrapeProgress&)>;
    void scrapeLibrary(std::vector<GameEntry>& games,
                       ProgressCallback callback = nullptr);

    // Check if a game already has scraped media
    bool isScraped(const GameEntry& game) const;

    // Clear all scraped media for a game (forces re-scrape)
    void clearScrapedData(const GameEntry& game);

    // ScreenScraper system ID for PS1
    static constexpr int PS1_SYSTEM_ID = 57;

private:
    // Build the API URL for a game lookup
    std::string buildApiUrl(const std::string& gameName,
                             const std::string& serial) const;

    // Download a file from URL to local path
    bool downloadFile(const std::string& url,
                      const std::string& localPath) const;

    // Parse JSON response from ScreenScraper
    ScrapeResult parseResponse(const std::string& json,
                                const GameEntry& game) const;

    // Extract a field from ScreenScraper's JSON response
    std::string extractField(const std::string& json,
                              const std::string& key) const;
    std::string extractMediaUrl(const std::string& json,
                                 const std::string& mediaType) const;

    // Sanitize filename for saving
    std::string safeFilename(const std::string& name) const;

    // Rate limiting
    void respectRateLimit() const;

    std::string m_user;
    std::string m_password;
    std::string m_mediaDir     = "media/";
    std::string m_devId;
    std::string m_devPassword;

    // Rate limiting state
    mutable uint32_t m_lastRequestTime = 0;
    static constexpr uint32_t MIN_REQUEST_INTERVAL_MS = 1000; // 1 req/sec max
};
