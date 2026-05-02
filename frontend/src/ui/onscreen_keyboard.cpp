#include "onscreen_keyboard.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
OnScreenKeyboard::OnScreenKeyboard(SDL_Renderer* renderer,
                                   ThemeEngine*  theme,
                                   ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
    buildLayout();
}

OnScreenKeyboard::~OnScreenKeyboard() {
    freeAssets();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout definition — QWERTY 5-row
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::buildLayout() {
    // Row 0: number / symbol row
    m_rows[0] = {
        OskKey::Symbol("1","!"), OskKey::Symbol("2","@"), OskKey::Symbol("3","#"),
        OskKey::Symbol("4","$"), OskKey::Symbol("5","%"), OskKey::Symbol("6","^"),
        OskKey::Symbol("7","&"), OskKey::Symbol("8","*"), OskKey::Symbol("9","("),
        OskKey::Symbol("0",")"), OskKey::Symbol("-","_"), OskKey::Symbol("=","+"),
        OskKey::Symbol("@","@"),   // email shortcut — same on both layers
    };

    // Row 1: QWERTY
    m_rows[1] = {
        OskKey::Char("q","Q"), OskKey::Char("w","W"), OskKey::Char("e","E"),
        OskKey::Char("r","R"), OskKey::Char("t","T"), OskKey::Char("y","Y"),
        OskKey::Char("u","U"), OskKey::Char("i","I"), OskKey::Char("o","O"),
        OskKey::Char("p","P"),
    };

    // Row 2: ASDF
    m_rows[2] = {
        OskKey::Char("a","A"), OskKey::Char("s","S"), OskKey::Char("d","D"),
        OskKey::Char("f","F"), OskKey::Char("g","G"), OskKey::Char("h","H"),
        OskKey::Char("j","J"), OskKey::Char("k","K"), OskKey::Char("l","L"),
        OskKey::Symbol(".",":")
    };

    // Row 3: ZXCV
    m_rows[3] = {
        OskKey::Char("z","Z"), OskKey::Char("x","X"), OskKey::Char("c","C"),
        OskKey::Char("v","V"), OskKey::Char("b","B"), OskKey::Char("n","N"),
        OskKey::Char("m","M"), OskKey::Symbol(",","<"), OskKey::Symbol(".","?"),
    };

    // Row 4: action row — Shift | Space | Backspace | OK
    {
        OskKey shiftKey = OskKey::Action("SHIFT", 1.8f);
        OskKey spaceKey = OskKey::Action("SPACE", 4.0f);
        OskKey bsKey    = OskKey::Action("DEL",   1.8f);
        OskKey okKey    = OskKey::Action("OK",    2.0f);
        m_rows[4] = { shiftKey, spaceKey, bsKey, okKey };
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  open / close
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::open(const std::string& prompt, int maxLen,
                             bool masked, OskCallback callback)
{
    m_prompt     = prompt;
    m_maxLen     = maxLen;
    m_masked     = masked;
    m_callback   = callback;
    m_text       = "";
    m_row        = 1;   // Start on QWERTY row
    m_col        = 0;
    m_shifted    = false;
    m_capsLock   = false;
    m_open       = true;
    m_wantsClose = false;
    m_cursorBlinkMs = 0;
    m_cursorVisible = true;
    m_physicalKeyboardActive = false;
    m_flashRow = -1;
    m_flashCol = -1;
    m_flashMs  = 0;

    if (!m_assetsLoaded) loadAssets();

    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    std::cout << "[OSK] Opened: " << prompt << " (masked=" << masked << ")\n";
}

void OnScreenKeyboard::close() {
    m_open       = false;
    m_wantsClose = false;
    m_callback   = nullptr;
}

void OnScreenKeyboard::setText(const std::string& text) {
    m_text = text;
    if (m_maxLen > 0 && (int)m_text.size() > m_maxLen)
        m_text.resize(m_maxLen);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Navigation helpers
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::clampSelection() {
    m_row = std::clamp(m_row, 0, NUM_ROWS - 1);
    int cols = (int)m_rows[m_row].size();
    if (cols == 0) return;
    m_col = std::clamp(m_col, 0, cols - 1);
}

// navigate — full behaviour including row-wrap on LEFT/RIGHT.
// Used for single fresh presses so the user can zip between rows.
void OnScreenKeyboard::navigate(NavAction a) {
    int prevRow = m_row;
    int prevCol = m_col;

    if (a == NavAction::UP) {
        m_row--;
    } else if (a == NavAction::DOWN) {
        m_row++;
    } else if (a == NavAction::LEFT) {
        m_col--;
        if (m_col < 0) {
            m_row--;
            if (m_row >= 0)
                m_col = (int)m_rows[m_row].size() - 1;
        }
    } else if (a == NavAction::RIGHT) {
        m_col++;
        int cols = (int)m_rows[m_row].size();
        if (m_col >= cols) {
            m_row++;
            m_col = 0;
        }
    }

    clampSelection();

    // Moving UP/DOWN: preserve approximate horizontal position across rows
    if (m_row != prevRow && a != NavAction::LEFT && a != NavAction::RIGHT) {
        int prevCols = (int)m_rows[prevRow].size();
        int newCols  = (int)m_rows[m_row].size();
        if (prevCols > 0 && newCols > 0) {
            float frac = (float)prevCol / (float)(prevCols - 1);
            m_col = std::clamp((int)std::round(frac * (newCols - 1)), 0, newCols - 1);
        }
    }
}

// navigateWithClamp — used for held-repeat events.
// UP/DOWN: same as navigate but stops at top/bottom row.
// LEFT/RIGHT: clamps within the current row — when the edge is hit,
// wraps to the adjacent row (same as a single press would) but then
// clamps immediately so the next hold tick starts from the new row edge.
// This prevents "fly through the whole keyboard" while still letting
// the user move between rows by holding through an edge naturally.
void OnScreenKeyboard::navigateWithClamp(NavAction a) {
    if (a == NavAction::UP) {
        if (m_row > 0) m_row--;
        else { m_nav->cancelHeld(); return; }
        clampSelection();
        m_col = std::clamp(m_col, 0, (int)m_rows[m_row].size() - 1);
    } else if (a == NavAction::DOWN) {
        if (m_row < NUM_ROWS - 1) m_row++;
        else { m_nav->cancelHeld(); return; }
        clampSelection();
        m_col = std::clamp(m_col, 0, (int)m_rows[m_row].size() - 1);
    } else if (a == NavAction::LEFT) {
        // Held: hard stop at left edge of current row — no wrap
        if (m_col > 0) m_col--;
        else m_nav->cancelHeld();
    } else if (a == NavAction::RIGHT) {
        // Held: hard stop at right edge of current row — no wrap
        int cols = (int)m_rows[m_row].size();
        if (m_col < cols - 1) m_col++;
        else m_nav->cancelHeld();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  activateSelected — fires the currently highlighted key
// ─────────────────────────────────────────────────────────────────────────────
// flashKeyForChar — finds the OSK key matching the typed character and
// stores its position + starts a 150ms highlight timer.
// Called when physical keyboard input arrives via SDL_TEXTINPUT.
void OnScreenKeyboard::flashKeyForChar(char c) {
    std::string target(1, c);
    // Search both layers (value and shiftValue)
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int col = 0; col < (int)m_rows[r].size(); ++col) {
            const OskKey& k = m_rows[r][col];
            if (k.value == target || k.shiftValue == target) {
                m_flashRow = r;
                m_flashCol = col;
                m_flashMs  = 150;
                return;
            }
        }
    }
    // No match (e.g. a character not on this keyboard) — clear flash
    m_flashMs = 0;
}

void OnScreenKeyboard::activateSelected() {
    if (m_row < 0 || m_row >= NUM_ROWS) return;
    if (m_col < 0 || m_col >= (int)m_rows[m_row].size()) return;

    const OskKey& key = m_rows[m_row][m_col];

    if (key.isAction) {
        const std::string& lbl = key.label;
        if (lbl == "SHIFT") {
            // Single tap: one-shot shift; double tap: caps lock
            if (m_capsLock) {
                m_capsLock = false;
                m_shifted  = false;
            } else if (m_shifted) {
                m_capsLock = true;
            } else {
                m_shifted = true;
            }
        } else if (lbl == "SPACE") {
            if (m_maxLen == 0 || (int)m_text.size() < m_maxLen)
                m_text += ' ';
            if (m_shifted && !m_capsLock) m_shifted = false;
        } else if (lbl == "DEL") {
            if (!m_text.empty()) m_text.pop_back();
        } else if (lbl == "OK") {
            std::cout << "[OSK] Confirmed: " << (m_masked ? "***" : m_text) << "\n";
            m_open       = false;
            m_wantsClose = true;
            if (m_callback) m_callback(true, m_text);
        }
    } else {
        // Character key
        const std::string& val = (m_shifted || m_capsLock) ? key.shiftValue : key.value;
        if (!val.empty()) {
            if (m_maxLen == 0 || (int)m_text.size() < m_maxLen)
                m_text += val;
        }
        // Release one-shot shift after typing
        if (m_shifted && !m_capsLock) m_shifted = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleEvent
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::handleEvent(const SDL_Event& e) {
    if (!m_open) return;

    NavAction a = m_nav->processEvent(e);

    if (a == NavAction::BACK) {
        // B / Circle — cancel without confirming
        std::cout << "[OSK] Cancelled\n";
        m_open       = false;
        m_wantsClose = true;
        if (m_callback) m_callback(false, "");
        return;
    }

    if (a == NavAction::OPTIONS) {
        // X / Square — backspace shortcut from anywhere
        if (!m_text.empty()) m_text.pop_back();
        return;
    }

    if (a == NavAction::UP || a == NavAction::DOWN ||
        a == NavAction::LEFT || a == NavAction::RIGHT) {
        m_physicalKeyboardActive = false;   // d-pad used — restore controller highlight
        navigateWithClamp(a);
        return;
    }

    if (a == NavAction::CONFIRM) {
        activateSelected();
        return;
    }

    // Keyboard text input (physical keyboard support)
    if (e.type == SDL_TEXTINPUT) {
        m_physicalKeyboardActive = true;
        const char* inp = e.text.text;
        while (*inp) {
            if (m_maxLen == 0 || (int)m_text.size() < m_maxLen)
                m_text += *inp;
            ++inp;
        }
        // Flash the matching OSK key so physical typing feels connected
        if (e.text.text[0] != '\0')
            flashKeyForChar(e.text.text[0]);
        return;
    }
    if (e.type == SDL_KEYDOWN) {
        m_physicalKeyboardActive = true;
        if (e.key.keysym.sym == SDLK_BACKSPACE && !m_text.empty()) {
            m_text.pop_back();
        } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
            m_open       = false;
            m_wantsClose = true;
            if (m_callback) m_callback(true, m_text);
        } else if (e.key.keysym.sym == SDLK_ESCAPE) {
            m_open       = false;
            m_wantsClose = true;
            if (m_callback) m_callback(false, "");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::update(float deltaMs) {
    if (!m_open) return;

    // Drive d-pad hold-to-scroll — same pattern as SettingsScreen / GameBrowser
    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held == NavAction::UP || held == NavAction::DOWN ||
        held == NavAction::LEFT || held == NavAction::RIGHT) {
        m_physicalKeyboardActive = false;
        navigateWithClamp(held);
    } else if (held == NavAction::OPTIONS) {
        // Hold Square = hold backspace
        if (!m_text.empty()) m_text.pop_back();
    }

    // Tick down the physical-key flash timer
    if (m_flashMs > 0) {
        m_flashMs -= (int)deltaMs;
        if (m_flashMs < 0) m_flashMs = 0;
    }

    // Cursor blink
    m_cursorBlinkMs += (Uint32)deltaMs;
    if (m_cursorBlinkMs >= 530) {
        m_cursorBlinkMs = 0;
        m_cursorVisible = !m_cursorVisible;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Asset loading
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::loadAssets() {
    auto load = [&](const char* path) -> SDL_Texture* {
        SDL_Texture* t = IMG_LoadTexture(m_renderer, path);
        if (!t) std::cerr << "[OSK] Could not load asset: " << path << "\n";
        return t;
    };
    m_texCross  = load("assets/osk/cross.png");
    m_texCircle = load("assets/osk/circle.png");
    m_texSquare = load("assets/osk/square.png");
    m_texDpad   = load("assets/osk/dpad.png");
    m_assetsLoaded = true;
}

void OnScreenKeyboard::freeAssets() {
    auto free = [](SDL_Texture*& t) { if (t) { SDL_DestroyTexture(t); t = nullptr; } };
    free(m_texCross); free(m_texCircle); free(m_texSquare); free(m_texDpad);
}

void OnScreenKeyboard::drawButtonIcon(SDL_Texture* tex, const std::string& fallback,
                                      int x, int y, int size, SDL_Color col)
{
    if (tex) {
        SDL_SetTextureColorMod(tex, col.r, col.g, col.b);
        SDL_Rect dst = { x, y, size, size };
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
    } else {
        // Fallback: filled circle with letter
        SDL_Rect r = { x, y, size, size };
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, 200);
        SDL_RenderFillRect(m_renderer, &r);
        m_theme->drawTextCentered(fallback, x + size/2, y + size/2 - 8,
                                  m_theme->palette().white, FontSize::SMALL);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render helpers
// ─────────────────────────────────────────────────────────────────────────────
SDL_Rect OnScreenKeyboard::overlayRect() const {
    // Overlay: full width minus margins, lower ~65% of screen
    int ovW = m_w - OVERLAY_MARGIN * 2;
    int ovH = (int)(m_h * 0.67f);
    int ovX = OVERLAY_MARGIN;
    int ovY = m_h - ovH;
    return { ovX, ovY, ovW, ovH };
}

std::vector<OnScreenKeyboard::KeyRect>
OnScreenKeyboard::computeRowRects(int rowIdx, int x0, int y0, int rowW) const
{
    const auto& row = m_rows[rowIdx];
    if (row.empty()) return {};

    // Compute total width-units for this row
    float totalUnits = 0.f;
    for (const auto& k : row) totalUnits += k.widthScale;

    // Gaps between keys
    float gapTotal = (float)(KEY_PAD * ((int)row.size() - 1));
    float unitPx = ((float)rowW - gapTotal) / totalUnits;

    std::vector<KeyRect> out;
    float cx = (float)x0;
    for (int i = 0; i < (int)row.size(); ++i) {
        int kw = (int)(row[i].widthScale * unitPx);
        SDL_Rect r = { (int)cx, y0, kw, KEY_H };
        out.push_back({ r, i });
        cx += (float)(kw + KEY_PAD);
    }
    return out;
}

void OnScreenKeyboard::renderOverlayBg(const SDL_Rect& ov) {
    const auto& pal = m_theme->palette();

    // Dim the screen behind the overlay
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 170);
    SDL_Rect fullscreen = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &fullscreen);

    // Panel background
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 245);
    SDL_RenderFillRect(m_renderer, &ov);

    // Top accent line
    SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
    SDL_Rect topLine = { ov.x, ov.y, ov.w, 3 };
    SDL_RenderFillRect(m_renderer, &topLine);
}

void OnScreenKeyboard::renderTextField(int x, int y, int w) {
    const auto& pal = m_theme->palette();
    const int fieldH = TEXT_FIELD_H;
    const int pad    = 12;

    // Prompt label
    m_theme->drawText(m_prompt, x, y, pal.textSecond, FontSize::SMALL);
    int fieldY = y + (int)FontSize::SMALL + 8;

    // Field background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_Rect field = { x, fieldY, w, fieldH };
    SDL_RenderFillRect(m_renderer, &field);

    // Field border — accent if non-empty, dim otherwise
    SDL_Color borderCol = m_text.empty() ? pal.textDisable : pal.accent;
    SDL_SetRenderDrawColor(m_renderer, borderCol.r, borderCol.g, borderCol.b, 255);
    SDL_RenderDrawRect(m_renderer, &field);

    // Text content (masked or plain)
    std::string display;
    if (m_masked) {
        display = std::string(m_text.size(), (char)0xE2); // We'll draw bullet chars
        // Simple approach: show asterisks for password
        display = std::string(m_text.size(), '*');
    } else {
        display = m_text;
    }

    // Cursor
    if (m_cursorVisible) display += "|";

    // Render with truncation from the right if too long
    int textX = x + pad;
    int textY = fieldY + (fieldH - (int)FontSize::BODY) / 2;
    m_theme->drawTextTruncated(display, textX, textY, w - pad * 2,
                               pal.textPrimary, FontSize::BODY);
}

void OnScreenKeyboard::renderKey(const SDL_Rect& r, const OskKey& key, bool selected, bool flashed) {
    const auto& pal = m_theme->palette();

    bool shiftActive = (m_shifted || m_capsLock);

    // For character keys: show lowercase label when unshifted, uppercase when shifted.
    // For symbol/action keys: use the layer label as-is.
    std::string lbl;
    if (!key.isAction && !key.value.empty()) {
        lbl = shiftActive ? key.shiftValue : key.value;
    } else {
        lbl = shiftActive ? key.shiftLabel : key.label;
    }

    // Background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_Color bg;
    if (selected) {
        bg = pal.accent;
    } else if (flashed) {
        // Brief highlight when a physical keyboard key is typed — slightly
        // lighter than the selection color so it's clearly different.
        bg = { (Uint8)std::min(255, pal.accent.r + 60),
               (Uint8)std::min(255, pal.accent.g + 60),
               (Uint8)std::min(255, pal.accent.b + 60), 255 };
    } else if (key.isAction && key.label == "SHIFT" && shiftActive) {
        bg = m_capsLock ? pal.accent : pal.accentDim;
    } else if (key.isAction && key.label == "OK") {
        bg = { 40, 120, 80, 255 };
    } else if (key.isAction) {
        bg = { 35, 35, 75, 255 };
    } else {
        bg = pal.bgCard;
    }
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 220);
    SDL_RenderFillRect(m_renderer, &r);

    // Border on selected
    if (selected) {
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 180);
        SDL_RenderDrawRect(m_renderer, &r);
    }

    // Label text — center in key
    SDL_Color textCol = selected ? pal.white : pal.textPrimary;
    if (key.isAction && !selected) textCol = pal.textSecond;

    m_theme->drawTextCentered(lbl,
        r.x + r.w / 2,
        r.y + (r.h - (int)FontSize::SMALL) / 2,
        textCol, FontSize::SMALL);
}

void OnScreenKeyboard::renderKeyboard(int x, int y, int w, int h) {
    // Distribute rows vertically with even spacing
    int totalRowH   = NUM_ROWS * KEY_H;
    int totalGapH   = (NUM_ROWS - 1) * KEY_PAD;
    int startY = y + std::max(0, (h - totalRowH - totalGapH) / 2);

    for (int ri = 0; ri < NUM_ROWS; ++ri) {
        int rowY  = startY + ri * (KEY_H + KEY_PAD);
        auto keys = computeRowRects(ri, x, rowY, w);
        for (auto& kr : keys) {
            // Suppress controller highlight while user is typing from physical keyboard.
            // It comes back the moment d-pad is touched.
            bool sel     = !m_physicalKeyboardActive && (ri == m_row && kr.keyIdx == m_col);
            bool flashed = (m_flashMs > 0 && ri == m_flashRow && kr.keyIdx == m_flashCol);
            renderKey(kr.rect, m_rows[ri][kr.keyIdx], sel, flashed);
        }
    }
}

void OnScreenKeyboard::renderFooter(const SDL_Rect& ov) {
    const auto& pal = m_theme->palette();
    int footY = ov.y + ov.h - FOOTER_H;
    int cy    = footY + (FOOTER_H - 32) / 2;
    int xPos  = ov.x + 20;
    const int ICON = 32;
    const int GAP  = 8;
    const int STEP = 140;

    // Divider above footer
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_Rect line = { ov.x, footY, ov.w, 1 };
    SDL_RenderFillRect(m_renderer, &line);

    // A / Cross = type key
    drawButtonIcon(m_texCross, "A", xPos, cy, ICON, pal.accent);
    m_theme->drawText("Type Key", xPos + ICON + GAP, cy + (ICON - (int)FontSize::TINY)/2,
                      pal.textSecond, FontSize::TINY);
    xPos += STEP;

    // X / Square = backspace
    drawButtonIcon(m_texSquare, "X", xPos, cy, ICON, pal.textSecond);
    m_theme->drawText("Backspace", xPos + ICON + GAP, cy + (ICON - (int)FontSize::TINY)/2,
                      pal.textSecond, FontSize::TINY);
    xPos += STEP;

    // B / Circle = cancel
    drawButtonIcon(m_texCircle, "B", xPos, cy, ICON, pal.textSecond);
    m_theme->drawText("Cancel", xPos + ICON + GAP, cy + (ICON - (int)FontSize::TINY)/2,
                      pal.textSecond, FontSize::TINY);
    xPos += STEP;

    // D-pad = navigate
    drawButtonIcon(m_texDpad, "+", xPos, cy, ICON, pal.textDisable);
    m_theme->drawText("Navigate", xPos + ICON + GAP, cy + (ICON - (int)FontSize::TINY)/2,
                      pal.textDisable, FontSize::TINY);
}

// ─────────────────────────────────────────────────────────────────────────────
//  render — top-level (call after your screen has rendered, so OSK is on top)
// ─────────────────────────────────────────────────────────────────────────────
void OnScreenKeyboard::render() {
    if (!m_open) return;
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);

    const SDL_Rect ov = overlayRect();
    const int innerX  = ov.x + 20;
    const int innerW  = ov.w - 40;

    renderOverlayBg(ov);

    // Text field sits at the top of the overlay, above the keyboard
    int tfTop = ov.y + 16;
    renderTextField(innerX, tfTop, innerW);

    // Keyboard fills the space between text field and footer
    int kbTop  = tfTop + (int)FontSize::SMALL + 8 + TEXT_FIELD_H + 12;
    int kbBot  = ov.y + ov.h - FOOTER_H - 8;
    int kbH    = kbBot - kbTop;
    if (kbH > 0)
        renderKeyboard(innerX, kbTop, innerW, kbH);

    renderFooter(ov);
}
