#pragma once
// =============================================================================
//  omnisave_import.h
//  OmniSave Import Screen — Session 38
//
//  Presents a controller-navigable list of save blocks found in a dropped
//  .mcr (or .zip) file.  User selects a block → confirm dialog → importBlock().
//
//  Lifecycle:
//    1. OmniSaveVault detects a file in memcards/import/
//    2. Calls OmniSaveImport::open(path, activeCardPath)
//    3. Renders via render() each frame while isOpen()
//    4. handleInput() consumes controller events
//    5. When done, isOpen() returns false; caller checks needsVaultRefresh()
//       and moves the source file to import/done/
// =============================================================================

#include <string>
#include <vector>
#include <SDL2/SDL.h>
#include "memcard_manager.h"

// Forward declaration
class MemCardManager;

// ─────────────────────────────────────────────────────────────────────────────

class OmniSaveImport {
public:
    OmniSaveImport(MemCardManager* memcard,
                   SDL_Renderer*   renderer);
    ~OmniSaveImport() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Open the import screen for a given .mcr or .zip path.
     * activeCardPath is the destination card that imports will write into.
     * Parses the file immediately. Returns false if parsing fails.
     */
    bool open(const std::string& sourcePath,
              const std::string& activeCardPath);

    /** True while the screen is visible. */
    bool isOpen() const { return m_open; }

    /**
     * True after a successful import — tells OmniSaveVault to refresh its
     * left panel so the new save appears immediately.
     */
    bool needsVaultRefresh() const { return m_vaultRefreshNeeded; }

    // ── Per-frame ─────────────────────────────────────────────────────────────
    void update(float deltaMs);
    void render(SDL_Renderer* renderer, int screenW, int screenH);
    void handleInput(const SDL_Event& e);

private:
    // ── Rendering helpers ────────────────────────────────────────────────────
    void renderBackground(SDL_Renderer* r, int screenW, int screenH);
    void renderHeader    (SDL_Renderer* r, int panelX, int panelY, int panelW);
    void renderList      (SDL_Renderer* r, int panelX, int panelY,
                          int panelW, int panelH);
    void renderBlockRow  (SDL_Renderer* r, int rowX, int rowY, int rowW,
                          int rowH, int index, bool selected);
    void renderSprite    (SDL_Renderer* r, const MemCardManager::ImportBlock& blk,
                          int x, int y, int size);
    void renderFooter    (SDL_Renderer* r, int panelX, int panelY,
                          int panelW, int screenH);
    void renderConfirm   (SDL_Renderer* r, int screenW, int screenH);
    void renderErrorToast(SDL_Renderer* r, int screenW, int screenH);

    // ── Actions ──────────────────────────────────────────────────────────────
    void confirmImport();
    void cancelConfirm();
    void doImport(int blockIndex);
    void close();

    // ── Zip handling ─────────────────────────────────────────────────────────
    std::string resolveSourcePath(const std::string& sourcePath);
    void cleanupTempExtract();

    // ── Sprite animation ─────────────────────────────────────────────────────
    void updateSpriteAnimation(float dt);

    // ── Data ─────────────────────────────────────────────────────────────────
    MemCardManager* m_memcard  = nullptr;
    SDL_Renderer*   m_renderer = nullptr;

    bool m_open               = false;
    bool m_vaultRefreshNeeded = false;

    std::string m_sourcePath;       // resolved path (may be temp extract)
    std::string m_activeCardPath;   // destination card to import into
    std::string m_displayName;      // filename shown in header

    std::vector<MemCardManager::ImportBlock> m_blocks;
    bool m_parseOk = false;

    // Temp zip extract state
    bool        m_usedTempExtract = false;
    std::string m_tempExtractPath;

    // List navigation
    int  m_cursor        = 0;
    int  m_scrollOffset  = 0;
    static constexpr int VISIBLE_ROWS = 7;

    // Confirm dialog state
    bool m_confirmOpen = false;
    int  m_confirmSlot = -1;

    // Error toast
    std::string m_errorMsg;
    float       m_errorTimer = 0.f;
    static constexpr float ERROR_DISPLAY_SECS = 3.5f;

    // Success toast
    std::string m_successMsg;
    float       m_successTimer = 0.f;
    static constexpr float SUCCESS_DISPLAY_SECS = 2.0f;

    // Sprite animation
    float m_spriteTimer = 0.f;
    int   m_spriteFrame = 0;
    static constexpr float SPRITE_FRAME_INTERVAL = 0.25f;

    // Panel layout constants
    static constexpr int PANEL_W       = 640;
    static constexpr int HEADER_H      = 70;
    static constexpr int FOOTER_H      = 50;
    static constexpr int ROW_H         = 64;
    static constexpr int SPRITE_SIZE   = 48;
    static constexpr int PANEL_PADDING = 20;

    // Colours (matching OmniSave vault palette)
    static constexpr SDL_Color COL_BG          = {15,  15,  20,  230};
    static constexpr SDL_Color COL_BORDER       = {80, 140, 200, 255};
    static constexpr SDL_Color COL_ROW_SEL      = {30,  60,  90, 255};
    static constexpr SDL_Color COL_ROW_NORMAL   = {22,  22,  30, 200};
    static constexpr SDL_Color COL_TEXT_MAIN    = {220, 220, 220, 255};
    static constexpr SDL_Color COL_TEXT_DIM     = {120, 120, 130, 255};
    static constexpr SDL_Color COL_TEXT_TEAL    = { 80, 200, 180, 255};
    static constexpr SDL_Color COL_CONFIRM_BDR  = { 60, 200, 140, 255};
    static constexpr SDL_Color COL_EMPTY        = { 50,  50,  55, 180};
    static constexpr SDL_Color COL_ERROR_BG     = {140,  30,  30, 230};
    static constexpr SDL_Color COL_SUCCESS_BG   = { 30, 120,  60, 230};
};
