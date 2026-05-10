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
//  │  [A]Reload [Y]History    │  [A]Load  [□]Save Here  [X]Delete      │
//  │  [X]Delete               │                                         │
//  └─────────────────────────────────────────────────────────────────────┘
//
// Panel focus:
//   L1 / R1  — switch focus between Memory Card panel (left) and Save States (right)
//   D-pad / Left stick — navigate within focused panel
//   A        — primary action (load state / reload card)
//   Y        — open Card Time Machine history (from memory card panel)
//   Square   — save new state (right panel only)
//   X        — delete selected item
//   B        — back / close Time Machine panel
//
// Card Time Machine (full-screen overlay, entered via Y from memcard panel):
//   Shows up to CARD_HISTORY_MAX timestamped .mcr snapshots from
//   memcards/history/<SERIAL>_1/. Each row shows timestamp, game frame
//   thumbnail, and SpriteCard mini-icons parsed from that snapshot.
//   A = restore (confirm → safety snapshot current card → atomic copy → sramReload)
//   B = back to memory card panel
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
#include <functional>

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

// ─── Card Time Machine snapshot entry ─────────────────────────────────────────
// One entry per .mcr file found in memcards/history/<SERIAL>_1/
// Snapshots are loaded newest-first so index 0 is always the most recent.
struct TimeMachineEntry {
    std::string mcrPath;        // full path to the snapshot .mcr
    std::string pngPath;        // paired game frame screenshot (may not exist)
    std::string timestamp;      // display string, e.g. "May 08  21:04"
    std::string rawFilename;    // stem e.g. "2026-05-08_21-04-33" (for sorting)

    // SpriteCard icons parsed from this snapshot (all entries, for mini-icon strip)
    std::vector<MemCardEntry> entries;
    SDL_Texture* thumbTex = nullptr;    // game frame thumbnail texture (may be nullptr)
};

enum class OmniSaveMode { BROWSE, SAVING, LOADING };
enum class OmniPanel    { MEMCARD, SAVESTATES, TIMEMACHINE };

// ─── Confirm dialog action types ──────────────────────────────────────────────
enum class ConfirmAction {
    NONE,
    LOAD_STATE,       // Load a save state (replaces live game state)
    DELETE_STATE,     // Delete a save state file permanently
    OVERWRITE_STATE,  // Save to a slot that already has data (replaces it)
    DELETE_ENTRY,     // Delete a PS1 memory card save entry (rewrites .mcr)
    RELOAD_CARD,      // Reload memory card from disk (discards in-RAM changes)
    RESTORE_SNAPSHOT, // Restore a Time Machine snapshot as the active card
};

class OmniSaveVault {
public:
    OmniSaveVault(SDL_Renderer* renderer, ThemeEngine* theme,
                  ControllerNav* nav,
                  SaveStateManager* saveStates,
                  MemCardManager* memCards);
    ~OmniSaveVault();

    // Call before opening — sets game context and reloads data.
    // Pass a pre-captured game screenshot so the save thumbnail shows the
    // game frame rather than the OmniSave UI.  Ownership transfers here;
    // OmniSaveVault will free it when overwritten or on destruction.
    void open(const std::string& gameTitle,
              const std::string& gameSerial,
              OmniSaveMode mode = OmniSaveMode::BROWSE,
              SDL_Surface* gameScreenshot = nullptr);

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

    // Returns true and clears flag if the user confirmed a card reload.
    // App should call flushSaveRAM → loadSaveRAM on its active card path.
    bool consumeCardReload() {
        if (m_wantsCardReload) { m_wantsCardReload = false; return true; }
        return false;
    }

    // Callbacks for SRAM protection during state loads.
    // App sets these before open() so the vault can flush/reload the card
    // independently of the core pointer.
    void setSramCallbacks(std::function<void()> flush,
                          std::function<void()> reload) {
        m_sramFlush  = std::move(flush);
        m_sramReload = std::move(reload);
    }

