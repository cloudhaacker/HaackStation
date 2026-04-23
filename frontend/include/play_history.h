#pragma once
// play_history.h
// Tracks which games have been played, when, and for how long.
//
// Each entry stores:
//   - Game path + display title
//   - Total accumulated playtime in seconds across all sessions
//   - Last played timestamp (Unix time)
//   - Play count (number of distinct sessions)
//
// Saved to: saves/play_history.json
// Format is simple hand-rolled JSON — no external parser needed.
//
// Usage:
//   playHistory.recordPlay(path, title);   // call on launch
//   playHistory.recordStop(path, seconds); // call on exit with session duration
//   playHistory.getEntries();              // returns sorted list (most recent first)
//   playHistory.getTotalSeconds(path);     // playtime for one game
//   playHistory.formatPlaytime(seconds);   // "2h 34m" / "45m" / "< 1m"

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>

struct PlayEntry {
    std::string path;           // canonical game file path (key)
    std::string title;          // display name
    uint64_t    totalSeconds = 0;   // accumulated playtime across all sessions
    time_t      lastPlayed   = 0;   // Unix timestamp of most recent launch
    int         playCount    = 0;   // number of times launched
};

class PlayHistory {
public:
    PlayHistory() = default;

    // ── Record a game launch ──────────────────────────────────────────────────
    // Moves the entry to the front of the list (most recent).
    // Creates a new entry if this path hasn't been played before.
    void recordPlay(const std::string& path, const std::string& title);

    // ── Record a game session ending ──────────────────────────────────────────
    // Adds sessionSeconds to the accumulated playtime for this path.
    // Safe to call with 0 (e.g. if the game crashed immediately).
    void recordStop(const std::string& path, uint64_t sessionSeconds);

    // ── Accessors ─────────────────────────────────────────────────────────────
    // Returns all entries, most recently played first.
    const std::vector<PlayEntry>& getEntries() const { return m_entries; }

    // Returns total accumulated playtime in seconds for a given path.
    // Returns 0 if the path hasn't been recorded.
    uint64_t getTotalSeconds(const std::string& path) const;

    // Returns the play count for a given path (0 if not recorded).
    int getPlayCount(const std::string& path) const;

    // ── Formatting ────────────────────────────────────────────────────────────
    // Converts a raw second count to a human-readable string.
    //   0        → "< 1m"
    //   45       → "< 1m"
    //   90       → "1m"
    //   3600     → "1h 0m"
    //   9000     → "2h 30m"
    static std::string formatPlaytime(uint64_t totalSeconds);

    // ── Persistence ───────────────────────────────────────────────────────────
    bool load(const std::string& path = "saves/play_history.json");
    bool save(const std::string& path = "saves/play_history.json") const;

private:
    // Finds entry by path — returns nullptr if not found
    PlayEntry*       findEntry(const std::string& path);
    const PlayEntry* findEntry(const std::string& path) const;

    std::vector<PlayEntry> m_entries; // ordered: most recently played first
};
