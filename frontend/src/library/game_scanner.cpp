#include "library/game_scanner.h"
#include "library/disc_formats.h"
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

GameScanner::GameScanner() {}

void GameScanner::addSearchPath(const std::string& dir) {
    m_searchPaths.push_back(dir);
}

void GameScanner::scanDefaultPaths() {
    for (const auto& p : defaultPaths())
        addSearchPath(p);
    scanAll();
}

ScanResult GameScanner::scanAll() {
    m_library.clear();
    ScanResult total;

    for (const auto& dir : m_searchPaths) {
        if (!fs::exists(dir)) continue;
        auto r = scanDirectoryInternal(dir);
        m_library.insert(m_library.end(), r.games.begin(), r.games.end());
        total.totalScanned   += r.totalScanned;
        total.suppressed     += r.suppressed;
        total.invalidSkipped += r.invalidSkipped;
    }

    // Sort alphabetically by title
    std::sort(m_library.begin(), m_library.end(),
        [](const GameEntry& a, const GameEntry& b) {
            return a.title < b.title;
        });

    total.games = m_library;

    std::cout << "[GameScanner] Library built: "
              << m_library.size()  << " games, "
              << total.suppressed  << " disc files suppressed by M3U, "
              << total.invalidSkipped << " invalid files skipped\n";

    return total;
}

ScanResult GameScanner::scanDirectory(const std::string& dir) {
    auto r = scanDirectoryInternal(dir);
    m_library = r.games;
    return r;
}

ScanResult GameScanner::rescan() {
    return scanAll();
}

// ─── Core scan logic ──────────────────────────────────────────────────────────
ScanResult GameScanner::scanDirectoryInternal(const std::string& dirPath) {
    ScanResult result;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        std::cerr << "[GameScanner] Not a directory: " << dirPath << "\n";
        return result;
    }

    // ── Pass 1: Find and process all M3U files first ──────────────────────────
    // We need to know which disc files are covered by M3Us BEFORE we process
    // individual disc files, so we can suppress them cleanly.
    std::vector<GameEntry> m3uEntries;
    auto suppressed = buildSuppressedSet(dirPath, m3uEntries);

    // Add the M3U entries to results
    for (auto& entry : m3uEntries) {
        result.games.push_back(std::move(entry));
        result.totalScanned++;
    }

    // ── Pass 2: Process individual disc files, skipping suppressed ones ────────
    for (const auto& dirEntry : fs::directory_iterator(dirPath)) {
        if (!dirEntry.is_regular_file()) continue;

        std::string path = dirEntry.path().string();
        std::string ext  = dirEntry.path().extension().string();

        // Skip M3U — already handled in pass 1
        if (DiscFormats::detectFormat(path) == DiscFormat::M3U) continue;

        // Skip non-disc extensions entirely
        if (!DiscFormats::isDiscExtension(ext)) continue;

        result.totalScanned++;

        // ── M3U SUPPRESSION CHECK ──────────────────────────────────────────────
        // If this file is referenced by an M3U, skip it.
        // The user will see the M3U entry instead — one clean entry per game.
        if (suppressed.count(normalizePath(path))) {
            result.suppressed++;
            std::cout << "[GameScanner] Suppressed (covered by M3U): "
                      << dirEntry.path().filename().string() << "\n";
            continue;
        }

        // Validate the disc
        DiscInfo info = DiscFormats::validate(path);
        if (!info.valid) {
            result.invalidSkipped++;
            std::cerr << "[GameScanner] Skipping invalid disc: "
                      << dirEntry.path().filename().string()
                      << " — " << info.errorReason << "\n";
            continue;
        }

        // Build library entry
        GameEntry entry;
        entry.path   = info.path;
        entry.title  = info.displayName;
        entry.format = info.format;

        result.games.push_back(std::move(entry));
    }

    return result;
}

// ─── Build the suppressed set ─────────────────────────────────────────────────
// Scans all M3U files in a directory, parses each one, and returns a set of
// normalized paths that should be hidden from the library view.
// Also populates m3uEntries with GameEntry objects for each valid M3U found.
std::unordered_set<std::string> GameScanner::buildSuppressedSet(
    const std::string& dirPath,
    std::vector<GameEntry>& m3uEntries)
{
    std::unordered_set<std::string> suppressed;

    for (const auto& dirEntry : fs::directory_iterator(dirPath)) {
        if (!dirEntry.is_regular_file()) continue;

        std::string path = dirEntry.path().string();
        if (DiscFormats::detectFormat(path) != DiscFormat::M3U) continue;

        // Validate the M3U (checks all referenced disc files exist)
        DiscInfo info = DiscFormats::validate(path);
        if (!info.valid) {
            std::cerr << "[GameScanner] Invalid M3U skipped: "
                      << dirEntry.path().filename().string()
                      << " — " << info.errorReason << "\n";
            continue;
        }

        // Add all disc files referenced in this M3U to the suppressed set
        for (const auto& discPath : info.m3uDiscs) {
            suppressed.insert(normalizePath(discPath));
        }

        // Create the library entry for the M3U itself
        GameEntry entry;
        entry.path        = info.path;
        entry.title       = info.displayName;
        entry.format      = DiscFormat::M3U;
        entry.isMultiDisc = true;
        entry.discCount   = static_cast<int>(info.m3uDiscs.size());

        std::cout << "[GameScanner] Multi-disc game: \""
                  << entry.title << "\" ("
                  << entry.discCount << " discs)\n";

        m3uEntries.push_back(std::move(entry));
    }

    return suppressed;
}

// ─── Path normalization ───────────────────────────────────────────────────────
// Converts to lowercase canonical path so suppression works regardless of
// how the path was constructed (relative vs absolute, slash direction, etc.)
std::string GameScanner::normalizePath(const std::string& path) {
    std::string norm;
    try {
        norm = fs::canonical(path).string();
    } catch (...) {
        norm = fs::absolute(path).string();
    }
    std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
    return norm;
}

// ─── Default search paths per platform ───────────────────────────────────────
std::vector<std::string> GameScanner::defaultPaths() {
    std::vector<std::string> paths;

#if defined(_WIN32)
    // Windows: check common locations
    if (const char* appdata = std::getenv("APPDATA")) {
        paths.push_back(std::string(appdata) + "\\HaackStation\\roms");
    }
    paths.push_back("C:\\Games\\PS1");
    paths.push_back("C:\\ROMs\\PS1");
    paths.push_back("C:\\ROMs\\PlayStation");

#elif defined(__ANDROID__)
    // Android: standard external storage locations
    paths.push_back("/sdcard/ROMs/PS1");
    paths.push_back("/sdcard/ROMs/PlayStation");
    paths.push_back("/sdcard/HaackStation/roms");
    paths.push_back("/storage/emulated/0/ROMs/PS1");

#else
    // Linux
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::string(home) + "/.config/haackstation/roms");
        paths.push_back(std::string(home) + "/ROMs/PS1");
        paths.push_back(std::string(home) + "/Games/PS1");
    }
#endif

    return paths;
}
