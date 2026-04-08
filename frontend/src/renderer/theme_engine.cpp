#include "theme_engine.h"
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

void ThemeEngine::drawTextWrapped(const std::string& text, int x, int y,
                                   int maxW, SDL_Color color, FontSize size) {
    // Simple word-wrap: split by spaces and add words until line is full
    if (text.empty()) return;
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (!word.empty()) { words.push_back(word); word.clear(); }
            if (c == '\n') words.push_back("\n");
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);

    std::string line;
    int lineY = y;
    int lineH = 18; // approximate line height for TINY font

    for (const auto& w : words) {
        if (w == "\n") {
            if (!line.empty()) {
                drawText(line, x, lineY, color, size);
                lineY += lineH;
                line.clear();
            }
            continue;
        }
        std::string test = line.empty() ? w : line + " " + w;
        int tw = 0, th = 0;
        measureText(test, size, tw, th);
        if (tw > maxW && !line.empty()) {
            drawText(line, x, lineY, color, size);
            lineY += lineH;
            line = w;
        } else {
            line = test;
        }
    }
    if (!line.empty())
        drawText(line, x, lineY, color, size);
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
                                SDL_Texture* coverArt, float selectionAnim,
                                bool isFavorite)
{
    (void)title; // title shown in bottom bar, not on card

    // ── Cover art — fit inside card, preserve aspect ratio ───────────────────
    // img = the actual drawn image rect (may be smaller than r if art isn't
    // exactly the card's aspect ratio). ALL overlays (border, badges, bookmark)
    // reference img so they always align with the image edges, never the card.
    SDL_Rect img = r; // default: image fills card (used for placeholder too)

    if (coverArt) {
        int texW = 0, texH = 0;
        SDL_QueryTexture(coverArt, nullptr, nullptr, &texW, &texH);
        if (texW > 0 && texH > 0) {
            float scale = std::min((float)r.w / texW, (float)r.h / texH);
            int dw = (int)(texW * scale);
            int dh = (int)(texH * scale);
            img = { r.x + (r.w - dw) / 2,
                    r.y + (r.h - dh) / 2, dw, dh };
        }
        SDL_RenderCopy(m_renderer, coverArt, nullptr, &img);
    } else {
        // Placeholder: card-coloured background + big ?
        drawRoundRect(img, m_palette.bgCard, 8);
        SDL_Color iconCol = selected ? m_palette.accent : m_palette.textDisable;
        drawTextCentered("?", img.x + img.w/2,
                         img.y + img.h/2 - (int)FontSize::HERO/2,
                         iconCol, FontSize::HERO);
    }

    // ── All overlays reference img, not r ─────────────────────────────────────
    // This ensures border, disc badge and bookmark always sit on the image
    // edges regardless of the image's aspect ratio or the card's dimensions.

    // Selection border — accent outline drawn on image edges
    if (selected) {
        Uint8 alpha  = (Uint8)(255 * selectionAnim);
        int   spread = (int)(6 * selectionAnim);
        drawShadow(img, 6, spread);
        SDL_Color accent = m_palette.accent;
        accent.a = alpha;
        drawRoundRectOutline(img, accent, 6, 3);
    }

    // Disc badge — top-LEFT of image (metadata tag, like a product label)
    if (isMultiDisc) {
        std::string badge = std::to_string(discCount) + "D";
        int tw = 0, th = 0;
        measureText(badge, FontSize::SMALL, tw, th);
        int bw = tw + 10;
        int bh = th + 6;
        // Anchor to image top-left corner
        SDL_Rect badgeRect = { img.x + 3, img.y + 3, bw, bh };
        drawRoundRect(badgeRect, m_palette.multiDisc, 3);
        drawTextCentered(badge, badgeRect.x + bw/2,
                         badgeRect.y + (bh - th) / 2,
                         m_palette.white, FontSize::SMALL);
    }

    // Bookmark — top-RIGHT of image, slightly inset from right edge.
    // Height ~20% of image height so it's tasteful. Hangs from the top edge.
    if (isFavorite) {
        int bw = std::max(10, img.w * 9 / 100);
        int bh = std::max(18, img.h * 20 / 100);
        // Sit just inside the right edge of the image
        int bx = img.x + img.w - bw - img.w / 14;
        int by = img.y; // hangs from top of image
        SDL_Color gold    = { 255, 200, 40, 235 };
        SDL_Color outline = { 160, 110,  5, 220 };
        drawBookmark(bx, by, bw, bh, gold, outline);
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

    // Title — vertically centred in header
    int titleY = (m_layout.headerH - (int)FontSize::HEADER) / 2 - 2;
    drawText(title, 28, titleY, m_palette.textPrimary, FontSize::HEADER);

    // Subtitle (version string)
    if (!subtitle.empty()) {
        int tw, th;
        measureText(title, FontSize::HEADER, tw, th);
        int subY = titleY + (int)FontSize::HEADER - (int)FontSize::BODY - 2;
        drawText(subtitle, 28 + tw + 14, subY, m_palette.textSecond, FontSize::BODY);
    }

    // Game count (right-aligned)
    if (gameCount > 0) {
        std::string countStr = std::to_string(gameCount) + " game" + (gameCount != 1 ? "s" : "");
        int cw, ch;
        measureText(countStr, FontSize::BODY, cw, ch);
        drawText(countStr, w - cw - 28, (m_layout.headerH - ch) / 2,
                 m_palette.textSecond, FontSize::BODY);
    }
}

// ─── Footer button hints ──────────────────────────────────────────────────────
void ThemeEngine::drawFooterHints(int w, int h,
                                   const std::string& confirmLabel,
                                   const std::string& backLabel,
                                   const std::string& xLabel,
                                   const std::string& yLabel)
{
    int y = h - m_layout.footerH;

    // Background
    SDL_Rect bar = { 0, y, w, m_layout.footerH };
    drawRect(bar, m_palette.bgPanel);
    drawLine(0, y, w, y, m_palette.gridLine);

    int xPos = 28;
    int cy   = y + (m_layout.footerH - 34) / 2;

    // A = confirm/launch
    drawButtonHint(xPos, cy, "A", confirmLabel, m_palette.accent);
    xPos += 160;

    // B = back (always shown)
    std::string bLabel = backLabel.empty() ? "Back" : backLabel;
    drawButtonHint(xPos, cy, "B", bLabel, m_palette.textSecond);
    xPos += 150;

    // X = tertiary action (e.g. Details)
    if (!xLabel.empty()) {
        drawButtonHint(xPos, cy, "X", xLabel, m_palette.textSecond);
        xPos += 150;
    }

    // Y = quaternary action (e.g. Favorite)
    if (!yLabel.empty()) {
        drawButtonHint(xPos, cy, "Y", yLabel, m_palette.textSecond);
    }
}

void ThemeEngine::drawButtonHint(int x, int y, const std::string& button,
                                  const std::string& label, SDL_Color btnColor)
{
    // Button circle — sized for TV readability
    const int BTN_SIZE = 34;
    SDL_Rect circle = { x, y, BTN_SIZE, BTN_SIZE };
    drawRoundRect(circle, btnColor, BTN_SIZE / 2);
    // Centre letter vertically: (BTN_SIZE - font_height) / 2
    // FontSize::SMALL pt is roughly that many pixels tall
    int textY = y + (BTN_SIZE - (int)FontSize::SMALL) / 2 - 1;
    drawTextCentered(button, x + BTN_SIZE / 2, textY, m_palette.white, FontSize::SMALL);

    // Label — vertically aligned with button centre
    drawText(label, x + BTN_SIZE + 8, textY, m_palette.textSecond, FontSize::SMALL);
}

// ─── Star polygon ────────────────────────────────────────────────────────────
// Draws a filled 5-point star centred at (cx,cy). Uses scanline fill so it
// works on any SDL renderer without geometry extensions.
void ThemeEngine::drawStar(int cx, int cy, int outerR, SDL_Color fill, SDL_Color outline) {
    // Build the 10 vertices of a 5-point star (alternating outer/inner points)
    const int    POINTS  = 5;
    const float  PI      = 3.14159265f;
    const float  innerR  = outerR * 0.42f; // classic star inner/outer ratio
    const float  startA  = -PI / 2.f;      // top point

    float vx[10], vy[10];
    for (int i = 0; i < POINTS * 2; i++) {
        float angle  = startA + i * (PI / POINTS);
        float radius = (i % 2 == 0) ? (float)outerR : innerR;
        vx[i] = cx + radius * std::cos(angle);
        vy[i] = cy + radius * std::sin(angle);
    }

    // Scanline fill — find y min/max
    float yMin = vy[0], yMax = vy[0];
    for (int i = 1; i < 10; i++) {
        if (vy[i] < yMin) yMin = vy[i];
        if (vy[i] > yMax) yMax = vy[i];
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);

    for (int y = (int)yMin; y <= (int)yMax; y++) {
        // Find all x intersections of the scanline with polygon edges
        float xs[10];
        int   cnt = 0;
        for (int i = 0; i < 10; i++) {
            int j = (i + 1) % 10;
            float y0 = vy[i], y1 = vy[j];
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                float t = (y - y0) / (y1 - y0);
                xs[cnt++] = vx[i] + t * (vx[j] - vx[i]);
            }
        }
        // Sort intersections and fill between pairs
        for (int a = 0; a < cnt - 1; a++)
            for (int b = a + 1; b < cnt; b++)
                if (xs[a] > xs[b]) { float tmp = xs[a]; xs[a] = xs[b]; xs[b] = tmp; }
        for (int k = 0; k + 1 < cnt; k += 2)
            SDL_RenderDrawLine(m_renderer, (int)xs[k], y, (int)xs[k+1], y);
    }

    // Draw outline
    SDL_SetRenderDrawColor(m_renderer, outline.r, outline.g, outline.b, outline.a);
    for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        SDL_RenderDrawLine(m_renderer, (int)vx[i], (int)vy[i], (int)vx[j], (int)vy[j]);
    }
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
}

