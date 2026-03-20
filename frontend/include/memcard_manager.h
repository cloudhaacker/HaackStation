#pragma once
// memcard_manager.h
// Folder-based memory card system inspired by PCSX2's approach.
//
// Instead of fixed-size .mcr files with a hard 15-block limit,
// HaackStation uses a folder-per-memory-card system where each
// save is stored as an individual file. The folder is presented
// to the Beetle core as a virtual memory card.
//
// Two modes:
//
//   SHARED (default):
//     All games share memcards/shared/MemoryCard1.mcr
//     Same behavior as a physical memory card plugged into both slots
//
//   PER-GAME:
//     Each game gets its own memory card file
//     memcards/per_game/SCUS-94900_1.mcr  (slot 1)
//     memcards/per_game/SCUS-94900_2.mcr  (slot 2)
//     Never worry about running out of space
//     No accidental overwrites between games
//
// The mode can be set globally or per-game via PerGameSettings.

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

enum class MemCardMode {
    SHARED,     // All games share one memory card (default)
    PER_GAME,   // Each game gets its own memory card file
};

class MemCardManager {
public:
    MemCardManager();

    void setBaseDir(const std::string& dir) { m_baseDir = dir; }
    void setMode(MemCardMode mode)          { m_mode = mode; }
    MemCardMode mode() const                { return m_mode; }

    // Call before loading a game — prepares the memory card paths
    // Returns the path the core should use for slot 1 and slot 2
    std::string prepareSlot1(const std::string& gameSerial);
    std::string prepareSlot2(const std::string& gameSerial);

    // Returns human-readable info about current memory card state
    std::string statusString(const std::string& gameSerial) const;

    // List all per-game memory card files
    std::vector<std::string> listPerGameCards() const;

    // Get size used by a memory card in bytes
    static size_t cardSize(const std::string& path);

    // Standard PS1 memory card size (128KB)
    static constexpr size_t STANDARD_CARD_BYTES = 131072;

private:
    std::string slotPath(int slot, const std::string& gameSerial) const;
    void ensureDirectories() const;

    std::string m_baseDir = "memcards/";
    MemCardMode m_mode    = MemCardMode::SHARED;
};
