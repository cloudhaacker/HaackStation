#include "remap_screen.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// ─── Construction ─────────────────────────────────────────────────────────────
RemapScreen::RemapScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                          ControllerNav* nav, InputMap* map)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_map(map)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

RemapScreen::~RemapScreen() {}

// ─── Controller detection ─────────────────────────────────────────────────────
std::string RemapScreen::detectControllerName() const {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            const char* name = SDL_GameControllerNameForIndex(i);
            return name ? name : "Unknown Controller";
        }
    }
    return "No controller detected";
}

std::string RemapScreen::detectControllerFamily() const {
    std::string name = detectControllerName();
    // Lowercase for comparison
    std::string low = name;
    for (char& c : low) c = (char)tolower(c);

    if (low.find("dualshock") != std::string::npos ||
        low.find("dualsense") != std::string::npos ||
        low.find("playstation") != std::string::npos ||
        low.find("ps4") != std::string::npos ||
        low.find("ps5") != std::string::npos)
        return "PlayStation";

    if (low.find("xbox") != std::string::npos ||
        low.find("xinput") != std::string::npos ||
        low.find("microsoft") != std::string::npos)
        return "Xbox";

    if (low.find("switch") != std::string::npos ||
        low.find("nintendo") != std::string::npos ||
        low.find("pro controller") != std::string::npos)
        return "Switch";

    return "Generic";
}

// ─── Events ───────────────────────────────────────────────────────────────────
void RemapScreen::handleEvent(const SDL_Event& e) {
    if (m_listening) return;  // poll-based while listening, ignore normal events

    // X button (SDL_CONTROLLER_BUTTON_X) = Reset Defaults
    // Handled here directly because NavAction doesn't have an EXTRA2 value for X.
    if (e.type == SDL_CONTROLLERBUTTONDOWN &&
        e.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
        resetDefaults();
        return;
    }
    // Keyboard equivalent: Delete key
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_DELETE) {
        resetDefaults();
        return;
    }

    // Y button = Configure All (NavAction::EXTRA if supported, else direct)
    if (e.type == SDL_CONTROLLERBUTTONDOWN &&
        e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
        beginConfigureAll();
        return;
    }

    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE)
        navigate(action);
}

void RemapScreen::update(float deltaMs) {
    m_listenPulse += deltaMs * 0.006f;  // pulse speed

    if (m_listening)
        pollListen();
}

// ─── Navigation ───────────────────────────────────────────────────────────────
void RemapScreen::navigate(NavAction action) {
    const int total = (int)PS1Button::COUNT;

    switch (action) {
        case NavAction::UP:
            if (m_selectedRow > 0) {
                m_selectedRow--;
                if (m_selectedRow < m_scrollOffset)
                    m_scrollOffset = m_selectedRow;
            }
            break;

        case NavAction::DOWN:
            if (m_selectedRow < total - 1) {
                m_selectedRow++;
                if (m_selectedRow >= m_scrollOffset + VISIBLE_ROWS)
                    m_scrollOffset = m_selectedRow - VISIBLE_ROWS + 1;
            }
            break;

        case NavAction::LEFT:
        case NavAction::RIGHT:
            // Switch between CONTROLLER and KEYBOARD tabs
            m_tab = (m_tab == RemapTab::CONTROLLER)
                    ? RemapTab::KEYBOARD
                    : RemapTab::CONTROLLER;
            break;

        case NavAction::CONFIRM:
            // A — edit current binding
            beginListen();
            break;

        case NavAction::SHOULDER_L:
        case NavAction::SHOULDER_R:
            // X — reset defaults (mapped via shoulder in some nav schemes)
            // We check for X button explicitly in handleEvent instead; shoulders
            // cycle screenshots elsewhere so we don't steal them here.
            break;

        case NavAction::BACK:
        case NavAction::MENU:
            m_wantsClose = true;
            break;

        default: break;
    }
}

