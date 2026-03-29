#include "play_history.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

PlayHistory::PlayHistory() {
    m_filePath = configDir() + "recently_played.json";
}

std::string PlayHistory::configDir() const {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    return appdata ? std::string(appdata) + "\\HaackStation\\" : ".\\";
#elif defined(__ANDROID__)
    return "/sdcard/HaackStation/";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/haackstation/" : "./";
#endif
}

// ─── Load ─────────────────────────────────────────────────────────────────────
// Minimal hand-rolled JSON parser — avoids a dependency for a simple format.
// Format: [{"path":"...","title":"...","lastPlayed":12345}, ...]
bool PlayHistory::load() {
    std::ifstream f(m_filePath);
    if (!f.is_open()) return false;

    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    m_entries.clear();

    // Walk through objects
    size_t pos = 0;
    while (pos < json.size()) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        size_t objEnd = json.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string obj = json.substr(objStart, objEnd - objStart + 1);

        auto extractStr = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            size_t p = obj.find(search);
            if (p == std::string::npos) return "";
            p += search.size();
            size_t end = obj.find('"', p);
            if (end == std::string::npos) return "";
            // Unescape basic sequences
            std::string val;
            for (size_t i = p; i < end; i++) {
                if (obj[i] == '\\' && i + 1 < end) {
                    i++;
                    if      (obj[i] == 'n')  val += '\n';
                    else if (obj[i] == 't')  val += '\t';
                    else if (obj[i] == '"')  val += '"';
                    else if (obj[i] == '\\') val += '\\';
                    else                     val += obj[i];
                } else {
                    val += obj[i];
                }
            }
            return val;
        };

        auto extractInt = [&](const std::string& key) -> int64_t {
            std::string search = "\"" + key + "\":";
            size_t p = obj.find(search);
            if (p == std::string::npos) return 0;
            p += search.size();
            while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) p++;
            int64_t val = 0;
            bool neg = (p < obj.size() && obj[p] == '-');
            if (neg) p++;
            while (p < obj.size() && isdigit(obj[p]))
                val = val * 10 + (obj[p++] - '0');
            return neg ? -val : val;
        };

        PlayHistoryEntry entry;
        entry.path       = extractStr("path");
        entry.title      = extractStr("title");
        entry.lastPlayed = extractInt("lastPlayed");

        if (!entry.path.empty() && !entry.title.empty())
            m_entries.push_back(entry);

        pos = objEnd + 1;
    }

    std::cout << "[PlayHistory] Loaded " << m_entries.size()
              << " entries from " << m_filePath << "\n";
    return true;
}

// ─── Save ─────────────────────────────────────────────────────────────────────
bool PlayHistory::save() const {
    fs::create_directories(fs::path(m_filePath).parent_path());
    std::ofstream f(m_filePath);
    if (!f.is_open()) {
        std::cerr << "[PlayHistory] Could not write: " << m_filePath << "\n";
        return false;
    }

    auto escapeStr = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        return out;
    };

    f << "[\n";
    for (size_t i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        f << "  {"
          << "\"path\":\"" << escapeStr(e.path) << "\","
          << "\"title\":\"" << escapeStr(e.title) << "\","
          << "\"lastPlayed\":" << e.lastPlayed
          << "}";
        if (i + 1 < m_entries.size()) f << ",";
        f << "\n";
    }
    f << "]\n";

    return true;
}

// ─── Record play ──────────────────────────────────────────────────────────────
void PlayHistory::recordPlay(const std::string& path, const std::string& title) {
    // Remove existing entry for this path if present
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [&](const PlayHistoryEntry& e) { return e.path == path; }),
        m_entries.end()
    );

    // Add to front
    PlayHistoryEntry entry;
    entry.path  = path;
    entry.title = title;
    entry.lastPlayed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_entries.insert(m_entries.begin(), entry);

    // Trim to max
    if ((int)m_entries.size() > MAX_HISTORY)
        m_entries.resize(MAX_HISTORY);

    save();
    std::cout << "[PlayHistory] Recorded: " << title << "\n";
}

void PlayHistory::clear() {
    m_entries.clear();
    save();
}
