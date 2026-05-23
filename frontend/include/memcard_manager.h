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
#include <vector>
#include <filesystem>
#include <SDL2/SDL.h>

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

    // Returns just the directory the core should use as its save directory.
    // The core writes <GameTitle>.srm into this folder — by pointing it at
    // the same folder our .mcr lives in, the core and OmniSave agree on location.
    std::string saveDirectory(const std::string& gameSerial) const;

    // Returns human-readable info about current memory card state
    std::string statusString(const std::string& gameSerial) const;

    // List all per-game memory card files
    std::vector<std::string> listPerGameCards() const;

    // Get size used by a memory card in bytes
    static size_t cardSize(const std::string& path);

    // Standard PS1 memory card size (128KB)
    static constexpr size_t STANDARD_CARD_BYTES = 131072;

    // Returns the .mcr file path for slot 1 for the given serial.
    // Used by app.cpp to tell flushSaveRAM() exactly where to write.
    std::string activeCardPath(const std::string& gameSerial) const {
        return slotPath(1, gameSerial);
    }

    // ── OmniSave Import ───────────────────────────────────────────────────────

    /**
     * A parsed, display-ready view of a single PS1 memory card directory entry.
     * Used by the OmniSave import screen to show the user what they're importing.
     */
    struct ImportBlock {
        int         slotIndex   = -1;   // 0-14, directory frame index in source MCR
        std::string title;              // Decoded Shift-JIS title (or serial fallback)
        std::string serial;             // e.g. "SCUS-94163"
        int         blocksUsed  = 0;    // how many 8KB blocks this save occupies
        bool        isEmpty     = true; // true if slot is free / deleted
        bool        isCorrupted = false;// true if allocation chain is broken

        // SpriteCard animation frames — decoded icon pixels (16x16 each frame)
        std::vector<std::vector<SDL_Color>> frames;
        int frameCount = 0;
    };

    /**
     * Opens a .mcr file read-only and parses all 15 directory entries into
     * ImportBlock structs. Does NOT set this as the active card.
     *
     * @param path   Full filesystem path to the source .mcr
     * @param out    Populated on success; cleared on failure
     * @return true on success
     */
    bool loadCardForImport(const std::string& path,
                           std::vector<ImportBlock>& out);

    /**
     * Copies one save block from a source .mcr into the currently-active card.
     * The operation is atomic: writes to a temp file then renames.
     *
     * @param sourcePath   Path to the source .mcr file
     * @param slotIndex    Directory frame index (0-14) to copy
     * @param errorOut     Human-readable error if this returns false
     * @return true on success
     */
    bool importBlock(const std::string& sourcePath,
                     int                slotIndex,
                     const std::string& destCardPath,
                     std::string&       errorOut);

private:
    std::string slotPath(int slot, const std::string& gameSerial) const;
    void ensureDirectories() const;

    std::string m_baseDir = "memcards/";
    MemCardMode m_mode    = MemCardMode::SHARED;
};
