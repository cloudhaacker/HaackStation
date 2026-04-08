#pragma once
// favorites.h
// Stores the player's favorite games and persists them to disk.
//
// Follows the same pattern as PlayHistory — simple JSON file,
// loaded at startup, saved immediately on any change.
//
// Format (JSON):
//   [ "C:/roms/Alundra.chd", "C:/roms/Legend of Dragoon.m3u", ... ]
//
// Usage:
//   FavoriteManager favs;
//   favs.load();
//   favs.toggle(game.path);   // add if absent, remove if present
//   bool fav = favs.isFavorite(game.path);

#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>

class FavoriteManager {
public:
    FavoriteManager() = default;

    bool load(const std::string& path = "saves/favorites.json") {
        m_filePath = path;
        m_paths.clear();

        std::ifstream f(path);
        if (!f.is_open()) return false; // not an error on first run

        // Simple hand-rolled JSON array parser — no dependency needed
        // Format: [ "path1", "path2", ... ]
        std::string line, all;
        while (std::getline(f, line)) all += line;

        size_t pos = 0;
        while ((pos = all.find('"', pos)) != std::string::npos) {
            pos++; // skip opening quote
            // Parse until closing quote, handling escape sequences
            std::string entry;
            while (pos < all.size() && all[pos] != '"') {
                if (all[pos] == '\\' && pos + 1 < all.size()) {
                    pos++; // skip backslash
                    if      (all[pos] == '\\') entry += '\\';
                    else if (all[pos] == '"')  entry += '"';
                    else if (all[pos] == 'n')  entry += '\n';
                    else if (all[pos] == 't')  entry += '\t';
                    else                       entry += all[pos];
                } else {
                    entry += all[pos];
                }
                pos++;
            }
            if (!entry.empty()) m_paths.push_back(entry);
            pos++; // skip closing quote
        }
        std::cout << "[Favorites] Loaded " << m_paths.size() << " favorites\n";
        return true;
    }

    bool save() const {
        std::ofstream f(m_filePath);
        if (!f.is_open()) {
            std::cerr << "[Favorites] Cannot write to: " << m_filePath << "\n";
            return false;
        }
        f << "[\n";
        for (size_t i = 0; i < m_paths.size(); i++) {
            // Escape backslashes for JSON
            std::string escaped;
            for (char c : m_paths[i]) {
                if (c == '\\') escaped += "\\\\";
                else if (c == '"') escaped += "\\\"";
                else escaped += c;
            }
            f << "  \"" << escaped << "\"";
            if (i + 1 < m_paths.size()) f << ",";
            f << "\n";
        }
        f << "]\n";
        return true;
    }

    bool isFavorite(const std::string& path) const {
        return std::find(m_paths.begin(), m_paths.end(), path) != m_paths.end();
    }

    // Returns true if game was added, false if it was removed
    bool toggle(const std::string& path) {
        auto it = std::find(m_paths.begin(), m_paths.end(), path);
        if (it != m_paths.end()) {
            m_paths.erase(it);
            save();
            std::cout << "[Favorites] Removed: " << path << "\n";
            return false;
        } else {
            m_paths.push_back(path);
            save();
            std::cout << "[Favorites] Added: " << path << "\n";
            return true;
        }
    }

    void remove(const std::string& path) {
        auto it = std::find(m_paths.begin(), m_paths.end(), path);
        if (it != m_paths.end()) { m_paths.erase(it); save(); }
    }

    const std::vector<std::string>& paths() const { return m_paths; }

    void clear() { m_paths.clear(); save(); }

private:
    std::string              m_filePath = "saves/favorites.json";
    std::vector<std::string> m_paths;
};
