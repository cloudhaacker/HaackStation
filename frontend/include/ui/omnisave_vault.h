#pragma once
// omnisave_vault.h

#include "save_state_manager.h"
#include "memcard_manager.h"
#include "theme_engine.h"
#include "controller_nav.h"
#include "ui/omnisave_import.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>

// ─── PS1 memory card save entry ───────────────────────────────────────────────
struct MemCardEntry {
    std::string productCode;
    std::string identifier;
    std::string title;
    int         blocksUsed = 0;
    int         frameCount = 0;
    int         firstBlock = 0;
    std::array<std::vector<uint32_t>, 3> iconFrames;
    SDL_Texture* iconTextures[3] = { nullptr, nullptr, nullptr };
};

// ─── Card Chronicle snapshot entry ────────────────────────────────────────────
// One entry per .mcr file found in memcards/history/<SERIAL>_1/
// Sorted newest-first so index 0 is always the most recent snapshot.
struct TimeMachineEntry {
    std::string mcrPath;        // full path to the snapshot .mcr
    std::string pngPath;        // paired game frame screenshot (may not exist)
    std::string timestamp;      // full display string e.g. "May 08, 2026  21:04"
    std::string relativeAge;    // human label e.g. "Today", "3 days ago"
    std::string rawFilename;    // stem e.g. "2026-05-08_21-04-33" (for sorting)

    std::vector<MemCardEntry> entries;   // SpriteCard icons parsed from this snapshot
    SDL_Texture* thumbTex = nullptr;     // game frame thumbnail (may be nullptr)
};

// ─── Per-game card slot descriptor ────────────────────────────────────────────
// One entry per .mcr file found for the current game serial.
// Sorted by slot number (_1, _2, _3 …).
struct GameCardSlot {
    std::string path;        // absolute path to the .mcr file
    std::string displayName; // e.g. "Alundra — Card 1"
    int         slotNumber;  // 1-based slot suffix from filename
    bool        isActive;    // true = currently loaded in the core
};

enum class OmniSaveMode { BROWSE, SAVING, LOADING };
enum class OmniPanel    { MEMCARD, SAVESTATES, CHRONICLE };

enum class ConfirmAction {
    NONE,
    LOAD_STATE,
    DELETE_STATE,
    OVERWRITE_STATE,
    DELETE_ENTRY,
    RELOAD_CARD,
    RESTORE_SNAPSHOT,
    SWAP_CARD,          // hot-swap to a different card for this game
    COPY_STATE,         // branch / duplicate a save state slot
};

class OmniSaveVault {
public:
    OmniSaveVault(SDL_Renderer* renderer, ThemeEngine* theme,
                  ControllerNav* nav,
                  SaveStateManager* saveStates,
                  MemCardManager* memCards);
    ~OmniSaveVault();

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

    bool consumeSaveWritten() {
        if (m_saveWritten) { m_saveWritten = false; return true; }
        return false;
    }
    bool consumeCardReload() {
        if (m_wantsCardReload) { m_wantsCardReload = false; return true; }
        return false;
    }
	
	bool isImporting() const { 
	     return m_importPhase == ImportPhase::IMPORTING; 
	}

    // Returns the path of the card selected for hot-swap, then clears it.
    // Empty string means no swap is pending.
    std::string consumeCardSwap() {
        std::string path = m_pendingSwapPath;
        m_pendingSwapPath.clear();
        return path;
    }

    void setSramCallbacks(std::function<void()> flush,
                          std::function<void()> reload) {
        m_sramFlush  = std::move(flush);
        m_sramReload = std::move(reload);
    }

    // Set by app before open() so Time Machine restore and card swap know
    // which card is currently live. Also used to mark the active card in
    // the multi-card list.
    void setActiveCardPath(const std::string& path) { m_activeCardPath = path; }

private:
    // ── Import watcher ───────────────────────────────────────────────────────────
    enum class ImportPhase {
    NONE,       // nothing pending
    DETECTED,   // file found, showing banner
    IMPORTING,  // import screen is open
};

    ImportPhase              m_importPhase    = ImportPhase::NONE;
    std::string              m_importPending; // full path to detected file
    float                    m_importScanTimer = 0.f;
    static constexpr float   IMPORT_SCAN_INTERVAL = 1.0f;  // seconds between scans

