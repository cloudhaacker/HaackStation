// ─── nanosvg — single-header SVG parser + rasteriser (MIT licence) ──────────
// Define implementations exactly once here. Headers go in frontend/include/.
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

#include "remap_screen.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstring>

// ─── Display order ────────────────────────────────────────────────────────────
// Maps display row index → PS1Button enum value.
// Groups buttons logically: face → shoulders → sticks → d-pad → meta.
// Used by renderRightPanel (drawing), navigate() (bounds), beginListen()
// (single edit), and pollListen() Configure All (wizard sequencing).
static const int DISPLAY_ORDER[] = {
    (int)PS1Button::B,       // Cross    (×)
    (int)PS1Button::A,       // Circle   (○)
    (int)PS1Button::Y,       // Square   (□)
    (int)PS1Button::X,       // Triangle (△)
    (int)PS1Button::L,       // L1
    (int)PS1Button::R,       // R1
    (int)PS1Button::L2,      // L2
    (int)PS1Button::R2,      // R2
    (int)PS1Button::L3,      // L3
    (int)PS1Button::R3,      // R3
    (int)PS1Button::UP,      // D-Pad Up
    (int)PS1Button::DOWN,    // D-Pad Down
    (int)PS1Button::LEFT,    // D-Pad Left
    (int)PS1Button::RIGHT,   // D-Pad Right
    (int)PS1Button::SELECT,  // Select
    (int)PS1Button::START,   // Start
};
static const int DISPLAY_COUNT = (int)(sizeof(DISPLAY_ORDER) / sizeof(DISPLAY_ORDER[0]));

// ─── Construction / destruction ───────────────────────────────────────────────
RemapScreen::RemapScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                          ControllerNav* nav, InputMap* map)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_map(map)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

RemapScreen::~RemapScreen() {
    freeSvgTexture();
}

void RemapScreen::onWindowResize(int w, int h) {
    m_w = w;
    m_h = h;
}

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
    std::string low = detectControllerName();
    for (char& c : low) c = (char)tolower(c);

    if (low.find("dualshock")   != std::string::npos ||
        low.find("dualsense")   != std::string::npos ||
        low.find("playstation") != std::string::npos ||
        low.find("ps4")         != std::string::npos ||
        low.find("ps5")         != std::string::npos)
        return "PlayStation";

    if (low.find("xbox")      != std::string::npos ||
        low.find("xinput")    != std::string::npos ||
        low.find("microsoft") != std::string::npos)
        return "Xbox";

    if (low.find("switch")       != std::string::npos ||
        low.find("nintendo")     != std::string::npos ||
        low.find("pro controller")!= std::string::npos)
        return "Switch";

    return "Generic";
}

// ─── SVG path selection ───────────────────────────────────────────────────────
std::string RemapScreen::svgPathForFamily(const std::string& family,
                                          const std::string& controllerName)
{
    std::string low = controllerName;
    for (char& c : low) c = (char)tolower(c);

    if (family == "PlayStation") {
        if (low.find("dualsense") != std::string::npos ||
            low.find("ps5")       != std::string::npos)
            return "assets/controllers/ps5.svg";
        return "assets/controllers/ps4.svg";
    }
    if (family == "Xbox") {
        if (low.find("series") != std::string::npos) return "assets/controllers/xbox-series-x.svg";
        if (low.find("one")    != std::string::npos) return "assets/controllers/xbox-one.svg";
        if (low.find("360")    != std::string::npos) return "assets/controllers/xbox-360.svg";
        return "assets/controllers/xbox-series-x.svg";
    }
    if (family == "Switch") return "assets/controllers/switch-pro.svg";
    return "assets/controllers/ps1.svg";
}

