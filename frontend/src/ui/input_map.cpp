#include "input_map.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

// ─── CtrlBinding::displayName ─────────────────────────────────────────────────
std::string CtrlBinding::displayName() const {
    if (isUnbound()) return "---";
    if (sdlButton == AXIS_L2) return "L2 (Trigger)";
    if (sdlButton == AXIS_R2) return "R2 (Trigger)";
    switch ((SDL_GameControllerButton)sdlButton) {
        case SDL_CONTROLLER_BUTTON_A:             return "A (Cross)";
        case SDL_CONTROLLER_BUTTON_B:             return "B (Circle)";
        case SDL_CONTROLLER_BUTTON_X:             return "X (Square)";
        case SDL_CONTROLLER_BUTTON_Y:             return "Y (Triangle)";
        case SDL_CONTROLLER_BUTTON_BACK:          return "Back / Select";
        case SDL_CONTROLLER_BUTTON_START:         return "Start";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "L1";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "R1";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L3 (Click)";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R3 (Click)";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "D-Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "D-Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "D-Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "D-Right";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Button %d", sdlButton);
            return buf;
        }
    }
}

// ─── KeyBinding::displayName ──────────────────────────────────────────────────
std::string KeyBinding::displayName() const {
    if (isUnbound()) return "---";
    const char* name = SDL_GetScancodeName(scancode);
    return name ? name : "?";
}

// ─── InputMap::setDefaults ────────────────────────────────────────────────────
// Mirrors the hardcoded mapping in app.cpp updateGameInput().
void InputMap::setDefaults() {
    // Controller defaults
    // SDL A → PS1 Cross, SDL B → PS1 Circle, SDL X → PS1 Square, SDL Y → PS1 Triangle
    ctrl[(int)PS1Button::B].sdlButton      = SDL_CONTROLLER_BUTTON_A;          // Cross
    ctrl[(int)PS1Button::A].sdlButton      = SDL_CONTROLLER_BUTTON_B;          // Circle
    ctrl[(int)PS1Button::Y].sdlButton      = SDL_CONTROLLER_BUTTON_X;          // Square
    ctrl[(int)PS1Button::X].sdlButton      = SDL_CONTROLLER_BUTTON_Y;          // Triangle
    ctrl[(int)PS1Button::SELECT].sdlButton = SDL_CONTROLLER_BUTTON_BACK;
    ctrl[(int)PS1Button::START].sdlButton  = SDL_CONTROLLER_BUTTON_START;
    ctrl[(int)PS1Button::UP].sdlButton     = SDL_CONTROLLER_BUTTON_DPAD_UP;
    ctrl[(int)PS1Button::DOWN].sdlButton   = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    ctrl[(int)PS1Button::LEFT].sdlButton   = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    ctrl[(int)PS1Button::RIGHT].sdlButton  = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    ctrl[(int)PS1Button::L].sdlButton      = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    ctrl[(int)PS1Button::R].sdlButton      = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    ctrl[(int)PS1Button::L2].sdlButton     = CtrlBinding::AXIS_L2;
    ctrl[(int)PS1Button::R2].sdlButton     = CtrlBinding::AXIS_R2;
    ctrl[(int)PS1Button::L3].sdlButton     = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    ctrl[(int)PS1Button::R3].sdlButton     = SDL_CONTROLLER_BUTTON_RIGHTSTICK;

    // Keyboard defaults — match app.cpp keyboard bindings
    keys[(int)PS1Button::B].scancode      = SDL_SCANCODE_X;       // Cross
    keys[(int)PS1Button::A].scancode      = SDL_SCANCODE_Z;       // Circle
    keys[(int)PS1Button::Y].scancode      = SDL_SCANCODE_A;       // Square
    keys[(int)PS1Button::X].scancode      = SDL_SCANCODE_S;       // Triangle
    keys[(int)PS1Button::SELECT].scancode = SDL_SCANCODE_SPACE;
    keys[(int)PS1Button::START].scancode  = SDL_SCANCODE_RETURN;
    keys[(int)PS1Button::UP].scancode     = SDL_SCANCODE_UP;
    keys[(int)PS1Button::DOWN].scancode   = SDL_SCANCODE_DOWN;
    keys[(int)PS1Button::LEFT].scancode   = SDL_SCANCODE_LEFT;
    keys[(int)PS1Button::RIGHT].scancode  = SDL_SCANCODE_RIGHT;
    keys[(int)PS1Button::L].scancode      = SDL_SCANCODE_Q;
    keys[(int)PS1Button::R].scancode      = SDL_SCANCODE_W;
    keys[(int)PS1Button::L2].scancode     = SDL_SCANCODE_E;
    keys[(int)PS1Button::R2].scancode     = SDL_SCANCODE_R;
    keys[(int)PS1Button::L3].scancode     = SDL_SCANCODE_UNKNOWN; // no default
    keys[(int)PS1Button::R3].scancode     = SDL_SCANCODE_UNKNOWN;
}