// ─── Begin listen ─────────────────────────────────────────────────────────────
void RemapScreen::beginListen() {
    m_listening     = true;
    m_listenIndex   = m_selectedRow;
    m_listenPulse   = 0.f;
    m_listenStartMs = SDL_GetTicks();
    std::cout << "[Remap] Listening for binding: ps1="
              << m_listenIndex << " tab="
              << (m_tab == RemapTab::CONTROLLER ? "ctrl" : "kb") << "\n";
}

// ─── Configure All wizard ─────────────────────────────────────────────────────
void RemapScreen::beginConfigureAll() {
    m_configuringAll = true;
    m_selectedRow    = 0;
    m_scrollOffset   = 0;
    beginListen();
}

// ─── Poll for button press while listening ────────────────────────────────────
void RemapScreen::pollListen() {
    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            ctrl = SDL_GameControllerFromInstanceID(id);
            if (!ctrl) ctrl = SDL_GameControllerOpen(i);
            break;
        }
    }

    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    // B / Escape cancels
    bool cancelCtrl = ctrl && SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_B);
    bool cancelKey  = ks[SDL_SCANCODE_ESCAPE] || ks[SDL_SCANCODE_BACKSPACE];
    if (cancelCtrl || cancelKey) {
        // Short debounce — don't fire immediately after opening
        if (SDL_GetTicks() - m_listenStartMs > 300) {
            std::cout << "[Remap] Listen cancelled\n";
            m_listening      = false;
            m_configuringAll = false;
        }
        return;
    }

    if (applyBinding(ctrl, ks)) {
        m_dirty = true;
        if (m_configuringAll) {
            // Advance to the next binding automatically
            m_selectedRow++;
            if (m_selectedRow < (int)PS1Button::COUNT) {
                m_listenIndex   = m_selectedRow;
                m_listenPulse   = 0.f;
                m_listenStartMs = SDL_GetTicks() + 400; // brief pause before next
                if (m_selectedRow >= m_scrollOffset + VISIBLE_ROWS)
                    m_scrollOffset = m_selectedRow - VISIBLE_ROWS + 1;
                // Stay listening
            } else {
                // Done with all bindings
                m_listening      = false;
                m_configuringAll = false;
                m_selectedRow    = 0;
                m_map->save();
                std::cout << "[Remap] Configure All complete\n";
            }
        } else {
            m_listening = false;
            m_map->save();
        }
    }
}

// ─── Apply a detected binding ─────────────────────────────────────────────────
bool RemapScreen::applyBinding(SDL_GameController* ctrl, const Uint8* ks) {
    // Wait a brief moment to avoid accidentally capturing the A press that opened us
    if (SDL_GetTicks() - m_listenStartMs < 200) return false;

    if (m_tab == RemapTab::CONTROLLER && ctrl) {
        // Check triggers first (they're axes not buttons)
        Sint16 lt = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 12000) {
            m_map->ctrl[m_listenIndex].sdlButton = CtrlBinding::AXIS_L2;
            std::cout << "[Remap] ps1[" << m_listenIndex << "] → L2 trigger\n";
            return true;
        }
        if (rt > 12000) {
            m_map->ctrl[m_listenIndex].sdlButton = CtrlBinding::AXIS_R2;
            std::cout << "[Remap] ps1[" << m_listenIndex << "] → R2 trigger\n";
            return true;
        }
        // Check buttons
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            if (SDL_GameControllerGetButton(ctrl, (SDL_GameControllerButton)b)) {
                m_map->ctrl[m_listenIndex].sdlButton = b;
                CtrlBinding tmp; tmp.sdlButton = b;
                std::cout << "[Remap] ps1[" << m_listenIndex
                          << "] → " << tmp.displayName() << "\n";
                return true;
            }
        }
    } else if (m_tab == RemapTab::KEYBOARD && ks) {
        // Scan all keys
        for (int sc = 1; sc < SDL_NUM_SCANCODES; sc++) {
            if (ks[sc]) {
                // Ignore modifier-only keys (they can still be used but are weird)
                if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE)
                    continue;  // these are reserved for cancel
                m_map->keys[m_listenIndex].scancode = (SDL_Scancode)sc;
                KeyBinding tmp; tmp.scancode = (SDL_Scancode)sc;
                std::cout << "[Remap] kb[" << m_listenIndex
                          << "] → " << tmp.displayName() << "\n";
                return true;
            }
        }
    }
    return false;
}

