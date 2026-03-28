#pragma once
// save_state_manager.h
// Save state system for HaackStation.
//
// Save states capture the complete emulator state at any moment —
// CPU registers, RAM, GPU, SPU, everything. Unlike memory card saves
// (which the game controls), save states are instant and work anywhere.
//
// Storage layout:
//   saves/states/<GameTitle>/
//       slot_auto.state      <- auto-save on exit
//       slot_auto.png        <- thumbnail screenshot
//       slot_001.state
//       slot_001.png
//       slot_002.state
//       slot_002.png
//       ...
//
// Each .state file is the raw data from retro_serialize().
// Each .png is a 320x180 thumbnail screenshot of that moment.
//
// Slots are unlimited — named by timestamp if no slot number given.
// The auto-save slot is written every time you exit a game.

#include <string>
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>

struct SaveSlot {
    int         slotNumber  = -1;    // -1 = auto-save, 0+ = manual slots
    std::string statePath;           // Path to .state file
    std::string thumbPath;           // Path to .png thumbnail
    std::string timestamp;           // Human-readable "2026-03-19 21:04"
    bool        exists      = false;
    size_t      fileSize    = 0;     // Size of state file in bytes
    SDL_Texture* thumbnail  = nullptr; // Loaded thumbnail texture (nullable)
};

class LibretroBridge;

class SaveStateManager {
public:
    SaveStateManager();
    ~SaveStateManager();

    void setBridge(LibretroBridge* bridge) { m_bridge = bridge; }
    void setRenderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void setBaseDir(const std::string& dir)  { m_baseDir = dir; }

    // Set the current game — call before save/load operations
    void setCurrentGame(const std::string& gameTitle,
                        const std::string& gamePath);

    // Save to a specific slot (0-999), or -1 for auto-save
    bool saveState(int slot, SDL_Surface* screenshot = nullptr);

    // Load from a specific slot
    bool loadState(int slot);

    // Auto-save (called on game exit)
    bool autoSave(SDL_Surface* screenshot = nullptr);

    // Load auto-save if it exists
    bool loadAutoSave();
    bool hasAutoSave() const;

    // Get all save slots for current game
    std::vector<SaveSlot> listSlots() const;

    // Get a specific slot's info
    SaveSlot getSlot(int slot) const;

    // Load thumbnail texture for a slot (caller must destroy texture)
    SDL_Texture* loadThumbnail(const SaveSlot& slot) const;

    // Delete a save slot
    bool deleteSlot(int slot);

    // Capture a screenshot from the current frame
    SDL_Surface* captureScreenshot() const;
    SDL_Surface* captureCleanScreenshot() const; // No UI overlay

    bool isGameLoaded() const { return !m_gameTitle.empty(); }

private:
    std::string slotPath(int slot) const;
    std::string thumbPath(int slot) const;
    std::string stateDir() const;
    std::string formatTimestamp() const;
    bool saveThumbnail(SDL_Surface* screenshot, const std::string& path) const;
    void ensureDir() const;

    LibretroBridge* m_bridge   = nullptr;
    SDL_Renderer*   m_renderer = nullptr;
    std::string     m_baseDir  = "saves/states/";
    std::string     m_gameTitle;
    std::string     m_gamePath;
};