// ─── buildMask ────────────────────────────────────────────────────────────────
int InputMap::buildMask(SDL_GameController* controller, const Uint8* ks) const {
    int mask = 0;

    for (int i = 0; i < (int)PS1Button::COUNT; i++) {
        bool pressed = false;

        // Controller
        if (controller && !ctrl[i].isUnbound()) {
            if (ctrl[i].isAxis()) {
                SDL_GameControllerAxis axis = (ctrl[i].sdlButton == CtrlBinding::AXIS_L2)
                    ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                    : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                pressed = SDL_GameControllerGetAxis(controller, axis) > 8000;
            } else {
                pressed = SDL_GameControllerGetButton(
                    controller, (SDL_GameControllerButton)ctrl[i].sdlButton) != 0;
            }
        }

        // Keyboard (OR with controller)
        if (ks && !keys[i].isUnbound()) {
            pressed = pressed || (ks[keys[i].scancode] != 0);
        }

        if (pressed)
            mask |= (1 << i);
    }

    return mask;
}

// ─── Minimal JSON helpers ─────────────────────────────────────────────────────
static std::string jsonStr(const std::string& key, const std::string& val) {
    return "\"" + key + "\": \"" + val + "\"";
}
static std::string jsonInt(const std::string& key, int val) {
    return "\"" + key + "\": " + std::to_string(val);
}
static int parseJsonInt(const std::string& json, const std::string& key, int def = -1) {
    std::string search = "\"" + key + "\":";
    auto p = json.find(search);
    if (p == std::string::npos) return def;
    p += search.size();
    while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
    if (p >= json.size()) return def;
    try { return std::stoi(json.substr(p)); } catch (...) { return def; }
}

// ─── save ─────────────────────────────────────────────────────────────────────
bool InputMap::save(const std::string& path) const {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) { std::cerr << "[InputMap] Cannot write: " << path << "\n"; return false; }

    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"controller\": [\n";
    for (int i = 0; i < (int)PS1Button::COUNT; i++) {
        f << "    { " << jsonInt("ps1", i) << ", "
                      << jsonInt("sdl", ctrl[i].sdlButton) << " }";
        if (i < (int)PS1Button::COUNT - 1) f << ",";
        f << "\n";
    }
    f << "  ],\n";
    f << "  \"keyboard\": [\n";
    for (int i = 0; i < (int)PS1Button::COUNT; i++) {
        f << "    { " << jsonInt("ps1", i) << ", "
                      << jsonInt("scan", (int)keys[i].scancode) << " }";
        if (i < (int)PS1Button::COUNT - 1) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";

    std::cout << "[InputMap] Saved to " << path << "\n";
    return true;
}

// ─── load ─────────────────────────────────────────────────────────────────────
bool InputMap::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cout << "[InputMap] No map at " << path << " — using defaults\n"; return false; }

    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    // Parse each array entry: { "ps1": N, "sdl": M } or { "ps1": N, "scan": M }
    setDefaults(); // start from defaults, overlay what we find

    // Simple line-by-line parse (our own controlled format, not arbitrary JSON)
    std::istringstream ss(json);
    std::string line;
    bool inCtrl = false, inKeys = false;
    while (std::getline(ss, line)) {
        if (line.find("\"controller\"") != std::string::npos) { inCtrl = true; inKeys = false; continue; }
        if (line.find("\"keyboard\"")   != std::string::npos) { inCtrl = false; inKeys = true;  continue; }
        if (line.find('{') == std::string::npos) continue;

        int ps1 = parseJsonInt(line, "ps1", -1);
        if (ps1 < 0 || ps1 >= (int)PS1Button::COUNT) continue;

        if (inCtrl) {
            int sdl = parseJsonInt(line, "sdl", CtrlBinding::UNBOUND);
            ctrl[ps1].sdlButton = sdl;
        } else if (inKeys) {
            int sc = parseJsonInt(line, "scan", (int)SDL_SCANCODE_UNKNOWN);
            keys[ps1].scancode = (SDL_Scancode)sc;
        }
    }

    std::cout << "[InputMap] Loaded from " << path << "\n";
    return true;
}