// ─── Bookmark badge ──────────────────────────────────────────────────────────
// Bookmark ribbon matching the standard icon: a rectangle where the bottom
// has a V-notch cut upward from the centre.
//
// Shape (viewed from front):
//   ┌──────┐   ← top edge (y)
//   │      │   ← straight sides
//   │      │
//   └─┐  ┌─┘   ← bottom corners (y + bh)
//      \/       ← notch apex points UP, sits notchDepth above bottom corners
//
// So the two OUTER bottom corners are at y+bh, and the notch apex is ABOVE them.
// This matches the standard bookmark/ribbon icon shape.
void ThemeEngine::drawBookmark(int x, int y, int bw, int bh,
                                SDL_Color fill, SDL_Color outline) {
    int midX      = x + bw / 2;
    int bottomY   = y + bh;          // outer bottom corners
    int notchDepth= bh / 3;          // how far up the apex sits from the bottom
    int apexY     = bottomY - notchDepth; // V-notch apex Y (above bottom corners)

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // ── Fill — scanline from top to bottom ───────────────────────────────────
    SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);

    for (int sy = y; sy <= bottomY; sy++) {
        if (sy <= apexY) {
            // Above the notch zone: full-width horizontal line
            SDL_RenderDrawLine(m_renderer, x, sy, x + bw - 1, sy);
        } else {
            // Inside the notch zone: two separate segments either side of the V.
            // The notch opens from width=0 at apexY to full-width at bottomY.
            float t = (float)(sy - apexY) / (float)(bottomY - apexY);
            // Left segment: x  →  midX - gap
            // Right segment: midX + gap  →  x+bw-1
            // gap grows from 0 at apex to bw/2 at bottom
            int gap = (int)(t * (bw / 2));
            int leftEnd    = midX - gap;
            int rightStart = midX + gap;
            if (leftEnd > x)
                SDL_RenderDrawLine(m_renderer, x, sy, leftEnd - 1, sy);
            if (rightStart < x + bw)
                SDL_RenderDrawLine(m_renderer, rightStart, sy, x + bw - 1, sy);
        }
    }

    // ── Outline — trace the perimeter ────────────────────────────────────────
    // Top → right side → right bottom corner → right diagonal up to apex
    //      → left diagonal down to left bottom corner → left side → top
    SDL_SetRenderDrawColor(m_renderer, outline.r, outline.g, outline.b, outline.a);
    // Top edge
    SDL_RenderDrawLine(m_renderer, x,        y,       x + bw - 1, y);
    // Right vertical side
    SDL_RenderDrawLine(m_renderer, x + bw - 1, y,     x + bw - 1, bottomY);
    // Right diagonal: bottom-right corner up to apex
    SDL_RenderDrawLine(m_renderer, x + bw - 1, bottomY, midX,     apexY);
    // Left diagonal: apex down to bottom-left corner
    SDL_RenderDrawLine(m_renderer, midX,      apexY,   x,         bottomY);
    // Left vertical side
    SDL_RenderDrawLine(m_renderer, x,         bottomY, x,         y);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
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
