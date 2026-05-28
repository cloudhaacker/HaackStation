#include "ui/haack_hub.h"
#include "settings_screen.h"       // HaackSettings
#include "renderer/theme_engine.h" // ThemeEngine, Palette, FontSize
#include "ui/controller_nav.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

HaackHub::HaackHub(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    buildTiles();
    m_selectedIdx = firstEnabledIdx();

    if (m_renderer)
        SDL_GetRendererOutputSize(m_renderer, &m_screenW, &m_screenH);
}

void HaackHub::buildTiles() {
    // Icon colors pulled from haackstation_color_reference.txt toast families
    m_tiles = {
        {
            HubTileID::TROPHY_HUB,
            "Trophy Hub",
            "Achievements, unlock history & stats",
            "",                             // enabled
            {42,  14,  24, 255},            // icon bg — dark red
            {233, 69,  96, 255},            // icon fg — accent
        },
        {
            HubTileID::OMNISAVE,
            "OmniSave",
            "Save states & memory card shelf",
            "",                             // enabled
            {12,  30,  56, 255},            // icon bg — dark cyan
            {60,  160, 220, 255},           // icon fg — cyan-blue
        },
        {
            HubTileID::COLLECTIONS,
            "Collections",
            "User-curated game lists",
            "coming soon",
            {42,  29,  6,  255},            // icon bg — dark amber
            {210, 170, 50, 255},            // icon fg — gold
        },
        {
            HubTileID::PLAY_HISTORY,
            "Play History",
            "Session log & hours per game",
            "coming soon",
            {12,  40,  20, 255},            // icon bg — dark green
            {60,  200, 120, 255},           // icon fg — green
        },
        {
            HubTileID::PROFILE,
            "Profile",
            "Multi-user profiles (v0.8)",
            "v0.8",
            {28,  28,  52, 255},            // icon bg — muted navy
            {150, 150, 190, 255},           // icon fg — textSecond
        },
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::open(const HaackSettings& settings) {
    m_open             = true;
    m_openAnim         = 0.0f;
    m_selectAnim       = 0.0f;
    m_hasPendingAction = false;
    m_pendingAction    = HubTileID::NONE;
    setRaEnabled(settings.raEnabled);
    m_selectedIdx = firstEnabledIdx();

    if (m_renderer)
        SDL_GetRendererOutputSize(m_renderer, &m_screenW, &m_screenH);
}

void HaackHub::close() {
    m_open = false;
}

void HaackHub::setRaEnabled(bool enabled) {
    m_raEnabled = enabled;
    for (auto& t : m_tiles) {
        if (t.id == HubTileID::TROPHY_HUB) {
            t.stubLabel = enabled ? "" : "disabled";
        }
    }
    // Re-seat selection if it landed on a now-disabled tile
    if (!m_tiles[m_selectedIdx].enabled())
        m_selectedIdx = firstEnabledIdx();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::update(float dt) {
    if (!m_open) return;

    // Fade-in — reach 1.0 in ~180ms (dt is milliseconds, matching other screens)
    if (m_openAnim < 1.0f) {
        m_openAnim += dt / 180.0f;
        if (m_openAnim > 1.0f) m_openAnim = 1.0f;
    }

    // Selection pulse — decays quickly
    if (m_selectAnim > 0.0f) {
        m_selectAnim -= dt / 150.0f;
        if (m_selectAnim < 0.0f) m_selectAnim = 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::handleEvent(const SDL_Event& e) {
    if (!m_open) return;

    auto isKey = [&](SDL_Keycode k) {
        return e.type == SDL_KEYDOWN && e.key.keysym.sym == k;
    };
    auto isButton = [&](Uint8 btn) {
        return e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == btn;
    };

    // Navigate left
    if (isKey(SDLK_LEFT) || isButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        moveLeft();
        return;
    }

    // Navigate right
    if (isKey(SDLK_RIGHT) || isButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        moveRight();
        return;
    }

    // Confirm — A button or Z key (standard HaackStation confirm)
    if (isKey(SDLK_z) || isButton(SDL_CONTROLLER_BUTTON_A)) {
        const HubTile& tile = m_tiles[m_selectedIdx];
        if (tile.enabled()) {
            m_pendingAction    = tile.id;
            m_hasPendingAction = true;
        }
        return;
    }

    // Cancel — B button or X key (standard HaackStation cancel)
    if (isKey(SDLK_x) || isKey(SDLK_ESCAPE) ||
        isButton(SDL_CONTROLLER_BUTTON_B)) {
        close();
        return;
    }
}

void HaackHub::moveLeft() {
    int idx = m_selectedIdx - 1;
    while (idx >= 0 && !m_tiles[idx].enabled()) --idx;
    if (idx >= 0) {
        m_animFromIdx = m_selectedIdx;
        m_selectedIdx = idx;
        m_selectAnim  = 1.0f;
    }
}

void HaackHub::moveRight() {
    int idx = m_selectedIdx + 1;
    while (idx < (int)m_tiles.size() && !m_tiles[idx].enabled()) ++idx;
    if (idx < (int)m_tiles.size()) {
        m_animFromIdx = m_selectedIdx;
        m_selectedIdx = idx;
        m_selectAnim  = 1.0f;
    }
}

int HaackHub::firstEnabledIdx() const {
    for (int i = 0; i < (int)m_tiles.size(); ++i)
        if (m_tiles[i].enabled()) return i;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — top-level
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::render() {
    if (!m_open || !m_renderer || !m_theme) return;

    int w = m_screenW, h = m_screenH;

    // Full-screen dim behind the Hub
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    Uint8 dimAlpha = static_cast<Uint8>(m_openAnim * 180.0f);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, dimAlpha);
    SDL_Rect full = {0, 0, w, h};
    SDL_RenderFillRect(m_renderer, &full);

    renderBackground(w, h);
    renderHeader(w);
    renderTileRow(w, h);
    renderFooter(w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — background
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderBackground(int w, int h) const {
    const auto& pal = m_theme->palette();
    drawRect(0, HEADER_H, w, h - HEADER_H - FOOTER_H, pal.bg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — header  (matches standard HaackStation header pattern)
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderHeader(int w) const {
    const auto& pal = m_theme->palette();

    // Bar background
    drawRect(0, 0, w, HEADER_H, pal.bgPanel);

    // Double accent underline
    SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, HEADER_H - 2, w, HEADER_H - 2);
    SDL_SetRenderDrawColor(m_renderer, pal.accentDim.r, pal.accentDim.g, pal.accentDim.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, HEADER_H - 1, w, HEADER_H - 1);

    // "HAACKSTATION" subtitle (small, muted)
    m_theme->drawText("HAACKSTATION", 20, 10, pal.textSecond, FontSize::SMALL);

    // "Hub" title — also SMALL for now; bump to a larger size once FontSize
    // enum values beyond SMALL are confirmed in theme_engine.h
    m_theme->drawText("Hub", 20, 24, pal.textPrimary, FontSize::SMALL);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — tile row, dots, info zone
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderTileRow(int w, int h) const {
    int totalTiles = (int)m_tiles.size();
    int totalRowW  = totalTiles * TILE_W + (totalTiles - 1) * TILE_GAP;
    int rowStartX  = (w - totalRowW) / 2;

    // Vertical centering in the body area
    int bodyTop  = HEADER_H;
    int bodyH    = h - HEADER_H - FOOTER_H;
    int contentH = TILE_H + DOT_ZONE_H + INFO_ZONE_H;
    int rowY     = bodyTop + (bodyH - contentH) / 2;

    for (int i = 0; i < totalTiles; ++i) {
        int tileX = rowStartX + i * (TILE_W + TILE_GAP);
        renderSingleTile(m_tiles[i], tileX, rowY,
                         i == m_selectedIdx, m_selectAnim);
    }

    int dotY  = rowY + TILE_H + 8;
    renderDots(w, dotY);

    int infoY = dotY + DOT_ZONE_H;
    renderInfoZone(w, infoY);
}

void HaackHub::renderSingleTile(const HubTile& tile, int x, int y,
                                bool selected, float /*selAnim*/) const {
    const auto& pal = m_theme->palette();
    bool  disabled  = !tile.enabled();
    Uint8 tileAlpha = disabled ? 90 : 255;

    // Selected tile shifts up slightly
    int drawY = selected ? y - 4 : y;

    // Card background
    SDL_Color cardBg = selected ? pal.bgCardHover : pal.bgCard;
    drawRect(x, drawY, TILE_W, TILE_H, cardBg, tileAlpha);

    // Card border
    if (selected)
        drawRectOutline(x, drawY, TILE_W, TILE_H, pal.accent, 255, 2);
    else
        drawRectOutline(x, drawY, TILE_W, TILE_H, pal.gridLine, tileAlpha, 1);

    // Icon background
    int iconX = x + (TILE_W - ICON_SIZE) / 2;
    int iconY = drawY + 18;
    drawRect(iconX, iconY, ICON_SIZE, ICON_SIZE, tile.iconBg, tileAlpha);

    // Icon symbol (SDL line art)
    renderIconSymbol(tile, iconX + ICON_SIZE / 2, iconY + ICON_SIZE / 2);

    // Tile label
    SDL_Color labelCol = disabled
        ? pal.textDisable
        : (selected ? pal.textPrimary : pal.textSecond);
    int labelY = drawY + 18 + ICON_SIZE + 10;
    renderText(tile.label, x, labelY, labelCol, true, TILE_W);

    // Stub label ("coming soon", "v0.8", "disabled")
    if (disabled && !tile.stubLabel.empty())
        renderText(tile.stubLabel, x, labelY + 18, pal.textDisable, true, TILE_W);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — icon symbols  (SDL line art, placeholder until v1.0 textures)
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderIconSymbol(const HubTile& tile, int cx, int cy) const {
    SDL_Color c = tile.iconFg;
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, 255);

    switch (tile.id) {

        case HubTileID::TROPHY_HUB: {
            // Trophy cup
            int bw = 12, tw = 8, ch = 10;
            SDL_RenderDrawLine(m_renderer, cx - tw, cy - ch, cx + tw, cy - ch);
            SDL_RenderDrawLine(m_renderer, cx - tw, cy - ch, cx - bw, cy);
            SDL_RenderDrawLine(m_renderer, cx + tw, cy - ch, cx + bw, cy);
            SDL_RenderDrawLine(m_renderer, cx - bw, cy,      cx + bw, cy);
            SDL_RenderDrawLine(m_renderer, cx,      cy,      cx,      cy + 6);
            SDL_RenderDrawLine(m_renderer, cx - 6,  cy + 6,  cx + 6,  cy + 6);
            SDL_RenderDrawLine(m_renderer, cx - bw, cy - 6,  cx - bw - 4, cy - 3);
            SDL_RenderDrawLine(m_renderer, cx + bw, cy - 6,  cx + bw + 4, cy - 3);
            break;
        }

        case HubTileID::OMNISAVE: {
            // Floppy disk
            int hw = 11, hh = 12;
            SDL_RenderDrawLine(m_renderer, cx - hw, cy - hh, cx + hw, cy - hh);
            SDL_RenderDrawLine(m_renderer, cx + hw, cy - hh, cx + hw, cy + hh);
            SDL_RenderDrawLine(m_renderer, cx + hw, cy + hh, cx - hw, cy + hh);
            SDL_RenderDrawLine(m_renderer, cx - hw, cy + hh, cx - hw, cy - hh);
            SDL_RenderDrawLine(m_renderer, cx - 6,  cy + 2,  cx + 6,  cy + 2);
            SDL_RenderDrawLine(m_renderer, cx - 6,  cy + 2,  cx - 6,  cy + hh);
            SDL_RenderDrawLine(m_renderer, cx + 6,  cy + 2,  cx + 6,  cy + hh);
            SDL_RenderDrawLine(m_renderer, cx + 4,  cy - hh, cx + hw, cy - hh + 4);
            break;
        }

        case HubTileID::COLLECTIONS: {
            // Stack of books
            for (int i = 0; i < 3; ++i) {
                int ox = (i - 1) * 3, oy = (2 - i) * 5 - 5, bw2 = 8, bh = 4;
                SDL_RenderDrawLine(m_renderer, cx-bw2+ox, cy+oy,    cx+bw2+ox, cy+oy);
                SDL_RenderDrawLine(m_renderer, cx+bw2+ox, cy+oy,    cx+bw2+ox, cy+oy+bh);
                SDL_RenderDrawLine(m_renderer, cx+bw2+ox, cy+oy+bh, cx-bw2+ox, cy+oy+bh);
                SDL_RenderDrawLine(m_renderer, cx-bw2+ox, cy+oy+bh, cx-bw2+ox, cy+oy);
            }
            break;
        }

        case HubTileID::PLAY_HISTORY: {
            // Clock face (16-sided polygon approximation)
            int radius = 10;
            for (int i = 0; i < 16; ++i) {
                float a1 = (float)i       / 16.0f * 2.0f * 3.14159f;
                float a2 = (float)(i + 1) / 16.0f * 2.0f * 3.14159f;
                SDL_RenderDrawLine(m_renderer,
                    cx + (int)(radius * cosf(a1)), cy + (int)(radius * sinf(a1)),
                    cx + (int)(radius * cosf(a2)), cy + (int)(radius * sinf(a2)));
            }
            SDL_RenderDrawLine(m_renderer, cx, cy, cx - 5, cy - 7);
            SDL_RenderDrawLine(m_renderer, cx, cy, cx,     cy - 9);
            break;
        }

        case HubTileID::PROFILE: {
            // Person silhouette — head circle + shoulders
            int hr = 5;
            for (int i = 0; i < 8; ++i) {
                float a1 = (float)i       / 8.0f * 2.0f * 3.14159f;
                float a2 = (float)(i + 1) / 8.0f * 2.0f * 3.14159f;
                SDL_RenderDrawLine(m_renderer,
                    cx + (int)(hr * cosf(a1)), cy - 5 + (int)(hr * sinf(a1)),
                    cx + (int)(hr * cosf(a2)), cy - 5 + (int)(hr * sinf(a2)));
            }
            SDL_RenderDrawLine(m_renderer, cx - 10, cy + 10, cx - 8, cy + 4);
            SDL_RenderDrawLine(m_renderer, cx - 8,  cy + 4,  cx,     cy + 2);
            SDL_RenderDrawLine(m_renderer, cx,      cy + 2,  cx + 8, cy + 4);
            SDL_RenderDrawLine(m_renderer, cx + 8,  cy + 4,  cx + 10, cy + 10);
            break;
        }

        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — dot indicators
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderDots(int w, int dotY) const {
    const auto& pal       = m_theme->palette();
    int totalTiles        = (int)m_tiles.size();
    int dotW_inactive     = 6;
    int dotW_active       = 16;
    int dotH              = 6;
    int gap               = 6;
    int totalDotW         = totalTiles * dotW_inactive
                            + (totalTiles - 1) * gap
                            + (dotW_active - dotW_inactive);
    int cx = (w - totalDotW) / 2;

    for (int i = 0; i < totalTiles; ++i) {
        bool      active = (i == m_selectedIdx);
        int       dw     = active ? dotW_active : dotW_inactive;
        SDL_Color dc     = active ? pal.accent : pal.gridLine;
        drawRect(cx, dotY, dw, dotH, dc);
        cx += dw + gap;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — info zone (tile name + description below the dots)
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderInfoZone(int w, int y) const {
    const auto& pal     = m_theme->palette();
    const HubTile& tile = m_tiles[m_selectedIdx];

    renderText(tile.label,       0, y,      pal.textPrimary, true, w);
    renderText(tile.description, 0, y + 20, pal.textSecond,  true, w);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render — footer  (matches standard HaackStation footer pattern)
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::renderFooter(int w, int h) const {
    const auto& pal = m_theme->palette();
    int footerY     = h - FOOTER_H;

    drawRect(0, footerY, w, FOOTER_H, pal.bgPanel);
    SDL_SetRenderDrawColor(m_renderer,
        pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, footerY, w, footerY);

    // Button hints centred — A Open  ·  B Close  ·  < > Navigate
    // Button circles match trophy_hub / omnisave_vault footer pattern
    struct Hint { std::string glyph; std::string label; bool isAccent; };
    const Hint hints[] = {
        { "A", "Open",     true  },
        { "B", "Close",    false },
        { "<>", "Navigate", false },
    };

    int hintCount  = 3;
    int blockW     = 240;
    int hx         = (w - blockW) / 2;
    int hy         = footerY + FOOTER_H / 2;
    int btnR       = 8;
    int colW       = blockW / hintCount;

    for (int i = 0; i < hintCount; ++i) {
        const Hint& hint = hints[i];
        int centreX = hx + i * colW + colW / 2;

        // Button circle fill
        SDL_Color btnCol = hint.isAccent ? pal.accent : pal.textSecond;
        SDL_SetRenderDrawColor(m_renderer,
            btnCol.r, btnCol.g, btnCol.b, 255);
        for (int dy = -btnR; dy <= btnR; ++dy) {
            int dx = (int)sqrtf((float)(btnR * btnR - dy * dy));
            SDL_RenderDrawLine(m_renderer,
                centreX - dx, hy + dy,
                centreX + dx, hy + dy);
        }

        // Glyph letter centred in circle
        m_theme->drawTextCentered(hint.glyph,
            centreX, hy - (int)FontSize::SMALL / 2 + 1,
            pal.white, FontSize::SMALL);

        // Label to the right of circle
        m_theme->drawText(hint.label,
            centreX + btnR + 5, hy - (int)FontSize::SMALL / 2 + 1,
            pal.textSecond, FontSize::SMALL);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SDL draw helpers
// ─────────────────────────────────────────────────────────────────────────────

void HaackHub::drawRect(int x, int y, int w, int h,
                        SDL_Color c, Uint8 alpha) const {
    SDL_SetRenderDrawBlendMode(m_renderer,
        alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, alpha);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(m_renderer, &rect);
}

void HaackHub::drawRectOutline(int x, int y, int w, int h,
                               SDL_Color c, Uint8 alpha, int thickness) const {
    SDL_SetRenderDrawBlendMode(m_renderer,
        alpha < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, alpha);
    for (int t = 0; t < thickness; ++t) {
        SDL_Rect rect = {x + t, y + t, w - t * 2, h - t * 2};
        SDL_RenderDrawRect(m_renderer, &rect);
    }
}

void HaackHub::renderText(const std::string& text, int x, int y,
                          SDL_Color c, bool centerX, int centerXWidth) const {
    if (!m_theme || text.empty()) return;
    if (centerX)
        m_theme->drawTextCentered(text, x + centerXWidth / 2, y, c, FontSize::SMALL);
    else
        m_theme->drawText(text, x, y, c, FontSize::SMALL);
}
