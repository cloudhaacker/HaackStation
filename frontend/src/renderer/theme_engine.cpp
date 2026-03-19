#include "renderer/theme_engine.h"
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <iostream>
#include <algorithm>

// ─── Built-in font fallback path list ─────────────────────────────────────────
// Display font search paths (Zrnic — splash screen title only)
static const char* DISPLAY_FONT_PATHS[] = {
    "assets/fonts/zrnic.otf",
    "assets/fonts/Zrnic.otf",
    "assets/fonts/zrnic.ttf",
    nullptr
};

// UI font search paths (clean readable font for all menus, cards, settings)
static const char* FONT_PATHS[] = {
    // Windows
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\calibri.ttf",
    // Linux
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    // Android (common paths)
    "/system/fonts/Roboto-Regular.ttf",
    "/system/fonts/DroidSans.ttf",
    nullptr
};

ThemeEngine::ThemeEngine(SDL_Renderer* renderer)
    : m_renderer(renderer)
{
    if (TTF_Init() != 0) {
        std::cerr << "[ThemeEngine] TTF_Init failed: " << TTF_GetError() << "\n";
    }

    // Find display font (Zrnic — splash screen only)
    for (int i = 0; DISPLAY_FONT_PATHS[i]; ++i) {
        if (SDL_RWops* rw = SDL_RWFromFile(DISPLAY_FONT_PATHS[i], "r")) {
            SDL_RWclose(rw);
            m_displayFontPath = DISPLAY_FONT_PATHS[i];
            std::cout << "[ThemeEngine] Display font (splash): " << m_displayFontPath << "\n";
            break;
        }
    }
    if (m_displayFontPath.empty()) {
        std::cout << "[ThemeEngine] Zrnic not found — splash will use UI font\n";
    }

    // Find UI font (clean readable, used everywhere else)
    for (int i = 0; FONT_PATHS[i]; ++i) {
        if (SDL_RWops* rw = SDL_RWFromFile(FONT_PATHS[i], "r")) {
            SDL_RWclose(rw);
            m_fontPath = FONT_PATHS[i];
            std::cout << "[ThemeEngine] UI font: " << m_fontPath << "\n";
            break;
        }
    }
    if (m_fontPath.empty()) {
        std::cerr << "[ThemeEngine] WARNING: No UI font found. Text may not render.\n";
    }

    // Pre-load common sizes
    getFont(FontSize::SMALL);
    getFont(FontSize::BODY);
    getFont(FontSize::TITLE);
}

ThemeEngine::~ThemeEngine() {
    clearTextCache();
    for (auto& [size, font] : m_fonts)
        if (font) TTF_CloseFont(font);
    for (auto& [size, font] : m_displayFonts)
        if (font) TTF_CloseFont(font);
    TTF_Quit();
}

// ─── Font loading ─────────────────────────────────────────────────────────────
TTF_Font* ThemeEngine::getFont(FontSize size, bool displayFont) {
    int pt = static_cast<int>(size);
    auto& map  = displayFont ? m_displayFonts : m_fonts;
    auto& path = displayFont ? m_displayFontPath : m_fontPath;

    // Fall back to UI font if display font not available
    if (displayFont && path.empty())
        return getFont(size, false);

    auto it = map.find(pt);
    if (it != map.end()) return it->second;

    TTF_Font* font = nullptr;
    if (!path.empty()) {
        font = TTF_OpenFont(path.c_str(), pt);
    }
    if (!font) {
        std::cerr << "[ThemeEngine] Could not load "
                  << (displayFont ? "display" : "UI")
                  << " font size " << pt << "\n";
    }
    map[pt] = font;
    return font;
}

// ─── Drawing primitives ───────────────────────────────────────────────────────
void ThemeEngine::drawRect(const SDL_Rect& r, SDL_Color c, int alpha) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, (Uint8)alpha);
    SDL_RenderFillRect(m_renderer, &r);
}

void ThemeEngine::drawLine(int x1, int y1, int x2, int y2, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(m_renderer, x1, y1, x2, y2);
}

