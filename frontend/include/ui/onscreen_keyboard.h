#pragma once
// onscreen_keyboard.h
// Modal on-screen keyboard — controller-navigable QWERTY overlay.
//
// Usage:
//   m_osk->open("Enter Username", 32, false, [this](bool ok, const std::string& text) {
//       if (ok) handleUsername(text);
//   });
//   // In your screen's handleEvent / render / update, forward to OSK while isOpen():
//   if (m_osk->isOpen()) { m_osk->handleEvent(e); return; }
//   if (m_osk->isOpen()) { m_osk->update(dt); m_osk->render(); return; }
//
// Layout (QWERTY, 5 rows):
//   Row 0 : 1 2 3 4 5 6 7 8 9 0  -  =
//   Row 1 : Q W E R T Y U I O P
//   Row 2 : A S D F G H J K L
//   Row 3 : Z X C V B N M  ,  .  /
//   Row 4 : [SHIFT]  [SPACE──────]  [⌫]  [OK]
//
// Symbols layer (Shift held):
//   Row 0 : ! @ # $ % ^ & * ( )  _  +
//   Row 3 includes  <  >  ?  symbols
//
// Caller decides whether input is masked (password fields show ●).
// Max length is enforced — input is silently clamped.
//
// Asset dependency:
//   assets/osk/cross.png      — PS Cross  (A = confirm key)
//   assets/osk/circle.png     — PS Circle (B = back/close)
//   assets/osk/square.png     — PS Square (X = backspace)
//   assets/osk/dpad.png       — D-pad hint
//   These are the Kenney PlayStation Series Default PNGs copied at build time.
//   Graceful fallback: text labels render if textures fail to load.

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <functional>
#include <string>
#include <vector>
#include <array>

#include "theme_engine.h"
#include "controller_nav.h"

// ─── Result callback ──────────────────────────────────────────────────────────
// Called when the user confirms (ok=true, text=entered string)
// or cancels (ok=false, text="").
using OskCallback = std::function<void(bool ok, const std::string& text)>;

// ─── Key descriptor ───────────────────────────────────────────────────────────
struct OskKey {
    std::string label;          // Displayed label (normal layer)
    std::string shiftLabel;     // Displayed label (shift layer)
    std::string value;          // Character inserted (normal)
    std::string shiftValue;     // Character inserted (shifted)
    float       widthScale = 1.0f;  // Relative key width (1.0 = standard)
    bool        isAction   = false; // True for Shift/Backspace/OK/Space

    // Convenience constructor for standard character keys
    static OskKey Char(const std::string& lower, const std::string& upper) {
        OskKey k;
        k.label      = upper;   // always show uppercase label
        k.shiftLabel = upper;
        k.value      = lower;
        k.shiftValue = upper;
        return k;
    }
    static OskKey Symbol(const std::string& normal, const std::string& shifted) {
        OskKey k;
        k.label      = normal;
        k.shiftLabel = shifted;
        k.value      = normal;
        k.shiftValue = shifted;
        return k;
    }
    static OskKey Action(const std::string& lbl, float wScale = 1.0f) {
        OskKey k;
        k.label      = lbl;
        k.shiftLabel = lbl;
        k.value      = "";
        k.shiftValue = "";
        k.widthScale = wScale;
        k.isAction   = true;
        return k;
    }
};

// ─── OnScreenKeyboard ─────────────────────────────────────────────────────────
class OnScreenKeyboard {
public:
    OnScreenKeyboard(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav);
    ~OnScreenKeyboard();

    // Open the keyboard.
    //   prompt   — label shown above the text field ("Enter Username")
    //   maxLen   — maximum characters allowed (0 = unlimited)
    //   masked   — true for password fields (shows ● instead of characters)
    //   callback — called on confirm or cancel
    void open(const std::string& prompt, int maxLen, bool masked, OskCallback callback);

    // Close without firing callback (e.g. parent screen is destroyed)
    void close();

    bool isOpen()     const { return m_open; }
    bool wantsClose() const { return m_wantsClose; }

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    // Pre-populate the text field (e.g. editing an existing value)
    void setText(const std::string& text);

private:
    // ── Layout ────────────────────────────────────────────────────────────────
    void buildLayout();

    // Number of rows
    static constexpr int NUM_ROWS = 5;

    // Row definitions — filled in buildLayout()
    std::array<std::vector<OskKey>, NUM_ROWS> m_rows;

    // ── Navigation state ──────────────────────────────────────────────────────
    int  m_row      = 0;
    int  m_col      = 0;
    bool m_shifted  = false;
    bool m_capsLock = false;

    void clampSelection();
    void navigate(NavAction a);
    void navigateWithClamp(NavAction a);  // navigate + cancelHeld at edges
    void activateSelected();

    // ── Text state ────────────────────────────────────────────────────────────
    std::string m_prompt;
    std::string m_text;
    int         m_maxLen  = 0;
    bool        m_masked  = false;
    OskCallback m_callback;
    bool        m_open       = false;
    bool        m_wantsClose = false;

    // Cursor blink
    Uint32 m_cursorBlinkMs = 0;
    bool   m_cursorVisible = true;

    // Physical keyboard flash — briefly highlights the matching OSK key when
    // the user types via a real keyboard so the visual stays connected.
    int m_flashRow = -1;
    int m_flashCol = -1;
    int m_flashMs  = 0;   // countdown in ms; 0 = no flash active

    // True while the user is typing via physical keyboard.
    // Controller selection highlight is suppressed until d-pad is used again.
    bool m_physicalKeyboardActive = false;

    // ── Rendering ─────────────────────────────────────────────────────────────
    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;
    ControllerNav* m_nav     = nullptr;

    int m_w = 0, m_h = 0;  // screen dimensions (refreshed each render)

    // Layout constants — computed in render() based on screen size
    static constexpr int TEXT_FIELD_H   = 72;
    static constexpr int FOOTER_H       = 52;
    static constexpr int KEY_PAD        = 5;    // gap between keys
    static constexpr int KEY_H          = 54;   // key height
    static constexpr int OVERLAY_MARGIN = 40;   // outer margin of the overlay panel

    // Compute key rects for a given row, returning vector of SDL_Rect
    // x0, y0 = top-left of the row area; rowW = available row width
    struct KeyRect { SDL_Rect rect; int keyIdx; };
    std::vector<KeyRect> computeRowRects(int rowIdx, int x0, int y0, int rowW) const;

    // Compute the full overlay bounds
    SDL_Rect overlayRect() const;

    // Draw helpers
    void renderOverlayBg(const SDL_Rect& ov);
    void renderTextField(int x, int y, int w);
    void renderKeyboard(int x, int y, int w, int h);
    void renderFooter(const SDL_Rect& ov);
    void renderKey(const SDL_Rect& r, const OskKey& key, bool selected, bool flashed = false);
    void flashKeyForChar(char c);

    // Button hint textures (loaded lazily from assets/osk/)
    SDL_Texture* m_texCross   = nullptr;
    SDL_Texture* m_texCircle  = nullptr;
    SDL_Texture* m_texSquare  = nullptr;
    SDL_Texture* m_texDpad    = nullptr;
    bool         m_assetsLoaded = false;

    void loadAssets();
    void freeAssets();
    void drawButtonIcon(SDL_Texture* tex, const std::string& fallback,
                        int x, int y, int size, SDL_Color col);
};