// ─── resetDefaults ────────────────────────────────────────────────────────────
void RemapScreen::resetDefaults() {
    m_map->setDefaults();
    m_map->save();
    m_dirty = true;
    std::cout << "[Remap] Reset to defaults\n";
}

// ─── Render ───────────────────────────────────────────────────────────────────
void RemapScreen::render() {
    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    // Title
    m_theme->drawText("INPUT REMAPPING", 60, 28, pal.textSecond, FontSize::SMALL);
    m_theme->drawLine(60, 56, m_w - 60, 56, pal.gridLine);

    int y = 68;

    // Controller image placeholder + detected name
    renderControllerPlaceholder();
    y += 130;

    // Tab bar
    int tabBarY = y;
    {
        int tabW = 200, tabH = 38, tabX = 60;
        struct Tab { const char* label; RemapTab value; };
        Tab tabs[] = { {"CONTROLLER", RemapTab::CONTROLLER}, {"KEYBOARD", RemapTab::KEYBOARD} };
        for (auto& t : tabs) {
            bool active = (m_tab == t.value);
            SDL_Rect r = { tabX, tabBarY, tabW, tabH };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_Color bg = active ? pal.accent : pal.bgCard;
            SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, active ? 220 : 180);
            SDL_RenderFillRect(m_renderer, &r);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            SDL_Color tc = active ? SDL_Color{255,255,255,255} : pal.textSecond;
            m_theme->drawTextCentered(t.label, tabX + tabW/2, tabBarY + 10, tc, FontSize::SMALL);
            tabX += tabW + 8;
        }
    }
    y = tabBarY + 46;

    // Table
    renderTable();

    // Hotkeys section
    renderHotkeys();

    // Footer
    renderFooter();

    // Listen overlay (on top of everything)
    if (m_listening)
        renderListenOverlay();
}

// ─── Controller placeholder ───────────────────────────────────────────────────
void RemapScreen::renderControllerPlaceholder() {
    const auto& pal = m_theme->palette();
    std::string family = detectControllerFamily();
    std::string ctrlName = detectControllerName();

    // Placeholder rect where the SVG will eventually go
    // When you add nanosvg + the SVG files, replace this block with
    // the rasterised SVG texture render.
    int imgX = 60, imgY = 68, imgW = 280, imgH = 110;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 200);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{imgX, imgY, imgW, imgH});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{imgX, imgY, imgW, imgH});

    // Label the placeholder with the controller family
    std::string label = "[" + family + " controller image]";
    m_theme->drawTextCentered(label, imgX + imgW/2, imgY + imgH/2 - 10,
                               pal.textDisable, FontSize::TINY);
    m_theme->drawTextCentered("(drop SVG into assets/controllers/)",
                               imgX + imgW/2, imgY + imgH/2 + 8,
                               pal.textDisable, FontSize::TINY);

    // Detected name beside the placeholder
    m_theme->drawText("Detected:", imgX + imgW + 24, imgY + 16,
                       pal.textSecond, FontSize::TINY);
    m_theme->drawText(ctrlName, imgX + imgW + 24, imgY + 34,
                       pal.textPrimary, FontSize::SMALL);
    m_theme->drawText("Family: " + family, imgX + imgW + 24, imgY + 58,
                       pal.textSecond, FontSize::TINY);
}