// ─── SVG preprocessing ────────────────────────────────────────────────────────
// These SVGs use class="ccsvg__primary" with no inline fill. nanosvg doesn't
// parse CSS, so we inject fill="#rrggbb" before parsing. We also strip the
// root <svg> class attribute so nanosvg doesn't choke on unknown attributes.
std::string RemapScreen::preprocessSvg(const std::string& raw,
                                        const std::string& fillHex)
{
    std::string out = raw;

    // Replace path class with fill attribute
    const std::string from = "class=\"ccsvg__primary\"";
    const std::string to   = "fill=\"" + fillHex + "\"";
    size_t pos = 0;
    while ((pos = out.find(from, pos)) != std::string::npos) {
        out.replace(pos, from.size(), to);
        pos += to.size();
    }

    // Strip class="..." from the <svg> root tag only
    size_t svgEnd = out.find('>');
    if (svgEnd != std::string::npos) {
        std::string tag = out.substr(0, svgEnd);
        size_t cp = tag.find(" class=\"");
        if (cp != std::string::npos) {
            size_t ep = tag.find('"', cp + 8);
            if (ep != std::string::npos)
                tag.erase(cp, ep - cp + 1);
        }
        out = tag + out.substr(svgEnd);
    }
    return out;
}

// ─── SVG loading ──────────────────────────────────────────────────────────────
void RemapScreen::freeSvgTexture() {
    if (m_svgTexture) { SDL_DestroyTexture(m_svgTexture); m_svgTexture = nullptr; }
    m_svgLoadedFamily.clear();
    m_svgLoadAttempted = false;
}

void RemapScreen::loadControllerSvg() {
    std::string family   = detectControllerFamily();
    std::string ctrlName = detectControllerName();

    if (m_svgLoadAttempted && m_svgLoadedFamily == family) return;

    freeSvgTexture();
    m_svgLoadedFamily  = family;
    m_svgLoadAttempted = true;

    std::string path = svgPathForFamily(family, ctrlName);
    std::ifstream f(path);
    if (!f) { std::cerr << "[Remap] SVG not found: " << path << "\n"; return; }

    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    f.close();

    std::string svgData = preprocessSvg(raw, "#ffffff");
    std::vector<char> buf(svgData.begin(), svgData.end());
    buf.push_back('\0');

    // Rasterise at a generous internal size — we'll scale to fit at draw time
    // The SVGs are 64×64 viewBox; rasterise at 4× for crisp display
    NSVGimage* image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image || image->width <= 0 || image->height <= 0) {
        std::cerr << "[Remap] nsvgParse failed: " << path << "\n";
        if (image) nsvgDelete(image);
        return;
    }

    // Rasterise at 4× the SVG's native size for sharp rendering when scaled up
    float scale = 4.0f;
    int   rasW  = (int)(image->width  * scale);
    int   rasH  = (int)(image->height * scale);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    std::vector<unsigned char> pixels(rasW * rasH * 4, 0);
    nsvgRasterize(rast, image, 0, 0, scale, pixels.data(), rasW, rasH, rasW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        pixels.data(), rasW, rasH, 32, rasW * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (!surf) return;

    m_svgTexture = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    if (m_svgTexture) {
        SDL_SetTextureBlendMode(m_svgTexture, SDL_BLENDMODE_BLEND);
        std::cout << "[Remap] Loaded SVG: " << path << " (" << rasW << "x" << rasH << ")\n";
    }
}

// ─── Events ───────────────────────────────────────────────────────────────────
void RemapScreen::handleEvent(const SDL_Event& e) {
    if (m_listening) return;

    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X) { resetDefaults(); return; }
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_Y) { beginConfigureAll(); return; }
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_DELETE) {
        resetDefaults(); return;
    }

    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) navigate(action);
}

void RemapScreen::update(float deltaMs) {
    m_listenPulse += deltaMs * 0.006f;
    if (m_listening) pollListen();
}

