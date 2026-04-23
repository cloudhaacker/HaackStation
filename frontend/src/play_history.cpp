#include "play_history.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;

// ─── formatPlaytime ───────────────────────────────────────────────────────────
std::string PlayHistory::formatPlaytime(uint64_t totalSeconds) {
    if (totalSeconds < 60)
        return "< 1m";

    uint64_t hours   = totalSeconds / 3600;
    uint64_t minutes = (totalSeconds % 3600) / 60;

    if (hours == 0)
        return std::to_string(minutes) + "m";

    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

// ─── findEntry ────────────────────────────────────────────────────────────────
PlayEntry* PlayHistory::findEntry(const std::string& path) {
    for (auto& e : m_entries)
        if (e.path == path) return &e;
    return nullptr;
}

const PlayEntry* PlayHistory::findEntry(const std::string& path) const {
    for (const auto& e : m_entries)
        if (e.path == path) return &e;
    return nullptr;
}

// ─── recordPlay ───────────────────────────────────────────────────────────────
// Called when a game is launched. Updates last-played timestamp, increments
// play count, and moves the entry to the front of the list.
void PlayHistory::recordPlay(const std::string& path, const std::string& title) {
    PlayEntry* existing = findEntry(path);
    if (existing) {
        // Update in place then move to front
        existing->title      = title; // title may have been cleaned up since last play
        existing->lastPlayed = std::time(nullptr);
        existing->playCount++;

        // Bubble to front
        PlayEntry copy = *existing;
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
            [&path](const PlayEntry& e) { return e.path == path; }),
            m_entries.end());
        m_entries.insert(m_entries.begin(), copy);
    } else {
        // New entry — insert at front
        PlayEntry e;
        e.path        = path;
        e.title       = title;
        e.totalSeconds= 0;
        e.lastPlayed  = std::time(nullptr);
        e.playCount   = 1;
        m_entries.insert(m_entries.begin(), e);
    }

    save();
    std::cout << "[PlayHistory] Recorded launch: " << title << "\n";
}

// ─── recordStop ───────────────────────────────────────────────────────────────
// Called when a game exits. Adds session duration to the accumulated total.
void PlayHistory::recordStop(const std::string& path, uint64_t sessionSeconds) {
    PlayEntry* entry = findEntry(path);
    if (!entry) {
        std::cout << "[PlayHistory] recordStop: path not found, ignoring\n";
        return;
    }

    entry->totalSeconds += sessionSeconds;
    save();

    std::cout << "[PlayHistory] Session: "
              << formatPlaytime(sessionSeconds)
              << "  Total: " << formatPlaytime(entry->totalSeconds)
              << "  (" << entry->title << ")\n";
}

// ─── Accessors ────────────────────────────────────────────────────────────────
uint64_t PlayHistory::getTotalSeconds(const std::string& path) const {
    const PlayEntry* e = findEntry(path);
    return e ? e->totalSeconds : 0;
}

int PlayHistory::getPlayCount(const std::string& path) const {
    const PlayEntry* e = findEntry(path);
    return e ? e->playCount : 0;
}

// ─── Minimal JSON helpers ─────────────────────────────────────────────────────
static std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

static std::string extractStr(const std::string& line, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    auto p = line.find(search);
    if (p == std::string::npos) return "";
    p += search.size();
    std::string val;
    while (p < line.size() && line[p] != '"') {
        if (line[p] == '\\' && p + 1 < line.size()) {
            p++;
            if      (line[p] == '"')  val += '"';
            else if (line[p] == '\\') val += '\\';
            else if (line[p] == 'n')  val += '\n';
            else                      val += line[p];
        } else {
            val += line[p];
        }
        p++;
    }
    return val;
}

static uint64_t extractU64(const std::string& line, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    auto p = line.find(search);
    if (p == std::string::npos) return 0;
    p += search.size();
    try { return std::stoull(line.substr(p)); } catch (...) { return 0; }
}

static int extractInt(const std::string& line, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    auto p = line.find(search);
    if (p == std::string::npos) return 0;
    p += search.size();
    try { return std::stoi(line.substr(p)); } catch (...) { return 0; }
}

// ─── save ─────────────────────────────────────────────────────────────────────
bool PlayHistory::save(const std::string& path) const {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) {
        std::cerr << "[PlayHistory] Cannot write: " << path << "\n";
        return false;
    }

    f << "{\n  \"version\": 2,\n  \"entries\": [\n";
    for (int i = 0; i < (int)m_entries.size(); i++) {
        const auto& e = m_entries[i];
        f << "    {\n";
        f << "      \"path\": \""         << escapeJson(e.path)  << "\",\n";
        f << "      \"title\": \""        << escapeJson(e.title) << "\",\n";
        f << "      \"totalSeconds\": "   << e.totalSeconds      << ",\n";
        f << "      \"lastPlayed\": "     << (uint64_t)e.lastPlayed << ",\n";
        f << "      \"playCount\": "      << e.playCount         << "\n";
        f << "    }";
        if (i < (int)m_entries.size() - 1) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    return true;
}

// ─── load ─────────────────────────────────────────────────────────────────────
// Parses our own controlled JSON format line-by-line.
// Version 1 files (no totalSeconds / playCount) load gracefully with defaults.
bool PlayHistory::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cout << "[PlayHistory] No history file at " << path << "\n";
        return false;
    }

    m_entries.clear();
    std::string line;
    PlayEntry   current;
    bool        inEntry = false;

    while (std::getline(f, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) line = line.substr(start);

        if (line == "{" && !inEntry) {
            // Could be root object or entry — track with inEntry
            continue;
        }
        if (line.find("\"path\"") != std::string::npos) {
            current      = PlayEntry{};
            inEntry      = true;
            current.path = extractStr(line, "path");
        }
        if (!inEntry) continue;

        if (line.find("\"title\"")        != std::string::npos)
            current.title        = extractStr(line, "title");
        if (line.find("\"totalSeconds\"") != std::string::npos)
            current.totalSeconds = extractU64(line, "totalSeconds");
        if (line.find("\"lastPlayed\"")   != std::string::npos)
            current.lastPlayed   = (time_t)extractU64(line, "lastPlayed");
        if (line.find("\"playCount\"")    != std::string::npos)
            current.playCount    = extractInt(line, "playCount");

        // End of entry object
        if ((line == "}," || line == "}") && inEntry && !current.path.empty()) {
            // Migrate v1: ensure playCount is at least 1 if path was recorded
            if (current.playCount == 0 && current.lastPlayed > 0)
                current.playCount = 1;
            m_entries.push_back(current);
            inEntry = false;
            current = PlayEntry{};
        }
    }

    std::cout << "[PlayHistory] Loaded " << m_entries.size() << " entries\n";
    return true;
}