// ─── Table ────────────────────────────────────────────────────────────────────
void RemapScreen::renderTable() {
    const auto& pal = m_theme->palette();
    const int total = (int)PS1Button::COUNT;

    // Table area
    int tableX = 60;
    int tableY = 216;   // below placeholder + tab bar
    int tableW = m_w - 120;
    int col1W  = (int)(tableW * 0.40f);
    int col2W  = (int)(tableW * 0.45f);
    // col3 is the remaining space (action hint column)

    // Header
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 180);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, tableY, tableW, 32});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    m_theme->drawText("PS1 Button", tableX + 12, tableY + 8, pal.textSecond, FontSize::TINY);
    m_theme->drawText(m_tab == RemapTab::CONTROLLER ? "Controller Binding" : "Keyboard Binding",
                       tableX + col1W + 12, tableY + 8, pal.textSecond, FontSize::TINY);

    tableY += 34;

    // Scroll indicator
    int lastVisible = std::min(m_scrollOffset + VISIBLE_ROWS, total);

    for (int i = m_scrollOffset; i < lastVisible; i++) {
        bool sel = (i == m_selectedRow);
        int rowY = tableY + (i - m_scrollOffset) * ROW_H;

        // Row background
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = sel ? pal.bgCardHover : ((i % 2 == 0) ? pal.bgCard : pal.bg);
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, sel ? 220 : 120);
        SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, rowY, tableW, ROW_H - 2});
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        // Selection accent bar
        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, rowY, 4, ROW_H - 2});
        }

        // PS1 button name
        SDL_Color tc = sel ? pal.textPrimary : pal.textSecond;
        m_theme->drawText(ps1ButtonName((PS1Button)i),
                           tableX + 12, rowY + (ROW_H - 2)/2 - 8, tc, FontSize::SMALL);

        // Current binding
        std::string binding;
        if (m_tab == RemapTab::CONTROLLER)
            binding = m_map->ctrl[i].displayName();
        else
            binding = m_map->keys[i].displayName();

        SDL_Color bc = sel ? pal.accent : pal.textSecond;
        m_theme->drawText(binding, tableX + col1W + 12,
                           rowY + (ROW_H - 2)/2 - 8, bc, FontSize::SMALL);

        // Divider
        m_theme->drawLine(tableX, rowY + ROW_H - 2,
                           tableX + tableW, rowY + ROW_H - 2, pal.gridLine);
    }

    // Scroll arrows if needed
    if (m_scrollOffset > 0) {
        m_theme->drawTextCentered("▲", tableX + tableW - 20, tableY - 12,
                                   pal.textDisable, FontSize::TINY);
    }
    if (lastVisible < total) {
        m_theme->drawTextCentered("▼", tableX + tableW - 20,
                                   tableY + VISIBLE_ROWS * ROW_H,
                                   pal.textDisable, FontSize::TINY);
    }
}

// ─── Hotkeys section ──────────────────────────────────────────────────────────
void RemapScreen::renderHotkeys() {
    const auto& pal = m_theme->palette();
    int hotY = m_h - m_theme->layout().footerH - 80;

    m_theme->drawLine(60, hotY - 8, m_w - 60, hotY - 8, pal.gridLine);
    m_theme->drawText("Frontend Hotkeys (not remappable):",
                       60, hotY, pal.textDisable, FontSize::TINY);

    int col = 0, row = 0;
    struct Hotkey { const char* name; const char* ctrl; const char* key; };
    static const Hotkey hotkeys[] = {
        { "Fast Forward", "Hold R2",     "Hold F"       },
        { "Turbo Toggle", "Hold R1+R2",  "Hold T"       },
        { "Rewind",       "Hold L2",     "Hold `"       },
        { "In-Game Menu", "Start+Y",     "F1"           },
        { "Screenshot",   "-",           "F12"          },
        { "UI Screenshot","-",           "F10"          },
    };
    int hkCount = (int)(sizeof(hotkeys) / sizeof(hotkeys[0]));
    int colW = (m_w - 120) / 3;
    for (int i = 0; i < hkCount; i++) {
        col = i % 3; row = i / 3;
        int hx = 60 + col * colW;
        int hy = hotY + 20 + row * 22;
        std::string label = std::string(hotkeys[i].name) + ":  "
                          + hotkeys[i].ctrl + "  /  " + hotkeys[i].key;
        m_theme->drawText(label, hx, hy, pal.textDisable, FontSize::TINY);
    }
}