// ─── Navigation ───────────────────────────────────────────────────────────────
void RemapScreen::navigate(NavAction action) {
    switch (action) {
        case NavAction::UP:
            if (m_selectedRow > 0) {
                m_selectedRow--;
                if (m_selectedRow < m_scrollOffset)
                    m_scrollOffset = m_selectedRow;
            } else {
                m_nav->cancelHeld(); // already at top — stop repeat flood
            }
            break;
        case NavAction::DOWN:
            if (m_selectedRow < DISPLAY_COUNT - 1) {
                m_selectedRow++;
            } else {
                m_nav->cancelHeld(); // already at bottom
            }
            break;
        case NavAction::LEFT:
        case NavAction::RIGHT:
            m_tab = (m_tab == RemapTab::CONTROLLER)
                    ? RemapTab::KEYBOARD : RemapTab::CONTROLLER;
            break;
        case NavAction::CONFIRM:    beginListen();       break;
        case NavAction::BACK:
        case NavAction::MENU:       m_wantsClose = true; break;
        default: break;
    }
}

// ─── Listen ───────────────────────────────────────────────────────────────────
void RemapScreen::beginListen() {
    // m_selectedRow is the display-order row index.
    // DISPLAY_ORDER maps it to the actual PS1Button enum value for the binding.
    m_listening     = true;
    m_listenIndex   = DISPLAY_ORDER[m_selectedRow];
    m_listenPulse   = 0.f;
    m_listenStartMs = SDL_GetTicks();
    m_listenReadyMs = SDL_GetTicks() + 250; // must wait 250ms before accepting input
    m_bCancelHeldSince = 0;
}

void RemapScreen::beginConfigureAll() {
    m_configuringAll = true;
    m_selectedRow    = 0;
    m_scrollOffset   = 0;
    beginListen();
}

// ─── pollListen ───────────────────────────────────────────────────────────────
// Called every frame while m_listening == true.
// Key fix: uses m_listenReadyMs (not m_listenStartMs) to gate input acceptance.
// When Configure All advances to the next binding, we set m_listenReadyMs to
// now + 600ms so the held button from the previous press can't bleed through.
// m_listenStartMs is only used for the cancel-button debounce.
//
// Cancel behaviour:
//   Keyboard:    Escape or Backspace (tap) — always safe, no remapping conflict.
//   Controller:  Hold B for CANCEL_HOLD_MS — requires deliberate hold, so a
//                quick B press during Configure All records B as a binding first,
//                and the hold gesture can't fire accidentally. Works on Steam Deck
//                and Android where Escape is awkward or unavailable.
void RemapScreen::pollListen() {
    static constexpr Uint32 CANCEL_HOLD_MS = 500;

    // Pump SDL events so button state is fresh
    SDL_PumpEvents();

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

    // ── Cancel check ──────────────────────────────────────────────────────────
    // Keyboard: tap Escape or Backspace — immediately cancel.
    bool cancelKey = ks[SDL_SCANCODE_ESCAPE] || ks[SDL_SCANCODE_BACKSPACE];
    if (cancelKey && SDL_GetTicks() - m_listenStartMs > 300) {
        m_listening      = false;
        m_configuringAll = false;
        m_bCancelHeldSince = 0;
        return;
    }

    // Controller: hold B for CANCEL_HOLD_MS.
    // Track how long B has been held — cancel only once the threshold passes.
    // A quick tap of B during Configure All records B as a binding instead.
    bool bHeld = ctrl && SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_B);
    if (bHeld) {
        if (m_bCancelHeldSince == 0)
            m_bCancelHeldSince = SDL_GetTicks();
        if (SDL_GetTicks() - m_bCancelHeldSince >= CANCEL_HOLD_MS) {
            m_listening        = false;
            m_configuringAll   = false;
            m_bCancelHeldSince = 0;
            return;
        }
    } else {
        m_bCancelHeldSince = 0;
    }

    // Not ready yet — waiting out the inter-binding delay
    if (SDL_GetTicks() < m_listenReadyMs) return;

    if (!applyBinding(ctrl, ks)) return;

    // A binding was recorded — reset B-hold timer so a new hold can cancel the next
    m_bCancelHeldSince = 0;
    m_dirty = true;

    if (m_configuringAll) {
        m_selectedRow++;
        if (m_selectedRow < DISPLAY_COUNT) {
            m_listenIndex   = DISPLAY_ORDER[m_selectedRow];
            m_listenPulse   = 0.f;
            m_listenStartMs = SDL_GetTicks();
            // Critical: delay READY time so the button held for the previous
            // binding doesn't immediately fire the next one.
            // 600ms gives enough margin even for slow button releases.
            m_listenReadyMs = SDL_GetTicks() + 600;
            // Scroll the table to keep the active row visible
            // (visibleRows not known here, use a conservative estimate)
            if (m_selectedRow >= m_scrollOffset + 8)
                m_scrollOffset = m_selectedRow - 7;
        } else {
            m_listening      = false;
            m_configuringAll = false;
            m_selectedRow    = 0;
            m_scrollOffset   = 0;
            m_map->save();
            std::cout << "[Remap] Configure All complete\n";
        }
    } else {
        m_listening = false;
        m_map->save();
    }
}