// Rounded rectangle using horizontal scan lines
void ThemeEngine::drawRoundRect(const SDL_Rect& r, SDL_Color c, int radius, int alpha) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, (Uint8)alpha);

    radius = std::min(radius, std::min(r.w, r.h) / 2);

    // Fill center body (3 rects: top strip, middle, bottom strip)
    SDL_Rect center = { r.x + radius, r.y, r.w - radius*2, r.h };
    SDL_RenderFillRect(m_renderer, &center);
    SDL_Rect left  = { r.x, r.y + radius, radius, r.h - radius*2 };
    SDL_RenderFillRect(m_renderer, &left);
    SDL_Rect right = { r.x + r.w - radius, r.y + radius, radius, r.h - radius*2 };
    SDL_RenderFillRect(m_renderer, &right);

    // Fill corners with circle approximation (pixel rows)
    for (int dy = 0; dy < radius; dy++) {
        int dx = (int)std::sqrt((float)(radius*radius - dy*dy));
        // Top-left
        SDL_RenderDrawLine(m_renderer, r.x + radius - dx, r.y + radius - dy - 1,
                           r.x + radius,                  r.y + radius - dy - 1);
        // Top-right
        SDL_RenderDrawLine(m_renderer, r.x + r.w - radius, r.y + radius - dy - 1,
                           r.x + r.w - radius + dx,        r.y + radius - dy - 1);
        // Bottom-left
        SDL_RenderDrawLine(m_renderer, r.x + radius - dx, r.y + r.h - radius + dy,
                           r.x + radius,                  r.y + r.h - radius + dy);
        // Bottom-right
        SDL_RenderDrawLine(m_renderer, r.x + r.w - radius, r.y + r.h - radius + dy,
                           r.x + r.w - radius + dx,        r.y + r.h - radius + dy);
    }
}

void ThemeEngine::drawRoundRectOutline(const SDL_Rect& r, SDL_Color c, int radius, int thick) {
    for (int i = 0; i < thick; i++) {
        SDL_Rect inner = { r.x + i, r.y + i, r.w - i*2, r.h - i*2 };
        // Just draw the 4 edge lines (simple outline, good enough for cards)
        SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, c.a);
        // Top
        SDL_RenderDrawLine(m_renderer, inner.x + radius, inner.y,
                           inner.x + inner.w - radius, inner.y);
        // Bottom
        SDL_RenderDrawLine(m_renderer, inner.x + radius, inner.y + inner.h - 1,
                           inner.x + inner.w - radius, inner.y + inner.h - 1);
        // Left
        SDL_RenderDrawLine(m_renderer, inner.x, inner.y + radius,
                           inner.x, inner.y + inner.h - radius);
        // Right
        SDL_RenderDrawLine(m_renderer, inner.x + inner.w - 1, inner.y + radius,
                           inner.x + inner.w - 1, inner.y + inner.h - radius);
    }
}

void ThemeEngine::drawShadow(const SDL_Rect& r, int radius, int spread) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    for (int i = spread; i > 0; i--) {
        Uint8 alpha = (Uint8)(80 * i / spread);
        SDL_Rect shadow = { r.x + i, r.y + i, r.w, r.h };
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, alpha);
        SDL_Rect line;
        // Just fill a slightly offset rect for the shadow effect
        line = { shadow.x + radius, shadow.y, shadow.w - radius*2, 1 };
        SDL_RenderFillRect(m_renderer, &line);
        line = { shadow.x + radius, shadow.y + shadow.h - 1, shadow.w - radius*2, 1 };
        SDL_RenderFillRect(m_renderer, &line);
        line = { shadow.x, shadow.y + radius, 1, shadow.h - radius*2 };
        SDL_RenderFillRect(m_renderer, &line);
        line = { shadow.x + shadow.w - 1, shadow.y + radius, 1, shadow.h - radius*2 };
        SDL_RenderFillRect(m_renderer, &line);
    }
}