// ─── Footer ───────────────────────────────────────────────────────────────────
void RemapScreen::renderFooter() {
    const auto& pal    = m_theme->layout();
    const auto& colors = m_theme->palette();
    int footY = m_h - m_theme->layout().footerH;
    m_theme->drawLine(60, footY, m_w - 60, footY, colors.gridLine);
    footY += 10;

    struct Hint { const char* btn; const char* label; };
    Hint hints[] = {
        {"A", "Edit binding"},
        {"Y", "Configure All"},
        {"X", "Reset Defaults"},
        {"B", "Back"},
    };
    int hx = 60;
    for (auto& h : hints) {
        // Button chip
        SDL_Rect chip = { hx, footY + 2, 24, 24 };
        SDL_SetRenderDrawColor(m_renderer,
            colors.accent.r, colors.accent.g, colors.accent.b, 200);
        SDL_RenderFillRect(m_renderer, &chip);
        m_theme->drawTextCentered(h.btn, hx + 12, footY + 4,
                                   SDL_Color{255,255,255,255}, FontSize::TINY);
        hx += 30;
        m_theme->drawText(h.label, hx, footY + 5, colors.textSecond, FontSize::SMALL);
        hx += (int)strlen(h.label) * 8 + 32;
    }
}

// ─── Listen overlay ───────────────────────────────────────────────────────────
void RemapScreen::renderListenOverlay() {
    const auto& pal = m_theme->palette();

    // Dim background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{0, 0, m_w, m_h});

    // Pulsing card
    float pulse = (std::sin(m_listenPulse) + 1.0f) * 0.5f;
    Uint8 borderAlpha = (Uint8)(150 + (int)(105 * pulse));

    int cardW = 480, cardH = 200;
    int cardX = m_w/2 - cardW/2;
    int cardY = m_h/2 - cardH/2;

    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 240);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{cardX, cardY, cardW, cardH});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Pulsing accent border
    SDL_SetRenderDrawColor(m_renderer,
        pal.accent.r, pal.accent.g, pal.accent.b, borderAlpha);
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{cardX, cardY, cardW, cardH});
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{cardX+1, cardY+1, cardW-2, cardH-2});

    // What we're binding
    std::string ps1name = ps1ButtonName((PS1Button)m_listenIndex);
    std::string tabLabel = (m_tab == RemapTab::CONTROLLER) ? "Controller" : "Keyboard";

    m_theme->drawTextCentered("PRESS A BUTTON",
        m_w/2, cardY + 36, pal.accent, FontSize::TITLE);

    m_theme->drawTextCentered("Binding:  " + ps1name,
        m_w/2, cardY + 82, pal.textPrimary, FontSize::SMALL);

    m_theme->drawTextCentered("Input type: " + tabLabel,
        m_w/2, cardY + 108, pal.textSecond, FontSize::SMALL);

    if (m_configuringAll) {
        std::string prog = std::to_string(m_listenIndex + 1) + " / "
                         + std::to_string((int)PS1Button::COUNT);
        m_theme->drawTextCentered("Configure All — " + prog,
            m_w/2, cardY + 136, pal.textDisable, FontSize::TINY);
    }

    m_theme->drawTextCentered("B / Backspace = cancel",
        m_w/2, cardY + 166, pal.textDisable, FontSize::TINY);
}