// ─── applyBinding ─────────────────────────────────────────────────────────────
bool RemapScreen::applyBinding(SDL_GameController* ctrl, const Uint8* ks) {
    if (m_tab == RemapTab::CONTROLLER && ctrl) {
        Sint16 lt = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 12000) { m_map->ctrl[m_listenIndex].sdlButton = CtrlBinding::AXIS_L2; return true; }
        if (rt > 12000) { m_map->ctrl[m_listenIndex].sdlButton = CtrlBinding::AXIS_R2; return true; }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            if (SDL_GameControllerGetButton(ctrl, (SDL_GameControllerButton)b)) {
                m_map->ctrl[m_listenIndex].sdlButton = b;
                return true;
            }
        }
    } else if (m_tab == RemapTab::KEYBOARD && ks) {
        for (int sc = 1; sc < SDL_NUM_SCANCODES; sc++) {
            if (!ks[sc]) continue;
            if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) continue;
            m_map->keys[m_listenIndex].scancode = (SDL_Scancode)sc;
            return true;
        }
    }
    return false;
}

// ─── resetDefaults ────────────────────────────────────────────────────────────
void RemapScreen::resetDefaults() {
    m_map->setDefaults();
    m_map->save();
    m_dirty = true;
}

// ─── render ───────────────────────────────────────────────────────────────────
void RemapScreen::render() {
    const auto& pal = m_theme->palette();

    loadControllerSvg();

    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    // Title bar
    m_theme->drawText("INPUT REMAPPING", MARGIN, 24, pal.textSecond, FontSize::SMALL);
    m_theme->drawLine(MARGIN, 52, m_w - MARGIN, 52, pal.gridLine);

    // Two-panel content area
    int footerBarH = m_theme->layout().footerH;
    int contentY   = TITLE_H;
    int contentH   = m_h - TITLE_H - footerBarH - FOOTER_H_EXTRA;
    int contentW   = m_w - MARGIN * 2;

    int leftW  = (int)(contentW * LEFT_PANEL_FRAC);
    int rightW = contentW - leftW - PANEL_GAP;
    int leftX  = MARGIN;
    int rightX = MARGIN + leftW + PANEL_GAP;

    renderLeftPanel (leftX,  contentY, leftW,  contentH);
    renderRightPanel(rightX, contentY, rightW, contentH);
    renderFooter();

    if (m_listening) renderListenOverlay();
}

