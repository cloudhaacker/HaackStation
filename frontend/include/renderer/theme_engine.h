#pragma once
// theme_engine.h
// Owns all visual styling for HaackStation's UI.
// Provides colors, layout geometry, font rendering helpers,
// and simple animation utilities (easing, lerp).
//
// The bold "game shelf" aesthetic:
//   - Deep navy/dark background  (#0D0D1A)
//   - Vivid accent red           (#E94560)
//   - Soft blue highlight        (#1A1A3E)
//   - Clean white text           (#EEEEFF)
//   - Subtle grid lines          (#1E1E3A)

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>

// ─── Colour palette ───────────────────────────────────────────────────────────
struct Palette {
    SDL_Color bg          = {13,  13,  26,  255};  // Deep navy
    SDL_Color bgPanel     = {20,  20,  50,  255};  // Slightly lighter panel
    SDL_Color bgCard      = {26,  26,  62,  255};  // Game card background
    SDL_Color bgCardHover = {38,  38,  90,  255};  // Selected card
    SDL_Color accent      = {233, 69,  96,  255};  // Vivid red accent
    SDL_Color accentDim   = {140, 40,  58,  255};  // Dimmed accent
    SDL_Color textPrimary = {238, 238, 255, 255};  // Near-white
    SDL_Color textSecond  = {150, 150, 190, 255};  // Dim secondary text
    SDL_Color textDisable = {80,  80,  110, 255};  // Disabled / hint
    SDL_Color gridLine    = {30,  30,  58,  255};  // Subtle dividers
    SDL_Color shadow      = {0,   0,   0,   180};  // Drop shadow
    SDL_Color white       = {255, 255, 255, 255};
    SDL_Color black       = {0,   0,   0,   255};
    SDL_Color multiDisc   = {80,  200, 120, 255};  // Green badge for multi-disc
};

// ─── Layout constants ─────────────────────────────────────────────────────────
struct Layout {
    // ── Grid geometry — all derived in recalculate(), never set directly ──────
    int cardW         = 200;   // Derived from column count + window width
    int cardH         = 260;   // Derived from cardW (PS1 box is ~0.72 aspect: w*1.3)
    int cardPadX      = 20;    // Horizontal gap between cards
    int cardPadY      = 24;    // Vertical gap between cards
    int shelfPadLeft  = 48;    // Left margin of the shelf
    int shelfPadTop   = 130;   // Top margin (below header bar)

    // Header bar
    int headerH       = 90;    // Height of top bar (larger for TV)

    // Footer hint bar
    int footerH       = 60;    // Height of bottom button hint bar (larger for TV)

    // Cards per row (set by recalculate based on library size)
    int cardsPerRow   = 4;

    // recalculate() — call whenever window resizes OR library changes.
    // gameCount drives column choice; card dimensions fill available width.
    // Always pass gameCount so column count stays correct every frame.
    void recalculate(int windowW, int windowH, int gameCount = -1) {
        // ── Choose column count based on library size ─────────────────────────
        // gameCount == -1 means "keep current cardsPerRow" (called from render
        // without a count — don't override what was set by rebuildActiveList).
        if (gameCount >= 0) {
            if      (gameCount <  7)  cardsPerRow = 3;  // small: 3 nice big cards
            else if (gameCount < 20)  cardsPerRow = 4;  // medium: 4 columns
            else if (gameCount < 50)  cardsPerRow = 5;  // large: 5 columns
            else                      cardsPerRow = 6;  // very large: 6 max
        }

        // ── Derive card size from column count and available width ────────────
        // Cards fill the shelf width exactly — padding between, margin at sides.
        // shelfPadLeft is the outer margin; inner gaps are cardPadX.
        int available = windowW - shelfPadLeft * 2;
        cardW = (available - cardPadX * (cardsPerRow - 1)) / cardsPerRow;
        cardW = std::max(120, cardW); // safety floor only, no ceiling

        // PS1 case art with PlayStation spine is roughly square.
        // Title strip (~20%) below image area makes card slightly taller.
        // PS1 cases are slightly portrait (spine + cover ~1:1.05).
        // We use a mild portrait ratio so the card area suits the art naturally.
        // Cover art is drawn fit-to-card (letterbox/pillarbox with shelf bg
        // showing through gaps) so any aspect ratio image looks correct.
        cardH = (int)(cardW * 1.05f);

        // shelfPadTop must fit: headerH + shelf-indicator bar (name + dots + margin)
        // Shelf indicator needs ~46px: name text (~22px) + dots row (~14px) + gaps
        shelfPadTop = headerH + 50;
        (void)windowH;
    }
};

// ─── Font sizes ───────────────────────────────────────────────────────────────
enum class FontSize {
    TINY   = 14,   // was 11 — readable at TV distance
    SMALL  = 17,   // was 14
    BODY   = 22,   // was 18
    TITLE  = 28,   // was 24
    HEADER = 38,   // was 32
    HERO   = 56,   // was 48
};

