#include "controller_nav.h"
#include <iostream>

ControllerNav::ControllerNav() {
    SDL_GameControllerEventState(SDL_ENABLE);
    // Auto-open first available controller
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
    NavAction action = NavAction::NONE;

    switch (e.type) {
        case SDL_CONTROLLERBUTTONDOWN:
            action = sdlButtonToAction(
                static_cast<SDL_GameControllerButton>(e.cbutton.button));
            // Start held tracking for directional actions
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
                action == NavAction::LEFT || action == NavAction::RIGHT) {
                m_heldAction  = action;
                m_heldSince   = SDL_GetTicks();
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
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return NavAction::UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return NavAction::DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return NavAction::LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NavAction::RIGHT;
        case SDL_CONTROLLER_BUTTON_A:          return NavAction::CONFIRM;
        case SDL_CONTROLLER_BUTTON_B:          return NavAction::BACK;
        case SDL_CONTROLLER_BUTTON_START:      return NavAction::MENU;
        case SDL_CONTROLLER_BUTTON_Y:          return NavAction::OPTIONS;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return NavAction::SHOULDER_L;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NavAction::SHOULDER_R;
        default: return NavAction::NONE;
    }
}

NavAction ControllerNav::sdlKeyToAction(SDL_Keycode key) const {
    switch (key) {
        case SDLK_UP:    case SDLK_w: return NavAction::UP;
        case SDLK_DOWN:  case SDLK_s: return NavAction::DOWN;
        case SDLK_LEFT:  case SDLK_a: return NavAction::LEFT;
        case SDLK_RIGHT: case SDLK_d: return NavAction::RIGHT;
        case SDLK_RETURN: case SDLK_SPACE: return NavAction::CONFIRM;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: return NavAction::BACK;
        case SDLK_TAB:   return NavAction::MENU;
        case SDLK_PAGEUP:   return NavAction::PAGE_UP;
        case SDLK_PAGEDOWN: return NavAction::PAGE_DOWN;
        default: return NavAction::NONE;
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