// ─── Text rendering ───────────────────────────────────────────────────────────
SDL_Texture* ThemeEngine::renderTextToTexture(const std::string& text, TTF_Font* font, SDL_Color c) {
    if (!font || text.empty()) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

int ThemeEngine::drawText(const std::string& text, int x, int y, SDL_Color c,
                           FontSize size, bool useDisplayFont) {
    TTF_Font* font = getFont(size, useDisplayFont);
    if (!font) return 0;
    SDL_Texture* tex = renderTextToTexture(text, font, c);
    if (!tex) return 0;
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    return w;
}

int ThemeEngine::drawTextCentered(const std::string& text, int cx, int y, SDL_Color c,
                                   FontSize size, bool useDisplayFont) {
    TTF_Font* font = getFont(size, useDisplayFont);
    if (!font) return 0;
    int w, h;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return drawText(text, cx - w/2, y, c, size, useDisplayFont);
}

int ThemeEngine::drawTextTruncated(const std::string& text, int x, int y, int maxW,
                                    SDL_Color c, FontSize size) {
    TTF_Font* font = getFont(size);
    if (!font) return 0;

    int w, h;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);

    if (w <= maxW) return drawText(text, x, y, c, size);

    // Truncate with ellipsis
    std::string truncated = text;
    const std::string ellipsis = "...";
    int ewW, ewH;
    TTF_SizeUTF8(font, ellipsis.c_str(), &ewW, &ewH);
    int targetW = maxW - ewW;

    while (!truncated.empty()) {
        TTF_SizeUTF8(font, truncated.c_str(), &w, &h);
        if (w <= targetW) break;
        truncated.pop_back();
    }
    truncated += ellipsis;
    return drawText(truncated, x, y, c, size);
}

void ThemeEngine::measureText(const std::string& text, FontSize size,
                               int& w, int& h, bool useDisplayFont) {
    TTF_Font* font = getFont(size, useDisplayFont);
    if (!font) { w = 0; h = 0; return; }
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
}

// ─── Game card ────────────────────────────────────────────────────────────────
void ThemeEngine::drawGameCard(const SDL_Rect& r, const std::string& title,
                                bool selected, bool isMultiDisc, int discCount,
                                SDL_Texture* coverArt, float selectionAnim)
{
    const int RADIUS = 10;

    // Shadow (only for selected card, grows with animation)
    if (selected) {
        int spread = (int)(8 * selectionAnim);
        drawShadow(r, RADIUS, spread);
    }

    // Card background
    SDL_Color bg = selected
        ? SDL_Color{ (Uint8)Ease::lerp(m_palette.bgCard.r, m_palette.bgCardHover.r, selectionAnim),
                     (Uint8)Ease::lerp(m_palette.bgCard.g, m_palette.bgCardHover.g, selectionAnim),
                     (Uint8)Ease::lerp(m_palette.bgCard.b, m_palette.bgCardHover.b, selectionAnim),
                     255 }
        : m_palette.bgCard;

    drawRoundRect(r, bg, RADIUS);

    // Selection accent border
    if (selected) {
        Uint8 alpha = (Uint8)(255 * selectionAnim);
        SDL_Color accent = m_palette.accent;
        accent.a = alpha;
        drawRoundRectOutline(r, accent, RADIUS, 3);
    }

    // Cover art area (top portion of card, ~70% of height)
    int coverH = (int)(r.h * 0.68f);
    SDL_Rect coverRect = { r.x + 4, r.y + 4, r.w - 8, coverH };

    if (coverArt) {
        SDL_RenderCopy(m_renderer, coverArt, nullptr, &coverRect);
    } else {
        // Placeholder cover art: gradient-style dark rect with PS1 icon style
        drawRoundRect(coverRect, m_palette.bgPanel, 6);

        // Draw a stylized "?" or PS1-ish placeholder
        SDL_Color iconColor = selected ? m_palette.accent : m_palette.textDisable;
        drawTextCentered("?", coverRect.x + coverRect.w/2,
                         coverRect.y + coverRect.h/2 - 16,
                         iconColor, FontSize::HERO);
    }

    // Title text (bottom portion of card)
    int titleY = r.y + coverH + 8;
    int titleMaxW = r.w - 10;
    drawTextTruncated(title, r.x + 5, titleY, titleMaxW,
                      selected ? m_palette.textPrimary : m_palette.textSecond,
                      FontSize::SMALL);

    // Multi-disc badge (top-right corner)
    if (isMultiDisc) {
        std::string badge = std::to_string(discCount) + "D";
        int bw = 28, bh = 18;
        SDL_Rect badgeRect = { r.x + r.w - bw - 4, r.y + 4, bw, bh };
        drawRoundRect(badgeRect, m_palette.multiDisc, 4);
        drawTextCentered(badge, badgeRect.x + bw/2, badgeRect.y + 2,
                         m_palette.white, FontSize::TINY);
    }
}