    std::unique_ptr<OmniSaveImport> m_importScreen;

    // Banner animation
    float m_importBannerAlpha = 0.f;   // 0..255, fades in/out
    bool  m_importBannerVisible = false;

    // Private method declarations to add:
    void  scanImportFolder();
    void  openImportScreen();
    void  moveToImportDone(const std::string& srcPath);
    void  renderImportBanner(SDL_Renderer* r, int screenW, int screenH);
    void  handleImportInput(const SDL_Event& e);

    // ── Data loading ──────────────────────────────────────────────────────────
    void loadMemCardEntries();
    void loadGameCardSlots();      // populate m_gameCards from memcards/per_game/
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
    std::string formatRelativeAge(const std::string& stem);

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
    void doSwapCard();
    void doBranchState();

    // ── Core members ──────────────────────────────────────────────────────────
    SDL_Renderer*     m_renderer  = nullptr;
    ThemeEngine*      m_theme     = nullptr;
    ControllerNav*    m_nav       = nullptr;
    SaveStateManager* m_saves     = nullptr;
    MemCardManager*   m_memCards  = nullptr;

    std::string m_gameTitle;
    std::string m_gameSerial;
    std::string m_activeCardPath;
    OmniSaveMode m_mode  = OmniSaveMode::BROWSE;
    OmniPanel    m_focus = OmniPanel::SAVESTATES;

    // ── Memory card: save entries (from active card) ──────────────────────────
    std::vector<MemCardEntry>  m_cardEntries;
    int  m_cardSel    = 0;
    int  m_cardScroll = 0;

    // ── Memory card: per-game card slots (all .mcr files for this game) ───────
    // Shown as a compact card-switcher strip above the save entries list.
    std::vector<GameCardSlot>  m_gameCards;
    int  m_cardSlotSel    = 0;    // currently highlighted card in the switcher
    bool m_cardSlotFocus  = false; // true = D-pad navigates the slot strip

    // ── Save states ───────────────────────────────────────────────────────────
    std::vector<SaveSlot>       m_slots;
    std::vector<SDL_Texture*>   m_thumbTex;
    int  m_stateSel    = 0;
    int  m_stateScroll = 0;
    int  m_branchSourceSlot = -1;  // slotNumber of the slot being branched

    // ── Card Chronicle (formerly Time Machine) ────────────────────────────────
    std::vector<TimeMachineEntry> m_snapshots;
    int  m_snapSel    = 0;
    int  m_snapScroll = 0;

    // ── Confirm overlay ───────────────────────────────────────────────────────
    ConfirmAction m_confirmAction  = ConfirmAction::NONE;
    std::string   m_confirmMessage;
    std::string   m_confirmDetail;

    // Path of card selected for swap — set by confirm, consumed by app.cpp.
    std::string   m_pendingSwapPath;

    float  m_iconAnimMs = 0.f;
    int    m_iconFrame  = 0;
    static constexpr float ICON_FRAME_MS = 250.f;

    SDL_Surface* m_gameScreenshot = nullptr;

    bool m_wantsClose      = false;
    bool m_saveWritten     = false;
    bool m_wantsCardReload = false;

    std::function<void()> m_sramFlush;
    std::function<void()> m_sramReload;

    int m_w = 1280;
    int m_h = 720;

    static constexpr int HEADER_H      = 88;
    static constexpr int FOOTER_H      = 52;
    static constexpr int DIVIDER_X_PC  = 50;
    static constexpr int DIVIDER_W     = 2;
    static constexpr int MARGIN        = 20;
    static constexpr int CARD_ROW_H    = 72;
    static constexpr int ICON_SIZE     = 64;
    static constexpr int STATE_CARD_W  = 180;
    static constexpr int STATE_CARD_H  = 130;
    static constexpr int STATE_COLS    = 3;

    // Card slot switcher strip
    static constexpr int SLOT_STRIP_H  = 48;  // height of the card switcher area
    static constexpr int SLOT_PILL_W   = 140; // width of each card pill button
    static constexpr int SLOT_PILL_H   = 34;

    static constexpr int TM_ROW_H     = 100;
    static constexpr int TM_THUMB_W   = 128;
    static constexpr int TM_THUMB_H   = 72;
    static constexpr int TM_ICON_SIZE  = 32;
};