// ─── Left panel — controller SVG + info ──────────────────────────────────────
void RemapScreen::renderLeftPanel(int px, int py, int pw, int ph) {
    const auto& pal = m_theme->palette();

    // Subtle panel background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 60);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{px, py, pw, ph});
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 60);
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{px, py, pw, ph});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    std::string ctrlName = detectControllerName();
    std::string family   = detectControllerFamily();

    // SVG image — fill as much of the panel as possible with padding
    int imgPad  = PANEL_PAD * 2;
    int imgMaxW = pw - imgPad * 2;
    int imgMaxH = ph - imgPad * 2 - 60; // 60px reserved for text below

    if (m_svgTexture) {
        int texW = 0, texH = 0;
        SDL_QueryTexture(m_svgTexture, nullptr, nullptr, &texW, &texH);

        // Scale to fit within the available space, preserving aspect ratio
        float scaleX = (float)imgMaxW / texW;
        float scaleY = (float)imgMaxH / texH;
        float scale  = std::min(scaleX, scaleY);
        int   drawW  = (int)(texW * scale);
        int   drawH  = (int)(texH * scale);
        int   drawX  = px + (pw - drawW) / 2;
        int   drawY  = py + imgPad + (imgMaxH - drawH) / 2;

        // Tint with theme text colour — white silhouette + colour mod = themed look
        SDL_SetTextureColorMod(m_svgTexture,
            pal.textSecond.r, pal.textSecond.g, pal.textSecond.b);
        SDL_Rect dst = { drawX, drawY, drawW, drawH };
        SDL_RenderCopy(m_renderer, m_svgTexture, nullptr, &dst);
    } else {
        // Fallback: labelled placeholder box
        SDL_Rect placeholder = { px + imgPad, py + imgPad, imgMaxW, imgMaxH };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 120);
        SDL_RenderFillRect(m_renderer, &placeholder);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
        SDL_RenderDrawRect(m_renderer, &placeholder);
        m_theme->drawTextCentered("[" + family + " controller]",
            px + pw/2, py + imgPad + imgMaxH/2 - 10, pal.textDisable, FontSize::SMALL);
        m_theme->drawTextCentered("(missing assets/controllers/)",
            px + pw/2, py + imgPad + imgMaxH/2 + 12, pal.textDisable, FontSize::TINY);
    }

    // Controller info text — bottom of left panel
    int infoY = py + ph - 52;
    m_theme->drawLine(px + PANEL_PAD, infoY - 8,
                      px + pw - PANEL_PAD, infoY - 8, pal.gridLine);
    m_theme->drawTextCentered(ctrlName,
        px + pw/2, infoY, pal.textPrimary, FontSize::SMALL);
    m_theme->drawTextCentered("Family: " + family,
        px + pw/2, infoY + 22, pal.textSecond, FontSize::TINY);
}

