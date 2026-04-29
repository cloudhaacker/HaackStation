#pragma once
// omnisave_vault.h
// OmniSave Vault — per-game save management screen.
//
// Split-panel layout:
//
//  ┌─────────────────────────────────────────────────────────────────────┐
//  │  💾  OMNISAVE                           Crash Bandicoot  [B] Back  │
//  │  ─────────────────────────────────────────────────────────────────  │
//  │                          │                                          │
//  │  MEMORY CARD             │  SAVE STATES                            │
//  │  MemoryCard1.mcr [Slot1] │                                         │
//  │                          │  ┌────────┐  ┌────────┐  ┌────────┐   │
//  │  ┌──────┐ Crash B…       │  │[thumb] │  │[thumb] │  │   +    │   │
//  │  │[anim]│ Jungle Roll    │  │ AUTO   │  │ Slot 1 │  │  New   │   │
//  │  │      │ 3 blocks       │  │ 2h ago │  │  Yesterday│        │   │
//  │  └──────┘                │  └────────┘  └────────┘  └────────┘   │
//  │  ┌──────┐ Crash B…       │                                         │
//  │  │[anim]│ Snow Go        │  ┌─────────────────────────────────┐   │
//  │  │      │ 1 block        │  │ Auto-save  •  Yesterday 21:04   │   │
//  │  └──────┘                │  │ [Load]  [Delete]                │   │
//  │                          │  └─────────────────────────────────┘   │
//  │  ─────────────────────── │  ─────────────────────────────────────  │
//  │  [A]View [X]Del          │  [A]Load  [□]Save Here  [X]Delete      │
//  └─────────────────────────────────────────────────────────────────────┘
//
// Panel focus:
//   L1 / R1  — switch focus between Memory Card panel (left) and Save States (right)
//   D-pad / Left stick — navigate within focused panel
//   A        — primary action (load state / view card entry)
//   Square(Y)— save new state (right panel only)
//   X(B-alt) — delete selected item
//   B        — back
//
// SpriteCard animated icons:
//   PS1 memory card saves embed up to 3 frames of 16x16 px, 4bpp animation
//   in the directory entry header. We parse these directly from the .mcr file
//   and animate at ~4fps (matching PS1 BIOS speed — snappy but readable).
//
// Opening modes:
//   BROWSE  — neutral, both panels navigable
//   SAVING  — right panel focused, prompts for slot selection
//   LOADING — right panel focused, loads on confirm

#include "save_state_manager.h"
#include "memcard_manager.h"
#include "theme_engine.h"
#include "controller_nav.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <array>

// ─── PS1 memory card save entry ───────────────────────────────────────────────
// Parsed from a .mcr directory block (128 bytes per entry, starting at byte 128)
struct MemCardEntry {
    std::string productCode;    // e.g. "SCUS-94900"
    std::string identifier;     // e.g. "BASCUS-94900CRASH1"
    std::string title;          // Shift-JIS decoded display title
    int         blocksUsed = 0; // Number of 8KB blocks this save occupies
    int         frameCount = 0; // 1–3 animation frames
    int         firstBlock = 0; // Index of first directory block

    // Icon pixel data — up to 3 frames, each 16x16 RGBA pixels
    // Decoded from 4bpp + 16-colour BGR555 palette at parse time
    std::array<std::vector<uint32_t>, 3> iconFrames; // RGBA8888
    SDL_Texture* iconTextures[3] = { nullptr, nullptr, nullptr };
};

enum class OmniSaveMode { BROWSE, SAVING, LOADING };
enum class OmniPanel    { MEMCARD, SAVESTATES };

class OmniSaveVault {
public:
    OmniSaveVault(SDL_Renderer* renderer, ThemeEngine* theme,
                  ControllerNav* nav,
                  SaveStateManager* saveStates,
                  MemCardManager* memCards);
    ~OmniSaveVault();

    // Call before opening — sets game context and reloads data
    void open(const std::string& gameTitle,
              const std::string& gameSerial,
              OmniSaveMode mode = OmniSaveMode::BROWSE);

    void handleEvent(const SDL_Event& e);
    void update(float deltaMs);
    void render();

    bool wantsClose()  const { return m_wantsClose; }
    void resetClose()        { m_wantsClose = false; }
    void onWindowResize(int w, int h) { m_w = w; m_h = h; }

    // Returns true and clears flag if a save was just written (so app can refresh)
    bool consumeSaveWritten() {
        if (m_saveWritten) { m_saveWritten = false; return true; }
        return false;
    }

private:
    // ── Data loading ──────────────────────────────────────────────────────────
    void loadMemCardEntries();
    void loadSaveSlots();
    void freeMemCardTextures();
    void freeSaveSlotTextures();

    // ── MCR parsing ───────────────────────────────────────────────────────────
    std::vector<MemCardEntry> parseMcr(const std::string& path);
    std::string decodeShiftJis(const uint8_t* data, int len);
    SDL_Texture* buildIconTexture(const std::vector<uint32_t>& rgba);

    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderHeader();
    void renderDivider(int x, int y, int h);
    void renderMemCardPanel(int x, int y, int w, int h);
    void renderSaveStatePanel(int x, int y, int w, int h);
    void renderFooter();

    // ── Navigation ────────────────────────────────────────────────────────────
    void handleMemCardNav(NavAction a);
    void handleSaveStateNav(NavAction a);
    void doSaveAction();    // write new save state to selected slot
    void doLoadAction();    // load selected save state
    void doDeleteState();   // delete selected save state
    void doDeleteEntry();   // delete selected memcard entry

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saves     = nullptr;
    MemCardManager*   m_memCards  = nullptr;

    std::string m_gameTitle;
    std::string m_gameSerial;
    OmniSaveMode m_mode   = OmniSaveMode::BROWSE;
    OmniPanel    m_focus  = OmniPanel::SAVESTATES;

    // Memory card panel state
    std::vector<MemCardEntry> m_cardEntries;
    int  m_cardSel     = 0;
    int  m_cardScroll  = 0;

    // Save state panel state
    std::vector<SaveSlot> m_slots;          // includes empty "new slot" sentinel at end
    std::vector<SDL_Texture*> m_thumbTex;   // parallel to m_slots (nullptr for empty)
    int  m_stateSel    = 0;
    int  m_stateScroll = 0;

    // SpriteCard animation
    float  m_iconAnimMs    = 0.f;   // accumulator
    int    m_iconFrame     = 0;     // current frame index (0–2)
    static constexpr float ICON_FRAME_MS = 250.f;  // ~4fps, matches PS1 BIOS

    bool m_wantsClose  = false;
    bool m_saveWritten = false;

    int m_w = 1280;
    int m_h = 720;

    // ── Layout ────────────────────────────────────────────────────────────────
    static constexpr int HEADER_H     = 88;
    static constexpr int FOOTER_H     = 52;
    static constexpr int DIVIDER_X_PC = 42;   // left panel = 42% of screen width
    static constexpr int DIVIDER_W    = 2;
    static constexpr int MARGIN       = 20;
    static constexpr int CARD_ROW_H   = 72;   // memcard entry row height
    static constexpr int ICON_SIZE    = 48;   // display size of SpriteCard icon
    static constexpr int STATE_CARD_W = 180;  // save state thumbnail card width
    static constexpr int STATE_CARD_H = 130;  // save state thumbnail card height
    static constexpr int STATE_COLS   = 3;    // thumbnail columns in right panel
};
