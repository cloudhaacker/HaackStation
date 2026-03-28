#include "controller_nav.h"
#include <iostream>

ControllerNav::ControllerNav() {
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            onControllerAdded(i);
            break;
        }
    }
}

ControllerNav::~ControllerNav() {
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
}

void ControllerNav::onControllerAdded(int deviceIndex) {
    if (!m_controller && SDL_IsGameController(deviceIndex)) {
        m_controller = SDL_GameControllerOpen(deviceIndex);
        if (m_controller)
            std::cout << "[ControllerNav] Connected: " << controllerName() << "\n";
    }
}

void ControllerNav::onControllerRemoved(int instanceId) {
    if (m_controller) {
        SDL_Joystick* joy = SDL_GameControllerGetJoystick(m_controller);
        if (SDL_JoystickInstanceID(joy) == instanceId) {
            SDL_GameControllerClose(m_controller);
            m_controller = nullptr;
            std::cout << "[ControllerNav] Disconnected\n";
        }
    }
}

std::string ControllerNav::controllerName() const {
    if (!m_controller) return "None";
    const char* name = SDL_GameControllerName(m_controller);
    return name ? name : "Unknown Controller";
}

NavAction ControllerNav::processEvent(const SDL_Event& e) {
    if (SDL_GetTicks() < m_cooldownUntil) return NavAction::NONE;
    NavAction action = NavAction::NONE;

    switch (e.type) {
        case SDL_CONTROLLERBUTTONDOWN:
            action = sdlButtonToAction(
                static_cast<SDL_GameControllerButton>(e.cbutton.button));
            if (action == NavAction::UP   || action == NavAction::DOWN ||
                action == NavAction::LEFT || action == NavAction::RIGHT) {
                m_heldAction  = action;
                m_heldSince   = SDL_GetTicks();
                m_lastRepeat  = 0;
                m_repeatFired = false;
            }
            break;

        case SDL_CONTROLLERBUTTONUP: {
            NavAction released = sdlButtonToAction(
                static_cast<SDL_GameControllerButton>(e.cbutton.button));
            if (released == m_heldAction)
                m_heldAction = NavAction::NONE;
            break;
        }

        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                Sint16 x = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTX);
                Sint16 y = SDL_GameControllerGetAxis(m_controller, SDL_CONTROLLER_AXIS_LEFTY);
                action = axisToAction(x, y);
                if (action != NavAction::NONE && action != m_heldAction) {
                    m_heldAction  = action;
                    m_heldSince   = SDL_GetTicks();
                    m_lastRepeat  = 0;
                    m_repeatFired = false;
                } else if (action == NavAction::NONE) {
                    m_heldAction = NavAction::NONE;
                }
            }
            break;

        case SDL_KEYDOWN:
            action = sdlKeyToAction(e.key.keysym.sym);
            if (action == NavAction::UP   || action == NavAction::DOWN ||
                action == NavAction::LEFT || action == NavAction::RIGHT ||
                action == NavAction::SHOULDER_L || action == NavAction::SHOULDER_R) {
                m_heldAction  = action;
                m_heldSince   = SDL_GetTicks();
                m_lastRepeat  = 0;
                m_repeatFired = false;
            }
            break;

        case SDL_KEYUP: {
            NavAction released = sdlKeyToAction(e.key.keysym.sym);
            if (released == m_heldAction)
                m_heldAction = NavAction::NONE;
            break;
        }

        default:
            break;
    }

    return action;
}

NavAction ControllerNav::updateHeld(Uint32 nowMs) {
    if (m_heldAction == NavAction::NONE) return NavAction::NONE;

    Uint32 held = nowMs - m_heldSince;
    if (!m_repeatFired && held >= static_cast<Uint32>(m_repeat.initialDelayMs)) {
        m_repeatFired = true;
        m_lastRepeat  = nowMs;
        return m_heldAction;
    }
    if (m_repeatFired &&
        (nowMs - m_lastRepeat) >= static_cast<Uint32>(m_repeat.repeatIntervalMs)) {
        m_lastRepeat = nowMs;
        return m_heldAction;
    }
    return NavAction::NONE;
}