// ─── Easing functions ────────────────────────────────────────────────────────
namespace Ease {
    inline float linear(float t)    { return t; }
    inline float outQuad(float t)   { return t * (2.f - t); }
    inline float outCubic(float t)  { float s = 1.f - t; return 1.f - s*s*s; }
    inline float inOutQuad(float t) { return t < .5f ? 2*t*t : -1+(4-2*t)*t; }
    inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
}

// ─── ThemeEngine ─────────────────────────────────────────────────────────────
class ThemeEngine {
public:
    explicit ThemeEngine(SDL_Renderer* renderer);
    ~ThemeEngine();

    // ── Drawing primitives ────────────────────────────────────────────────────
    void drawRect(const SDL_Rect& r, SDL_Color c, int alpha = 255);
    void drawRoundRect(const SDL_Rect& r, SDL_Color c, int radius, int alpha = 255);
    void drawRoundRectOutline(const SDL_Rect& r, SDL_Color c, int radius, int thick = 2);
    void drawLine(int x1, int y1, int x2, int y2, SDL_Color c);
    void drawShadow(const SDL_Rect& r, int radius = 8, int spread = 6);

    // ── Text rendering ────────────────────────────────────────────────────────
    // Returns the width of rendered text in pixels
    // useDisplayFont=true uses Zrnic (splash screen title only)
    // useDisplayFont=false uses the clean UI font (all menus, cards, settings)
    int  drawText(const std::string& text, int x, int y, SDL_Color c, FontSize size,
                  bool useDisplayFont = false);
    int  drawTextCentered(const std::string& text, int cx, int y, SDL_Color c, FontSize size,
                          bool useDisplayFont = false);
    int  drawTextTruncated(const std::string& text, int x, int y, int maxW, SDL_Color c, FontSize size);
    void drawTextWrapped(const std::string& text, int x, int y, int maxW, SDL_Color c, FontSize size);
    void measureText(const std::string& text, FontSize size, int& w, int& h,
                     bool useDisplayFont = false);

    bool hasDisplayFont() const { return m_displayFontPath.empty() == false; }

    // ── Game card drawing ─────────────────────────────────────────────────────
    // Draws a full game card (cover art placeholder + title + badges)
    void drawGameCard(const SDL_Rect& r, const std::string& title,
                      bool selected, bool isMultiDisc, int discCount,
                      SDL_Texture* coverArt = nullptr,
                      float selectionAnim = 1.f,
                      bool isFavorite = false);

    // ── UI components ─────────────────────────────────────────────────────────
    void drawHeader(int w, int h, const std::string& title,
                    const std::string& subtitle, int gameCount);
    void drawFooterHints(int w, int h,
                         const std::string& confirmLabel = "Launch",
                         const std::string& backLabel    = "Back",
                         const std::string& xLabel       = "",
                         const std::string& yLabel       = "");
    void drawScrollBar(int x, int y, int h, int total, int visible, int offset);

    // Draw a filled 5-point star centred at (cx, cy) with outer radius r
    void drawStar(int cx, int cy, int outerR, SDL_Color fill, SDL_Color outline);

    // Draw a bookmark shape (rectangle with triangular notch cut from bottom)
    // Top-left corner at (x, y), width bw, total height bh
    void drawBookmark(int x, int y, int bw, int bh, SDL_Color fill, SDL_Color outline);
    void drawLoadingSpinner(int cx, int cy, float angle, SDL_Color c);
    void drawButtonHint(int x, int y, const std::string& button,
                        const std::string& label, SDL_Color btnColor);

    // ── Accessors ─────────────────────────────────────────────────────────────
    const Palette& palette() const { return m_palette; }
    const Layout&  layout()  const { return m_layout; }
    Layout&        layout()        { return m_layout; }

    void onWindowResize(int w, int h, int gameCount = 0) { m_layout.recalculate(w, h, gameCount); }

private:
    TTF_Font* getFont(FontSize size, bool displayFont = false);
    SDL_Texture* renderTextToTexture(const std::string& text, TTF_Font* font, SDL_Color c);

    SDL_Renderer* m_renderer = nullptr;
    Palette       m_palette;
    Layout        m_layout;

    // UI font — clean, readable, used everywhere in menus/cards/settings
    std::unordered_map<int, TTF_Font*> m_fonts;
    std::string    m_fontPath;

    // Display font — Zrnic, used ONLY on the splash screen title
    std::unordered_map<int, TTF_Font*> m_displayFonts;
    std::string    m_displayFontPath;

    // Texture cache for frequently drawn strings
    struct CachedText {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };
    std::unordered_map<std::string, CachedText> m_textCache;
    void clearTextCache();
};