// ─── Header bar ───────────────────────────────────────────────────────────────
void ThemeEngine::drawHeader(int w, int /*h*/, const std::string& title,
                              const std::string& subtitle, int gameCount)
{
    // Background bar
    SDL_Rect bar = { 0, 0, w, m_layout.headerH };
    drawRect(bar, m_palette.bgPanel);

    // Bottom accent line
    drawLine(0, m_layout.headerH - 2, w, m_layout.headerH - 2, m_palette.accent);
    drawLine(0, m_layout.headerH - 1, w, m_layout.headerH - 1, m_palette.accentDim);

    // Title
    drawText(title, 24, 14, m_palette.textPrimary, FontSize::HEADER);

    // Subtitle (version or current filter)
    if (!subtitle.empty()) {
        int tw, th;
        measureText(title, FontSize::HEADER, tw, th);
        drawText(subtitle, 24 + tw + 12, 22, m_palette.textSecond, FontSize::BODY);
    }

    // Game count (right-aligned)
    std::string countStr = std::to_string(gameCount) + " game" + (gameCount != 1 ? "s" : "");
    int cw, ch;
    measureText(countStr, FontSize::BODY, cw, ch);
    drawText(countStr, w - cw - 24, (m_layout.headerH - ch) / 2,
             m_palette.textSecond, FontSize::BODY);
}

// ─── Footer button hints ──────────────────────────────────────────────────────
void ThemeEngine::drawFooterHints(int w, int h, const std::string& confirmLabel,
                                   const std::string& optionsLabel)
{
    int y = h - m_layout.footerH;

    // Background
    SDL_Rect bar = { 0, y, w, m_layout.footerH };
    drawRect(bar, m_palette.bgPanel);
    drawLine(0, y, w, y, m_palette.gridLine);

    int xPos = 24;
    int cy = y + m_layout.footerH / 2 - 10;

    // Cross / A button
    drawButtonHint(xPos, cy, "A", confirmLabel, m_palette.accent);
    xPos += 120;

    // Circle / B button
    drawButtonHint(xPos, cy, "B", "Back", m_palette.textSecond);
    xPos += 100;

    // Triangle / Y button
    if (!optionsLabel.empty()) {
        drawButtonHint(xPos, cy, "Y", optionsLabel, m_palette.textSecond);
    }
}

void ThemeEngine::drawButtonHint(int x, int y, const std::string& button,
                                  const std::string& label, SDL_Color btnColor)
{
    // Button circle
    SDL_Rect circle = { x, y, 24, 24 };
    drawRoundRect(circle, btnColor, 12);
    drawTextCentered(button, x + 12, y + 4, m_palette.white, FontSize::TINY);

    // Label
    drawText(label, x + 30, y + 4, m_palette.textSecond, FontSize::SMALL);
}

// ─── Scrollbar ────────────────────────────────────────────────────────────────
void ThemeEngine::drawScrollBar(int x, int y, int h, int total, int visible, int offset) {
    if (total <= visible) return;

    // Track
    SDL_Rect track = { x, y, 4, h };
    drawRect(track, m_palette.bgCard);

    // Thumb
    float ratio  = (float)visible / total;
    float pos    = (float)offset  / total;
    int   thumbH = std::max(20, (int)(h * ratio));
    int   thumbY = y + (int)((h - thumbH) * pos);

    SDL_Rect thumb = { x, thumbY, 4, thumbH };
    drawRect(thumb, m_palette.accent);
}

// ─── Loading spinner ─────────────────────────────────────────────────────────
void ThemeEngine::drawLoadingSpinner(int cx, int cy, float angle, SDL_Color c) {
    const int SPOKES = 8;
    const float INNER = 12.f, OUTER = 20.f;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < SPOKES; i++) {
        float a = angle + (float)i * (2.f * 3.14159f / SPOKES);
        float fade = (float)i / SPOKES;
        SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, (Uint8)(c.a * fade));

        int x1 = (int)(cx + INNER * std::cos(a));
        int y1 = (int)(cy + INNER * std::sin(a));
        int x2 = (int)(cx + OUTER * std::cos(a));
        int y2 = (int)(cy + OUTER * std::sin(a));
        SDL_RenderDrawLine(m_renderer, x1, y1, x2, y2);
    }
}

void ThemeEngine::clearTextCache() {
    for (auto& [key, cached] : m_textCache) {
        if (cached.tex) SDL_DestroyTexture(cached.tex);
    }
    m_textCache.clear();
}
