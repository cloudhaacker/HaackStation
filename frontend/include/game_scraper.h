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
// Media downloaded per game (all from the single jeuInfos API call):
//   covers/[title].png         — box art front (box-2D)
//   covers/[title]_back.jpg    — box art back  (box-2D-back)
//   screenshots/[title]/       — per-game folder, up to 3 shots:
//       01_screenshot.jpg      — in-game screenshot
//       02_titlescreen.jpg     — title screen (ss)
//       03_fanart.jpg          — fan art / background art
//
// Skip logic:
//   Cover:       skip if covers/[title].png or .jpg already exists
//   Screenshots: skip if screenshots/[title]/ folder already exists (non-empty)

#include "game_scanner.h"
#include <string>
#include <vector>
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
    std::string              coverPath;         // Box art front
    std::string              backCoverPath;     // Box art back
    std::vector<std::string> screenshotPaths;   // Per-game folder contents
};

struct ScrapeProgress {
    int  total     = 0;
    int  done      = 0;
    int  succeeded = 0;
    int  failed    = 0;
    int  skipped   = 0;
    std::string currentGame;
};

class GameScraper {
public:
    GameScraper();
    ~GameScraper();

    void setCredentials(const std::string& user, const std::string& password);
    void setDevCredentials(const std::string& devId, const std::string& devPassword);
    void setMediaDir(const std::string& dir) { m_mediaDir = dir; }

    ScrapeResult scrapeGame(const GameEntry& game);

    using ProgressCallback = std::function<bool(const ScrapeProgress&)>;
    void scrapeLibrary(std::vector<GameEntry>& games,
                       ProgressCallback callback = nullptr);

    bool isScraped(const GameEntry& game) const;
    void clearScrapedData(const GameEntry& game);

    static constexpr int PS1_SYSTEM_ID = 57;

private:
    std::string buildApiUrl(const std::string& gameName,
                             const std::string& serial) const;
    bool downloadFile(const std::string& url,
                      const std::string& localPath) const;
    ScrapeResult parseResponse(const std::string& json,
                                const GameEntry& game) const;
    std::string extractField(const std::string& json,
                              const std::string& key) const;
    std::string extractMediaUrl(const std::string& json,
                                 const std::string& mediaType) const;
    std::string safeFilename(const std::string& name) const;

    // Returns true if the per-game screenshot folder exists and is non-empty
    bool screenshotsScraped(const GameEntry& game) const;

    void respectRateLimit() const;

    std::string m_user;
    std::string m_password;
    std::string m_mediaDir    = "media/";
    std::string m_devId;
    std::string m_devPassword;

    mutable uint32_t m_lastRequestTime = 0;
    static constexpr uint32_t MIN_REQUEST_INTERVAL_MS = 1000;
};