    // Active card path — set by app before open() so Time Machine restore
    // knows where the live card lives for the pre-restore safety snapshot.
    void setActiveCardPath(const std::string& path) { m_activeCardPath = path; }

private:
    // ── Data loading ──────────────────────────────────────────────────────────
    void loadMemCardEntries();
    void loadSaveSlots();
    void loadTimeMachineEntries();
    void freeMemCardTextures();
    void freeSaveSlotTextures();
    void freeTimeMachineTextures();

    // ── MCR parsing ───────────────────────────────────────────────────────────
    std::vector<MemCardEntry> parseMcr(const std::string& path);
    std::string decodeShiftJis(const uint8_t* data, int len);
    SDL_Texture* buildIconTexture(const std::vector<uint32_t>& rgba);
    std::string formatSnapshotTimestamp(const std::string& stem);

    // ── Rendering ─────────────────────────────────────────────────────────────
    void renderHeader();
    void renderDivider(int x, int y, int h);
    void renderMemCardPanel(int x, int y, int w, int h);
    void renderSaveStatePanel(int x, int y, int w, int h);
    void renderTimeMachinePanel(int x, int y, int w, int h);
    void renderFooter();
    void renderConfirmOverlay();

    // ── Navigation ────────────────────────────────────────────────────────────
    void handleMemCardNav(NavAction a);
    void handleSaveStateNav(NavAction a);
    void handleTimeMachineNav(NavAction a);
    void doSaveAction();
    void doLoadAction();
    void doDeleteState();
    void doDeleteEntry();
    void doRestoreSnapshot();

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saves     = nullptr;
    MemCardManager*   m_memCards  = nullptr;

    std::string m_gameTitle;
    std::string m_gameSerial;
    std::string m_activeCardPath;   // live card path, set by app before open()
    OmniSaveMode m_mode   = OmniSaveMode::BROWSE;
    OmniPanel    m_focus  = OmniPanel::SAVESTATES;

    // Memory card panel state
    std::vector<MemCardEntry> m_cardEntries;
    int  m_cardSel     = 0;
    int  m_cardScroll  = 0;

    // Save state panel state
    std::vector<SaveSlot> m_slots;
    std::vector<SDL_Texture*> m_thumbTex;
    int  m_stateSel    = 0;
    int  m_stateScroll = 0;

    // ── Time Machine panel state ──────────────────────────────────────────────
    std::vector<TimeMachineEntry> m_snapshots;  // sorted newest-first
    int  m_snapSel     = 0;
    int  m_snapScroll  = 0;

    // ── Confirm dialog state ──────────────────────────────────────────────────
    ConfirmAction m_confirmAction  = ConfirmAction::NONE;
    std::string   m_confirmMessage;
    std::string   m_confirmDetail;

    // SpriteCard animation
    float  m_iconAnimMs    = 0.f;
    int    m_iconFrame     = 0;
    static constexpr float ICON_FRAME_MS = 250.f;

    SDL_Surface* m_gameScreenshot = nullptr;

    bool m_wantsClose      = false;
    bool m_saveWritten     = false;
    bool m_wantsCardReload = false;

    std::function<void()> m_sramFlush;
    std::function<void()> m_sramReload;

    int m_w = 1280;
    int m_h = 720;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int HEADER_H     = 88;
    static constexpr int FOOTER_H     = 52;
    static constexpr int DIVIDER_X_PC = 50;
    static constexpr int DIVIDER_W    = 2;
    static constexpr int MARGIN       = 20;
    static constexpr int CARD_ROW_H   = 72;
    static constexpr int ICON_SIZE    = 64;
    static constexpr int STATE_CARD_W = 180;
    static constexpr int STATE_CARD_H = 130;
    static constexpr int STATE_COLS   = 3;

    // Time Machine layout
    static constexpr int TM_ROW_H     = 100;
    static constexpr int TM_THUMB_W   = 128;
    static constexpr int TM_THUMB_H   = 72;
    static constexpr int TM_ICON_SIZE = 32;
};
