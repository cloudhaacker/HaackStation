#pragma once
// disc_memory.h
// Remembers which disc index the player was on for each multi-disc game.
// Persisted to saves/disc_memory.txt so it survives app restarts.
//
// Format (one entry per line):
//   <game_path_hash> <disc_index>
//
// Usage:
//   DiscMemory mem;
//   mem.load();
//   int disc = mem.getDisc(gamePath);   // returns 0 if never set
//   mem.setDisc(gamePath, 1);
//   mem.save();

#include <string>
#include <unordered_map>
#include <fstream>
#include <functional>
#include <iostream>

class DiscMemory {
public:
    void load(const std::string& path = "saves/disc_memory.txt") {
        m_path = path;
        m_data.clear();
        std::ifstream f(path);
        if (!f.is_open()) return;
        std::string hash;
        int idx;
        while (f >> hash >> idx)
            m_data[hash] = idx;
    }

    void save() const {
        std::ofstream f(m_path);
        for (const auto& [hash, idx] : m_data)
            f << hash << " " << idx << "\n";
    }

    int getDisc(const std::string& gamePath) const {
        auto it = m_data.find(hashPath(gamePath));
        return it != m_data.end() ? it->second : 0;
    }

    void setDisc(const std::string& gamePath, int discIndex) {
        m_data[hashPath(gamePath)] = discIndex;
        save(); // persist immediately
    }

private:
    static std::string hashPath(const std::string& path) {
        // Simple FNV-1a hash as hex string — stable, no collisions in practice
        uint32_t h = 2166136261u;
        for (unsigned char c : path) {
            h ^= c;
            h *= 16777619u;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%08x", h);
        return buf;
    }

    std::string m_path = "saves/disc_memory.txt";
    std::unordered_map<std::string, int> m_data;
};
