#pragma once
// per_game_settings.h
// Stores and retrieves per-game configuration overrides.
// Each game can have its own settings that override the global defaults.
//
// Examples:
//   - Enable texture replacement for Crash Bandicoot but not FF7
//   - Use CRT shader for some games, sharp bilinear for others
//   - Different internal resolution per game
//   - Game-specific memory card slot assignment
//
// Settings are stored as simple INI-style files:
//   saves/per_game/SCUS-94900.cfg
//
// If no per-game config exists, global settings are used.

#include <string>
#include <unordered_map>

struct GameOverrides {
    // Video
    bool  overrideRenderer      = false;
    int   rendererChoice        = 0;
    bool  overrideResolution    = false;
    int   internalRes           = 1;
    bool  overrideShader        = false;
    int   shaderChoice          = 0;

    // Enhancements
    bool  overrideTextures      = false;
    bool  textureReplacement    = false;
    bool  overrideAiUpscaling   = false;
    bool  aiUpscaling           = false;

    // Audio
    bool  overrideAudioReplace  = false;
    bool  audioReplacement      = false;

    // Memory card
    bool  overrideMemCard       = false;
    int   memCardSlot           = 0;   // 0 = shared, 1 = per-game

    bool hasAnyOverride() const {
        return overrideRenderer || overrideResolution || overrideShader ||
               overrideTextures || overrideAiUpscaling ||
               overrideAudioReplace || overrideMemCard;
    }
};

class PerGameSettings {
public:
    PerGameSettings();

    // Load settings for a game (by serial or filename)
    // Returns true if a per-game config was found
    bool load(const std::string& gameSerial, const std::string& gamePath);

    // Save current overrides for the game
    void save(const std::string& gameSerial, const GameOverrides& overrides);

    // Get the loaded overrides (call after load())
    const GameOverrides& overrides() const { return m_overrides; }
    GameOverrides&       overrides()       { return m_overrides; }

    // Clear overrides (revert to global settings)
    void clear() { m_overrides = GameOverrides{}; }

    bool isLoaded() const { return m_loaded; }

    void setConfigDir(const std::string& dir) { m_configDir = dir; }

private:
    std::string configPath(const std::string& serial) const;

    GameOverrides m_overrides;
    std::string   m_configDir = "saves/per_game/";
    std::string   m_currentSerial;
    bool          m_loaded = false;
};