// ─── Right panel — tabs + binding table + hotkeys ─────────────────────────────
void RemapScreen::renderRightPanel(int px, int py, int pw, int ph) {
    const auto& pal = m_theme->palette();

    // Panel background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 60);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{px, py, pw, ph});
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 60);
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{px, py, pw, ph});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    int y = py + PANEL_PAD;

    // ── Tab bar ───────────────────────────────────────────────────────────────
    {
        int tabW = (pw - PANEL_PAD * 2 - 8) / 2;
        int tabX = px + PANEL_PAD;
        struct Tab { const char* label; RemapTab val; };
        Tab tabs[] = { {"CONTROLLER", RemapTab::CONTROLLER}, {"KEYBOARD", RemapTab::KEYBOARD} };
        for (auto& t : tabs) {
            bool active = (m_tab == t.val);
            SDL_Rect r  = { tabX, y, tabW, TAB_H };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_Color bg = active ? pal.accent : pal.bgCard;
            SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, active ? 230 : 160);
            SDL_RenderFillRect(m_renderer, &r);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            SDL_Color tc = active ? SDL_Color{255,255,255,255} : pal.textSecond;
            m_theme->drawTextCentered(t.label, tabX + tabW/2, y + 10, tc, FontSize::SMALL);
            tabX += tabW + 8;
        }
    }
    y += TAB_H + 8;

    // ── Table header ──────────────────────────────────────────────────────────
    int tableX  = px + PANEL_PAD;
    int tableW  = pw - PANEL_PAD * 2;
    int col1W   = (int)(tableW * 0.48f);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 180);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, y, tableW, 28});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    m_theme->drawText("PS1 Button",
        tableX + 10, y + 6, pal.textSecond, FontSize::TINY);
    m_theme->drawText(m_tab == RemapTab::CONTROLLER ? "Controller" : "Keyboard",
        tableX + col1W + 10, y + 6, pal.textSecond, FontSize::TINY);
    y += 30;

    // ── Compute how many rows fit above the hotkey block ──────────────────────
    int tableBottom  = py + ph - HOTKEY_BLOCK_H - PANEL_PAD;
    int availableH   = tableBottom - y;
    int visibleRows  = std::max(1, availableH / ROW_H);
    // DISPLAY_ORDER / DISPLAY_COUNT defined at file scope — see top of file.

    // Clamp scroll so selected row stays visible
    if (m_selectedRow >= m_scrollOffset + visibleRows)
        m_scrollOffset = m_selectedRow - visibleRows + 1;
    if (m_scrollOffset < 0) m_scrollOffset = 0;

    int lastVisible = std::min(m_scrollOffset + visibleRows, DISPLAY_COUNT);

    for (int row = m_scrollOffset; row < lastVisible; row++) {
        int  i    = DISPLAY_ORDER[row];
        bool sel  = (row == m_selectedRow);
        int  rowY = y + (row - m_scrollOffset) * ROW_H;

        // Clip rows that would overlap the hotkey block
        if (rowY + ROW_H > tableBottom) break;

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = sel ? pal.bgCardHover : ((i % 2 == 0) ? pal.bgCard : pal.bg);
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, sel ? 220 : 100);
        SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, rowY, tableW, ROW_H - 2});
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderFillRect(m_renderer, &SDL_Rect{tableX, rowY, 4, ROW_H - 2});
        }

        SDL_Color tc = sel ? pal.textPrimary : pal.textSecond;
        m_theme->drawText(ps1ButtonName((PS1Button)i),
            tableX + 12, rowY + (ROW_H - 2)/2 - 8, tc, FontSize::SMALL);

        std::string binding = (m_tab == RemapTab::CONTROLLER)
            ? m_map->ctrl[i].displayName()
            : m_map->keys[i].displayName();
        SDL_Color bc = sel ? pal.accent : pal.textSecond;
        m_theme->drawText(binding,
            tableX + col1W + 10, rowY + (ROW_H - 2)/2 - 8, bc, FontSize::SMALL);

        m_theme->drawLine(tableX, rowY + ROW_H - 2,
                          tableX + tableW, rowY + ROW_H - 2, pal.gridLine);
    }

    // Scroll arrows
    if (m_scrollOffset > 0)
        m_theme->drawTextCentered("▲", tableX + tableW - 10, y - 14,
                                   pal.textDisable, FontSize::TINY);
    if (lastVisible < DISPLAY_COUNT)
        m_theme->drawTextCentered("▼", tableX + tableW - 10, tableBottom - 4,
                                   pal.textDisable, FontSize::TINY);

    // ── Hotkeys block — always at bottom of right panel ───────────────────────
    int hkY = py + ph - HOTKEY_BLOCK_H;
    m_theme->drawLine(tableX, hkY, tableX + tableW, hkY, pal.gridLine);
    hkY += 8;
    m_theme->drawText("Hotkeys (not remappable):",
        tableX, hkY, pal.textDisable, FontSize::TINY);
    hkY += 18;

    struct HK { const char* name; const char* ctrl; const char* key; };
    static const HK hks[] = {
        { "Fast Fwd",  "Hold R2",    "Hold F" },
        { "Turbo",     "Hold R1+R2", "Hold T" },
        { "Rewind",    "Hold L2",    "Hold `" },
        { "Menu",      "Start+Y",    "F1"     },
    };
    int hkColW = tableW / 2;
    for (int i = 0; i < 4; i++) {
        int hx = tableX + (i % 2) * hkColW;
        int hy = hkY + (i / 2) * 20;
        std::string lbl = std::string(hks[i].name) + ": "
                        + hks[i].ctrl + " / " + hks[i].key;
        m_theme->drawText(lbl, hx, hy, pal.textDisable, FontSize::TINY);
    }
}

