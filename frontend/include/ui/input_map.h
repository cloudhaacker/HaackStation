#pragma once
// input_map.h
// Stores and serialises the full controller + keyboard button map.
//
// There are two separate mapping tables:
//
//   1. PS1 game buttons — what physical button sends which PS1 signal
//      e.g. SDL A button → PS1 Cross (RETRO_DEVICE_ID_JOYPAD_B)
//
//   2. Frontend hotkeys — fixed actions that are always intercepted
//      e.g. R2 hold = fast-forward, R1+R2 hold = turbo toggle
//      These are shown read-only in the remap screen (not editable yet,
//      but displayed so the user knows what they are).
//
// Saved to: saves/input_map.json
// One global map; per-game overrides stored alongside PerGameSettings.

#include <SDL2/SDL.h>
#include <string>
#include <array>
#include <cstdint>

// ─── PS1 button IDs (matches libretro joypad IDs) ────────────────────────────
// These are the values written into the bitmask in updateGameInput().
enum class PS1Button : int {
    B      = 0,   // Cross    (×)
    Y      = 1,   // Square   (□)
    SELECT = 2,
    START  = 3,
    UP     = 4,
    DOWN   = 5,
    LEFT   = 6,
    RIGHT  = 7,
    A      = 8,   // Circle   (○)
    X      = 9,   // Triangle (△)
    L      = 10,  // L1
    R      = 11,  // R1
    L2     = 12,
    R2     = 13,
    L3     = 14,
    R3     = 15,
    COUNT  = 16
};

inline const char* ps1ButtonName(PS1Button b) {
    switch (b) {
        case PS1Button::B:      return "Cross (×)";
        case PS1Button::Y:      return "Square (□)";
        case PS1Button::SELECT: return "Select";
        case PS1Button::START:  return "Start";
        case PS1Button::UP:     return "D-Pad Up";
        case PS1Button::DOWN:   return "D-Pad Down";
        case PS1Button::LEFT:   return "D-Pad Left";
        case PS1Button::RIGHT:  return "D-Pad Right";
        case PS1Button::A:      return "Circle (○)";
        case PS1Button::X:      return "Triangle (△)";
        case PS1Button::L:      return "L1";
        case PS1Button::R:      return "R1";
        case PS1Button::L2:     return "L2";
        case PS1Button::R2:     return "R2";
        case PS1Button::L3:     return "L3 (L-Stick Click)";
        case PS1Button::R3:     return "R3 (R-Stick Click)";
        default:                return "Unknown";
    }
}

// ─── A physical controller button binding ────────────────────────────────────
struct CtrlBinding {
    // SDL_CONTROLLER_BUTTON_* for regular buttons, or AXIS sentinel for triggers
    static constexpr int AXIS_L2 = 1000;  // left trigger
    static constexpr int AXIS_R2 = 1001;  // right trigger
    static constexpr int UNBOUND = -1;

    int sdlButton = UNBOUND;   // SDL_GameControllerButton or AXIS_* sentinel

    bool isUnbound()  const { return sdlButton == UNBOUND; }
    bool isAxis()     const { return sdlButton == AXIS_L2 || sdlButton == AXIS_R2; }

    std::string displayName() const;
};

// ─── A physical keyboard key binding ─────────────────────────────────────────
struct KeyBinding {
    static constexpr SDL_Scancode UNBOUND = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode scancode = UNBOUND;
    bool isUnbound() const { return scancode == UNBOUND; }
    std::string displayName() const;
};

// ─── The full input map ───────────────────────────────────────────────────────
struct InputMap {
    // Controller bindings — one per PS1 button
    std::array<CtrlBinding, (int)PS1Button::COUNT> ctrl;

    // Keyboard bindings — one per PS1 button
    std::array<KeyBinding, (int)PS1Button::COUNT> keys;

    // Load defaults that match the hardcoded mapping in app.cpp
    void setDefaults();

    // Save/load to saves/input_map.json
    bool save(const std::string& path = "saves/input_map.json") const;
    bool load(const std::string& path = "saves/input_map.json");

    // Apply this map — converts a set of live SDL states into a PS1 button bitmask.
    // ctrl: the currently connected gamepad (may be nullptr).
    // ks:   SDL_GetKeyboardState() result.
    // Returns bitmask compatible with LibretroBridge::setButtonState().
    int buildMask(SDL_GameController* ctrl, const Uint8* ks) const;
};
