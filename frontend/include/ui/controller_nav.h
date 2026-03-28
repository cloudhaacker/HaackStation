#pragma once
// controller_nav.h
// Translates raw SDL gamepad + keyboard events into abstract UI navigation signals.
// Every screen in HaackStation reads from this — no screen ever reads SDL directly.
// This means the entire UI works with any controller, keyboard, or the Ayn Thor's
// built-in controls without any per-screen input code.

#include <SDL2/SDL.h>
#include <array>
#include <functional>
#include <string>

// ─── Abstract navigation actions ──────────────────────────────────────────────
enum class NavAction {
    NONE,
    UP, DOWN, LEFT, RIGHT,  // D-pad / left stick
    CONFIRM,                // A / Cross
    BACK,                   // B / Circle
    MENU,                   // Start / Options  
    OPTIONS,                // Y / Triangle — context menu
    PAGE_UP,                // L1/L2
    PAGE_DOWN,              // R1/R2
    SHOULDER_L,             // L1
    SHOULDER_R,             // R1
};

// ─── Repeat settings for held directions ──────────────────────────────────────
struct NavRepeat {
    int initialDelayMs = 400;   // How long to hold before repeat starts
    int repeatIntervalMs = 120; // How fast repeats fire after that
};

class ControllerNav {
public:
    ControllerNav();
    ~ControllerNav();

    // Call once per frame with the SDL event — returns the abstract action
    NavAction processEvent(const SDL_Event& e);

    // Call each frame even without an event (handles held-direction repeats)
    NavAction updateHeld(Uint32 nowMs);

    // Controller lifecycle
    void onControllerAdded(int deviceIndex);
    void onControllerRemoved(int instanceId);

    bool hasController() const { return m_controller != nullptr; }
    std::string controllerName() const;

    // Vibration — short pulse for confirm, double for error
    void rumbleConfirm();
    void setInputCooldown(int ms);  // Block all input for N milliseconds
    void rumbleError();

    void setRepeat(NavRepeat r) { m_repeat = r; }

private:
    NavAction sdlButtonToAction(SDL_GameControllerButton btn) const;
    NavAction sdlKeyToAction(SDL_Keycode key) const;
    NavAction axisToAction(Sint16 x, Sint16 y) const;

    SDL_GameController* m_controller = nullptr;

    // Held-direction repeat state
    NavAction m_heldAction    = NavAction::NONE;
    Uint32    m_heldSince     = 0;
    Uint32    m_lastRepeat    = 0;
	Uint32    m_cooldownUntil = 0;
    bool      m_repeatFired   = false;

    NavRepeat m_repeat;

    static constexpr Sint16 AXIS_DEAD_ZONE = 10000;
};