NavAction ControllerNav::sdlButtonToAction(SDL_GameControllerButton btn) const {
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return NavAction::UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return NavAction::DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return NavAction::LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return NavAction::RIGHT;
        case SDL_CONTROLLER_BUTTON_A:             return NavAction::CONFIRM;
        case SDL_CONTROLLER_BUTTON_B:             return NavAction::BACK;
        case SDL_CONTROLLER_BUTTON_START:         return NavAction::MENU;
        case SDL_CONTROLLER_BUTTON_Y:             return NavAction::OPTIONS;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return NavAction::SHOULDER_L;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NavAction::SHOULDER_R;
        default: return NavAction::NONE;
    }
}

// ─── Keyboard mapping ─────────────────────────────────────────────────────────
//
// UI NAVIGATION (all menus, shelf, settings, details panel):
//   Arrow keys     → D-pad / directional navigation
//   X              → Confirm (maps to Cross / controller A)
//   Z              → Back    (maps to Circle / controller B)
//   Page Up/Down   → L1/R1   (screenshot cycling in details panel)
//
// APP-LEVEL SHORTCUTS (intercepted in app.cpp BEFORE this function):
//   Enter          → Start / Open Settings from shelf
//   Space          → Select
//   Escape         → Quit game / quit app
//   F1             → Toggle in-game menu
//   F2             → Open details panel (Y button)
//   F11            → Toggle fullscreen
//
// IN-GAME PS1 BUTTONS (read via SDL_GetKeyboardState each frame, NOT events):
//   Arrow keys → D-pad
//   X → Cross(×)  Z → Circle(○)  A → Square(□)  S → Triangle(△)
//   Q → L1   W → R1   E → L2   R → R2
//   Enter → Start   Space → Select
//
// WASD is intentionally absent from UI nav — A/S/W are in-game PS1 buttons.
// Using WASD for navigation would make settings/shelf unusable during a game.

NavAction ControllerNav::sdlKeyToAction(SDL_Keycode key) const {
    switch (key) {
        case SDLK_UP:        return NavAction::UP;
        case SDLK_DOWN:      return NavAction::DOWN;
        case SDLK_LEFT:      return NavAction::LEFT;
        case SDLK_RIGHT:     return NavAction::RIGHT;
        case SDLK_x:         return NavAction::CONFIRM;
        case SDLK_z:         return NavAction::BACK;
        case SDLK_PAGEUP:    return NavAction::SHOULDER_L;   // L1 / cycle screenshots left
        case SDLK_PAGEDOWN:  return NavAction::SHOULDER_R;   // R1 / cycle screenshots right
        default:             return NavAction::NONE;
    }
}

NavAction ControllerNav::axisToAction(Sint16 x, Sint16 y) const {
    if (y < -AXIS_DEAD_ZONE) return NavAction::UP;
    if (y >  AXIS_DEAD_ZONE) return NavAction::DOWN;
    if (x < -AXIS_DEAD_ZONE) return NavAction::LEFT;
    if (x >  AXIS_DEAD_ZONE) return NavAction::RIGHT;
    return NavAction::NONE;
}

void ControllerNav::rumbleConfirm() {
    if (m_controller)
        SDL_GameControllerRumble(m_controller, 0x4000, 0x4000, 80);
}

void ControllerNav::rumbleError() {
    if (m_controller) {
        SDL_GameControllerRumble(m_controller, 0xFFFF, 0x0000, 120);
        SDL_Delay(150);
        SDL_GameControllerRumble(m_controller, 0xFFFF, 0x0000, 120);
    }
}

void ControllerNav::setInputCooldown(int ms) {
    m_cooldownUntil = SDL_GetTicks() + (Uint32)ms;
}