// ─── Footer hint bar ──────────────────────────────────────────────────────────
void RemapScreen::renderFooter() {
    const auto& pal   = m_theme->palette();
    int         footY = m_h - m_theme->layout().footerH;
    m_theme->drawLine(MARGIN, footY, m_w - MARGIN, footY, pal.gridLine);
    footY += 10;

    struct Hint { const char* btn; const char* label; };
    static const Hint hints[] = {
        {"A", "Edit binding"},
        {"Y", "Configure All"},
        {"X", "Reset Defaults"},
        {"B", "Back"},
    };
    int hx = MARGIN;
    for (auto& h : hints) {
        // Filled chip
        SDL_Rect chip = { hx, footY + 2, 24, 24 };
        SDL_SetRenderDrawColor(m_renderer,
            pal.accent.r, pal.accent.g, pal.accent.b, 200);
        SDL_RenderFillRect(m_renderer, &chip);
        m_theme->drawTextCentered(h.btn, hx + 12, footY + 4,
                                   {255,255,255,255}, FontSize::TINY);
        hx += 30;
        m_theme->drawText(h.label, hx, footY + 5, pal.textSecond, FontSize::SMALL);
        hx += (int)strlen(h.label) * 8 + 28;
    }
}

// ─── Listen overlay ───────────────────────────────────────────────────────────
void RemapScreen::renderListenOverlay() {
    const auto& pal = m_theme->palette();

    // Dim the whole screen
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{0, 0, m_w, m_h});

    // Pulsing accent border
    float pulse       = (std::sin(m_listenPulse) + 1.0f) * 0.5f;
    Uint8 borderAlpha = (Uint8)(140 + (int)(115 * pulse));

    int cardW = 500, cardH = 210;
    int cardX = m_w/2 - cardW/2;
    int cardY = m_h/2 - cardH/2;

    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 245);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{cardX, cardY, cardW, cardH});
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, borderAlpha);
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{cardX,   cardY,   cardW,   cardH  });
    SDL_RenderDrawRect(m_renderer, &SDL_Rect{cardX+1, cardY+1, cardW-2, cardH-2});

    // "PRESS A BUTTON" — use accent colour so it pulses with the border visually
    m_theme->drawTextCentered("PRESS A BUTTON",
        m_w/2, cardY + 38, pal.accent, FontSize::TITLE);

    std::string ps1name  = ps1ButtonName((PS1Button)m_listenIndex);
    std::string tabLabel = (m_tab == RemapTab::CONTROLLER) ? "Controller" : "Keyboard";

    m_theme->drawTextCentered("Binding:  " + ps1name,
        m_w/2, cardY + 90, pal.textPrimary, FontSize::SMALL);
    m_theme->drawTextCentered("Input type: " + tabLabel,
        m_w/2, cardY + 116, pal.textSecond, FontSize::SMALL);

    if (m_configuringAll) {
        std::string prog = std::to_string(m_listenIndex + 1) + " of "
                         + std::to_string((int)PS1Button::COUNT);
        m_theme->drawTextCentered("Configure All  —  " + prog,
            m_w/2, cardY + 148, pal.textDisable, FontSize::TINY);
    }

    m_theme->drawTextCentered("Hold B (0.5s) / Esc / Backspace  =  cancel",
        m_w/2, cardY + 178, pal.textDisable, FontSize::TINY);
}
