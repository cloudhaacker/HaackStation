#pragma once
// per_game_settings.h
// Stores and retrieves per-game configuration overrides.
// Each game can have its own settings that override the global defaults.
//
// Examples:
//   - Enable texture replacement for Crash Bandicoot but not FF7
//   - Use CRT shader for some games, sharp bilinear for others
//   - Different internal resolution per game
// Settings are stored as simple INI-style files:
//   saves/per_game/SCUS-94900.cfg
//
// If no per-game config exists, global settings are used.

#include <string>
#include <unordered_map>

struct GameOverrides {
    // ── Video ──────────────────────────────────────────────────────────────────
    bool  overrideRenderer      = false;
    int   rendererChoice        = 0;    // 0=Software, 1=Hardware (OpenGL)

    bool  overrideResolution    = false;
    int   internalRes           = 0;    // index into resolution choices (see screen)

    bool  overrideShader        = false;
    int   shaderChoice          = 0;

    // ── Enhancements (existing) ────────────────────────────────────────────────
    bool  overrideTextures      = false;
    bool  textureReplacement    = false;

    bool  overrideAiUpscaling   = false;
    bool  aiUpscaling           = false;

    // ── Audio ──────────────────────────────────────────────────────────────────
    bool  overrideAudioReplace  = false;
    bool  audioReplacement      = false;

    // ── Run-Ahead (item 26) ────────────────────────────────────────────────────
    // Reduces perceived input lag by running the core N extra frames ahead
    // and rolling back. 1–2 frames is typical; 4 is the Beetle maximum.
    bool  overrideRunAhead      = false;
    int   runAheadFrames        = 0;    // 0=disabled, 1/2/3/4 = frame count

    // ── Widescreen hack + CPU overclock (item 27) ─────────────────────────────
    // Widescreen stretches the 3D projection to 16:9 without letterboxing.
    // Not all games look good with it — hence per-game.
    bool  overrideWidescreen    = false;
    bool  widescreenHack        = false;

    // CPU overclock helps games that have slowdown (e.g. Spyro water levels).
    bool  overrideCpuOverclock  = false;
    int   cpuOverclock          = 0;    // 0=1x, 1=2x, 2=4x, 3=8x

    // ── Overscan crop (item 28) ────────────────────────────────────────────────
    // Crops the ~8px black border present on many PS1 games.
    bool  overrideOverscan      = false;
    bool  cropOverscan          = false;

    // ── Bilinear filter (item 29) ─────────────────────────────────────────────
    // Smooths the 320×240 output. Some people love it, some hate it.
    bool  overrideBilinear      = false;
    int   filterChoice          = 0;    // 0=Nearest (sharp), 1=Bilinear, 2=3-point

    // ── Aspect ratio (item 30) ────────────────────────────────────────────────
    bool  overrideAspectRatio   = false;
    int   aspectRatioChoice     = 0;    // 0=4:3, 1=16:9, 2=8:7 (pixel-perfect)

    bool hasAnyOverride() const {
        return overrideRenderer    || overrideResolution  || overrideShader      ||
               overrideTextures    || overrideAiUpscaling || overrideAudioReplace||
               overrideRunAhead    || overrideWidescreen  || overrideCpuOverclock||
               overrideOverscan    || overrideBilinear    || overrideAspectRatio;
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
