#pragma once
// play_history.h
// Tracks recently played games with timestamps.
// Persists to %APPDATA%\HaackStation\recently_played.json
//
// Format (JSON):
//   [
//     { "path": "C:/roms/Crash Bandicoot.chd",
//       "title": "Crash Bandicoot",
//       "lastPlayed": 1718000000 },
//     ...
//   ]
//
// Most recent entry is always first (index 0).
// Maximum MAX_HISTORY entries stored; oldest are dropped automatically.

#include <string>
#include <vector>
#include <cstdint>

struct PlayHistoryEntry {
    std::string path;
    std::string title;
    int64_t     lastPlayed = 0;  // Unix timestamp (seconds)
};

class PlayHistory {
public:
    PlayHistory();

    // Load from disk — call once at startup
    bool load();

    // Save to disk — call after recording a play
    bool save() const;

    // Record that a game was just launched
    // Moves it to the front if already present, adds new entry otherwise
    void recordPlay(const std::string& path, const std::string& title);

    // Get the list, most recent first
    const std::vector<PlayHistoryEntry>& entries() const { return m_entries; }

    // Clear all history
    void clear();

    // Maximum number of entries to store
    static constexpr int MAX_HISTORY = 20;

private:
    std::string              m_filePath;
    std::vector<PlayHistoryEntry> m_entries;

    std::string configDir() const;
};
