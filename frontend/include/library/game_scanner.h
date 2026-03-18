#pragma once
// game_scanner.h
// Scans directories for PS1 disc images and builds the game library.
//
// M3U DE-DUPLICATION (automatic, no user input needed):
//   If a folder contains both "Final Fantasy IX.m3u" and the individual disc
//   files it references (e.g. "Final Fantasy IX (Disc 1).bin"), only the M3U
//   entry will appear in the library. The individual discs are silently
//   suppressed. The user sees one clean entry for the game. Disc switching
//   during gameplay is handled automatically via the M3U playlist.
//
//   This behaviour is always on. There is no setting to disable it.

#include "disc_formats.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

// ─── GameEntry ────────────────────────────────────────────────────────────────
// One entry in the game library — could be a single disc or an M3U playlist.
struct GameEntry {
    std::string   path;           // Path passed to the core on launch
    std::string   title;          // Clean display title
    DiscFormat    format;
    bool          isMultiDisc = false;
    int           discCount   = 1;

    // Optional metadata (populated later from a DB or scraper)
    std::string   region;         // "NTSC-U", "PAL", "NTSC-J"
    std::string   serial;         // e.g. "SCUS-94165"
    std::string   coverArtPath;   // Path to cover image if available
};

// ─── ScanResult ───────────────────────────────────────────────────────────────
struct ScanResult {
    std::vector<GameEntry> games;
    int totalScanned   = 0;   // Files examined
    int suppressed     = 0;   // Disc files hidden because an M3U covers them
    int invalidSkipped = 0;   // Files that failed validation
};

// ─── GameScanner ─────────────────────────────────────────────────────────────
class GameScanner {
public:
    GameScanner();

    // Add a directory to scan (call before scanAll)
    void addSearchPath(const std::string& dir);

    // Scan all registered directories and build the library
    // Populates default OS paths if none have been added
    ScanResult scanAll();

    // Convenience: scan a single directory right now and return results
    ScanResult scanDirectory(const std::string& dir);

    // The built library — valid after scanAll()
    const std::vector<GameEntry>& getLibrary() const { return m_library; }

    // Rescan (clears and rebuilds)
    ScanResult rescan();

    // Optional progress callback: called with (filesScanned, totalEstimate, currentFile)
    using ProgressCallback = std::function<void(int, int, const std::string&)>;
    void setProgressCallback(ProgressCallback cb) { m_progress = cb; }

    // Default search paths for each platform
    static std::vector<std::string> defaultPaths();

    // Scan default paths automatically on construction
    void scanDefaultPaths();

private:
    // Core scan logic
    ScanResult scanDirectoryInternal(const std::string& dir);

    // Build set of disc paths that are already covered by an M3U in this dir
    std::unordered_set<std::string> buildSuppressedSet(
        const std::string& dir,
        std::vector<GameEntry>& m3uEntries
    );

    // Normalize a path for set comparison (lowercase, canonical)
    static std::string normalizePath(const std::string& path);

    std::vector<std::string>   m_searchPaths;
    std::vector<GameEntry>     m_library;
    ProgressCallback           m_progress;
};
