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
    // Game shelf grid
    int cardW         = 160;   // Game card width
    int cardH         = 220;   // Game card height (taller than wide, like a box)
    int cardPadX      = 18;    // Horizontal gap between cards
    int cardPadY      = 24;    // Vertical gap between cards
    int shelfPadLeft  = 48;    // Left margin of the shelf
    int shelfPadTop   = 120;   // Top margin (below header bar)

    // Header bar
    int headerH       = 80;    // Height of top bar

    // Footer hint bar
    int footerH       = 48;    // Height of bottom button hint bar

    // Cards per row (recalculated on window resize)
    int cardsPerRow   = 6;

    void recalculate(int windowW, int windowH) {
        // How many cards fit across with padding
        int available = windowW - shelfPadLeft * 2;
        cardsPerRow = std::max(2, available / (cardW + cardPadX));
        (void)windowH;
    }
};

// ─── Font sizes ───────────────────────────────────────────────────────────────
enum class FontSize {
    TINY   = 11,
    SMALL  = 14,
    BODY   = 18,
    TITLE  = 24,
    HEADER = 32,
    HERO   = 48,
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
                      float selectionAnim = 1.f);

    // ── UI components ─────────────────────────────────────────────────────────
    void drawHeader(int w, int h, const std::string& title,
                    const std::string& subtitle, int gameCount);
    void drawFooterHints(int w, int h,
                         const std::string& confirmLabel = "Launch",
                         const std::string& optionsLabel = "Options");
    void drawScrollBar(int x, int y, int h, int total, int visible, int offset);
    void drawLoadingSpinner(int cx, int cy, float angle, SDL_Color c);
    void drawButtonHint(int x, int y, const std::string& button,
                        const std::string& label, SDL_Color btnColor);

    // ── Accessors ─────────────────────────────────────────────────────────────
    const Palette& palette() const { return m_palette; }
    const Layout&  layout()  const { return m_layout; }
    Layout&        layout()        { return m_layout; }

    void onWindowResize(int w, int h) { m_layout.recalculate(w, h); }

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
