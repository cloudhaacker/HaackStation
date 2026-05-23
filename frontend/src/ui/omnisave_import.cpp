// =============================================================================
//  omnisave_import.cpp
//  OmniSave Import Screen — Session 38 (corrected)
// =============================================================================

#include "ui/omnisave_import.h"
#include "memcard_manager.h"
// Use miniz bundled with libchdr
#include "miniz.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Internal drawing helpers
//  (self-contained — no font members, draws coloured rects and simple text
//   via SDL_Renderer only; text is rendered as solid coloured blocks for now
//   since OmniSaveImport has no access to ThemeEngine or TTF fonts)
// ─────────────────────────────────────────────────────────────────────────────

static void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    setColor(r, c);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void drawOutline(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, int thickness = 1) {
    setColor(r, c);
    for (int t = 0; t < thickness; ++t) {
        SDL_Rect rect{x + t, y + t, w - 2*t, h - 2*t};
        SDL_RenderDrawRect(r, &rect);
    }
}

// Minimal bitmap font — draws each character as a small pixel grid.
// Only covers printable ASCII. Each char is 4x7 pixels, scaled by `scale`.
static const uint8_t FONT4x7[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // '!'
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
    {0x06,0x04,0x08,0x00,0x00,0x00,0x00}, // '\''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x06,0x04,0x08}, // ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x06,0x06}, // '.'
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, // '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // '2'
    {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, // '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
    {0x00,0x06,0x06,0x00,0x06,0x06,0x00}, // ':'
    {0x00,0x06,0x06,0x00,0x06,0x04,0x08}, // ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
    {0x0E,0x11,0x02,0x04,0x04,0x00,0x04}, // '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0F}, // '@'
    {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11}, // 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, // '\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 'a'
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 'b'
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, // 'c'
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 'd'
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 'e'
    {0x06,0x08,0x1E,0x08,0x08,0x08,0x08}, // 'f'
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01}, // 'g' (no descender)
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 'h'
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 'i'
    {0x02,0x00,0x06,0x02,0x02,0x02,0x0C}, // 'j'
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 'k'
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'l'
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, // 'm'
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 'n'
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 'o'
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10}, // 'p' (no descender)
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01}, // 'q' (no descender)
    {0x00,0x00,0x16,0x18,0x10,0x10,0x10}, // 'r'
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 's'
    {0x08,0x08,0x1E,0x08,0x08,0x08,0x06}, // 't'
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, // 'u'
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 'v'
    {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, // 'w'
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 'x'
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 'y' (no descender)
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 'z'
    {0x06,0x04,0x08,0x10,0x08,0x04,0x06}, // '{'
    {0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // '|'
    {0x0C,0x04,0x02,0x01,0x02,0x04,0x0C}, // '}'
    {0x00,0x08,0x15,0x02,0x00,0x00,0x00}, // '~'
};

static void drawText(SDL_Renderer* r, const std::string& text,
                     int x, int y, SDL_Color col, int scale = 1)
{
    int cx = x;
    for (char ch : text) {
        if (ch < 32 || ch > 126) { cx += (4 + 1) * scale; continue; }
        const uint8_t* glyph = FONT4x7[ch - 32];
        for (int row = 0; row < 7; ++row) {
            for (int bit = 3; bit >= 0; --bit) {
                if (glyph[row] & (1 << bit)) {
                    SDL_Rect px{ cx + (3 - bit) * scale,
                                 y  + row * scale,
                                 scale, scale };
                    setColor(r, col);
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
        cx += (4 + 1) * scale;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Utility
// ─────────────────────────────────────────────────────────────────────────────

static std::string baseName(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

static bool endsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    std::string e = s.substr(s.size() - suffix.size());
    std::string sf = suffix;
    std::transform(e.begin(),  e.end(),  e.begin(),  ::tolower);
    std::transform(sf.begin(), sf.end(), sf.begin(), ::tolower);
    return e == sf;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

OmniSaveImport::OmniSaveImport(MemCardManager* memcard, SDL_Renderer* renderer)
    : m_memcard(memcard), m_renderer(renderer)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  open()
// ─────────────────────────────────────────────────────────────────────────────

bool OmniSaveImport::open(const std::string& sourcePath,
                           const std::string& activeCardPath)
{
    m_vaultRefreshNeeded = false;
    m_confirmOpen        = false;
    m_confirmSlot        = -1;
    m_cursor             = 0;
    m_scrollOffset       = 0;
    m_errorMsg.clear();
    m_successMsg.clear();
    m_errorTimer   = 0.f;
    m_successTimer = 0.f;
    m_blocks.clear();
    m_activeCardPath = activeCardPath;
    m_displayName    = baseName(sourcePath);

    std::string resolved = resolveSourcePath(sourcePath);
    if (resolved.empty()) return false;
    m_sourcePath = resolved;

    m_parseOk = m_memcard->loadCardForImport(m_sourcePath, m_blocks);
    if (!m_parseOk) { cleanupTempExtract(); return false; }

    // Advance cursor to first non-empty slot
    for (int i = 0; i < (int)m_blocks.size(); ++i)
        if (!m_blocks[i].isEmpty) { m_cursor = i; break; }

    m_open = true;
    SDL_Log("[OmniSaveImport] Opened: %s (%d blocks)", m_displayName.c_str(), (int)m_blocks.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  resolveSourcePath
//  Handles: .mcr  .mcd  .mem  .vgs  .srm  (raw MCR data, use directly)
//           .gme  (DexDrive — strip 3904-byte header, write temp file)
//           .zip  (extract first recognised save file, needs real miniz.h)
// ─────────────────────────────────────────────────────────────────────────────

std::string OmniSaveImport::resolveSourcePath(const std::string& sourcePath)
{
    // Raw MCR-compatible formats — use directly
    if (endsWithCI(sourcePath, ".mcr") ||
        endsWithCI(sourcePath, ".mcd") ||
        endsWithCI(sourcePath, ".mem") ||
        endsWithCI(sourcePath, ".vgs") ||
        endsWithCI(sourcePath, ".srm"))
    {
        return sourcePath;
    }

    // DexDrive .gme — 3904-byte proprietary header, then standard 128KB MCR
    if (endsWithCI(sourcePath, ".gme"))
    {
        static constexpr int GME_HEADER = 3904;
        static constexpr int MCR_SIZE   = 131072;

        std::ifstream f(sourcePath, std::ios::binary);
        if (!f.is_open()) { m_errorMsg = "Could not open .gme file."; return ""; }

        f.seekg(0, std::ios::end);
        auto sz = f.tellg();
        if (sz < GME_HEADER + MCR_SIZE) {
            m_errorMsg = "File is too small to be a valid .gme file.";
            return "";
        }

        std::vector<uint8_t> data(MCR_SIZE);
        f.seekg(GME_HEADER);
        f.read(reinterpret_cast<char*>(data.data()), MCR_SIZE);
        f.close();

        std::string tempPath = sourcePath + ".tmp_gme.mcr";
        std::ofstream out(tempPath, std::ios::binary);
        if (!out.is_open()) { m_errorMsg = "Could not write temp file."; return ""; }
        out.write(reinterpret_cast<const char*>(data.data()), MCR_SIZE);
        out.close();

        m_usedTempExtract = true;
        m_tempExtractPath = tempPath;
        SDL_Log("[OmniSaveImport] Stripped GME header, temp: %s", tempPath.c_str());
        return tempPath;
    }

    // Zip support — disabled until miniz linking is sorted out
    if (endsWithCI(sourcePath, ".zip"))
    {
        m_errorMsg = "Zip import coming soon. Extract the .mcr or .gme and drop that in.";
        return "";
    }

    m_errorMsg = "Unsupported file type. Supported: .mcr .gme .mcd .mem .vgs .srm .zip";
    return "";
}

void OmniSaveImport::cleanupTempExtract() {
    if (m_usedTempExtract && !m_tempExtractPath.empty()) {
        std::remove(m_tempExtractPath.c_str());
        m_usedTempExtract = false;
        m_tempExtractPath.clear();
    }
}

void OmniSaveImport::close() {
    cleanupTempExtract();
    m_open = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  update()
// ─────────────────────────────────────────────────────────────────────────────

void OmniSaveImport::update(float deltaMs) {
    if (!m_open) return;
    updateSpriteAnimation(deltaMs);
    if (m_errorTimer   > 0.f) m_errorTimer   -= deltaMs / 1000.f;
    if (m_successTimer > 0.f) m_successTimer -= deltaMs / 1000.f;
}

void OmniSaveImport::updateSpriteAnimation(float /*dt*/) {
    // Sprites not yet implemented — placeholder for future
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleInput()
// ─────────────────────────────────────────────────────────────────────────────

void OmniSaveImport::handleInput(const SDL_Event& e) {
    if (!m_open) return;

    if (m_confirmOpen) {
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) confirmImport();
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) cancelConfirm();
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) confirmImport();
            if (e.key.keysym.sym == SDLK_ESCAPE) cancelConfirm();
        }
        return;
    }

    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (m_cursor > 0) { m_cursor--;
                    if (m_cursor < m_scrollOffset) m_scrollOffset = m_cursor; }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (m_cursor < (int)m_blocks.size()-1) { m_cursor++;
                    if (m_cursor >= m_scrollOffset + VISIBLE_ROWS)
                        m_scrollOffset = m_cursor - VISIBLE_ROWS + 1; }
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if (m_cursor >= 0 && m_cursor < (int)m_blocks.size()
                    && !m_blocks[m_cursor].isEmpty
                    && !m_blocks[m_cursor].isCorrupted)
                { m_confirmSlot = m_cursor; m_confirmOpen = true; }
                break;
            case SDL_CONTROLLER_BUTTON_B: close(); break;
            default: break;
        }
    } else if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_UP:
                if (m_cursor > 0) { m_cursor--;
                    if (m_cursor < m_scrollOffset) m_scrollOffset = m_cursor; }
                break;
            case SDLK_DOWN:
                if (m_cursor < (int)m_blocks.size()-1) { m_cursor++;
                    if (m_cursor >= m_scrollOffset + VISIBLE_ROWS)
                        m_scrollOffset = m_cursor - VISIBLE_ROWS + 1; }
                break;
            case SDLK_RETURN:
                if (m_cursor >= 0 && m_cursor < (int)m_blocks.size()
                    && !m_blocks[m_cursor].isEmpty
                    && !m_blocks[m_cursor].isCorrupted)
                { m_confirmSlot = m_cursor; m_confirmOpen = true; }
                break;
            case SDLK_ESCAPE: close(); break;
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Actions
// ─────────────────────────────────────────────────────────────────────────────

void OmniSaveImport::confirmImport()  { m_confirmOpen = false; doImport(m_confirmSlot); m_confirmSlot = -1; }
void OmniSaveImport::cancelConfirm() { m_confirmOpen = false; m_confirmSlot = -1; }

void OmniSaveImport::doImport(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= (int)m_blocks.size()) return;
    std::string error;
    bool ok = m_memcard->importBlock(m_sourcePath, blockIndex, m_activeCardPath, error);
    if (ok) {
        const auto& blk = m_blocks[blockIndex];
        std::string name = blk.title.empty() ? blk.serial : blk.title;
        m_successMsg   = "Imported: " + name;
        m_successTimer = SUCCESS_DISPLAY_SECS;
        m_vaultRefreshNeeded    = true;
        m_blocks[blockIndex].isEmpty = true; // grey it out so it can't be imported twice
        SDL_Log("[OmniSaveImport] Import OK: slot %d (%s)", blockIndex, name.c_str());
    } else {
        m_errorMsg   = error.empty() ? "Import failed." : error;
        m_errorTimer = ERROR_DISPLAY_SECS;
        SDL_Log("[OmniSaveImport] Import FAILED: slot %d — %s", blockIndex, error.c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  render()
// ─────────────────────────────────────────────────────────────────────────────

void OmniSaveImport::render(SDL_Renderer* r, int screenW, int screenH) {
    if (!m_open) return;

    renderBackground(r, screenW, screenH);

    int panelX = (screenW - PANEL_W) / 2;
    int listH  = VISIBLE_ROWS * ROW_H;
    int panelH = HEADER_H + listH + FOOTER_H + PANEL_PADDING * 2;
    int panelY = (screenH - panelH) / 2;

    fillRect(r, panelX, panelY, PANEL_W, panelH, COL_BG);
    drawOutline(r, panelX, panelY, PANEL_W, panelH, COL_BORDER, 2);

    renderHeader(r, panelX, panelY, PANEL_W);
    renderList  (r, panelX, panelY + HEADER_H, PANEL_W, listH);
    renderFooter(r, panelX, panelY + HEADER_H + listH + PANEL_PADDING, PANEL_W, screenH);

    if (m_confirmOpen) renderConfirm(r, screenW, screenH);

    // Error toast
    if (m_errorTimer > 0.f) renderErrorToast(r, screenW, screenH);

    // Success toast
    if (m_successTimer > 0.f) {
        int tw = PANEL_W - 40, tx = (screenW - tw) / 2;
        int panelBottom = panelY + panelH;
        fillRect(r, tx, panelBottom + 12, tw, 34, COL_SUCCESS_BG);
        drawOutline(r, tx, panelBottom + 12, tw, 34, {60, 220, 120, 255});
        drawText(r, m_successMsg, tx + 10, panelBottom + 20, COL_TEXT_MAIN, 1);
    }
}

void OmniSaveImport::renderBackground(SDL_Renderer* r, int screenW, int screenH) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_Rect full{0, 0, screenW, screenH};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void OmniSaveImport::renderHeader(SDL_Renderer* r, int panelX, int panelY, int panelW) {
    drawText(r, "IMPORT SAVES", panelX + PANEL_PADDING, panelY + 14, COL_TEXT_TEAL, 2);
    drawText(r, m_displayName,  panelX + PANEL_PADDING, panelY + 44, COL_TEXT_DIM,  1);

    setColor(r, {80, 140, 200, 80});
    SDL_RenderDrawLine(r, panelX + PANEL_PADDING, panelY + HEADER_H - 1,
                          panelX + panelW - PANEL_PADDING, panelY + HEADER_H - 1);
}

void OmniSaveImport::renderList(SDL_Renderer* r, int panelX, int panelY,
                                 int panelW, int /*panelH*/)
{
    for (int vi = 0; vi < VISIBLE_ROWS; ++vi) {
        int i = m_scrollOffset + vi;
        if (i >= (int)m_blocks.size()) break;
        renderBlockRow(r, panelX, panelY + vi * ROW_H, panelW, ROW_H, i, i == m_cursor);
    }
}

void OmniSaveImport::renderBlockRow(SDL_Renderer* r, int rowX, int rowY,
                                     int rowW, int rowH, int index, bool selected)
{
    const MemCardManager::ImportBlock& blk = m_blocks[index];
    bool importable = !blk.isEmpty && !blk.isCorrupted;

    SDL_Color rowBg = selected ? COL_ROW_SEL : COL_ROW_NORMAL;
    fillRect(r, rowX, rowY, rowW, rowH, rowBg);
    if (selected) fillRect(r, rowX, rowY, 3, rowH, COL_BORDER);

    setColor(r, {40, 40, 50, 180});
    SDL_RenderDrawLine(r, rowX, rowY + rowH - 1, rowX + rowW, rowY + rowH - 1);

    int contentX = rowX + PANEL_PADDING;
    int centreY  = rowY + rowH / 2;

    // Sprite placeholder box
    SDL_Color phCol = blk.isEmpty      ? COL_EMPTY
                    : blk.isCorrupted  ? SDL_Color{80, 40, 40, 180}
                    :                    SDL_Color{40, 80, 120, 200};
    fillRect(r, contentX, centreY - SPRITE_SIZE / 2, SPRITE_SIZE, SPRITE_SIZE, phCol);
    if (!blk.isEmpty && !blk.isCorrupted)
        drawText(r, "MCR", contentX + 4, centreY - 4, COL_TEXT_DIM, 1);

    int textX = contentX + SPRITE_SIZE + 12;

    if (blk.isEmpty) {
        drawText(r, "(empty)", textX, centreY - 4, COL_TEXT_DIM, 1);
        return;
    }
    if (blk.isCorrupted) {
        drawText(r, "CORRUPTED", textX, centreY - 4, {200, 100, 80, 255}, 1);
        return;
    }

    std::string title = blk.title.empty() ? blk.serial : blk.title;
    drawText(r, title, textX, centreY - 10, COL_TEXT_MAIN, 2);

    std::string sub = blk.serial + "  "
                    + std::to_string(blk.blocksUsed)
                    + (blk.blocksUsed == 1 ? " block" : " blocks");
    drawText(r, sub, textX, centreY + 8, COL_TEXT_DIM, 1);
}

void OmniSaveImport::renderSprite(SDL_Renderer* /*r*/,
                                   const MemCardManager::ImportBlock& /*blk*/,
                                   int /*x*/, int /*y*/, int /*size*/)
{
    // Sprite decoding not yet implemented — renderBlockRow uses a placeholder box.
}

void OmniSaveImport::renderFooter(SDL_Renderer* r, int panelX, int panelY,
                                   int panelW, int /*screenH*/)
{
    setColor(r, {80, 140, 200, 80});
    SDL_RenderDrawLine(r, panelX + PANEL_PADDING, panelY,
                          panelX + panelW - PANEL_PADDING, panelY);

    drawText(r, "[A] Import    [B] Close", panelX + PANEL_PADDING, panelY + 14, COL_TEXT_DIM, 1);

    int usedSlots = 0;
    for (auto& b : m_blocks) if (!b.isEmpty) usedSlots++;
    std::string countStr = std::to_string(usedSlots) + " save"
                         + (usedSlots != 1 ? "s" : "") + " found";
    int textW = (int)countStr.size() * 5;
    drawText(r, countStr, panelX + panelW - PANEL_PADDING - textW,
             panelY + 14, COL_TEXT_DIM, 1);
}

void OmniSaveImport::renderConfirm(SDL_Renderer* r, int screenW, int screenH) {
    if (m_confirmSlot < 0 || m_confirmSlot >= (int)m_blocks.size()) return;
    const MemCardManager::ImportBlock& blk = m_blocks[m_confirmSlot];
    std::string name = blk.title.empty() ? blk.serial : blk.title;

    static constexpr int DW = 480, DH = 120;
    int dx = (screenW - DW) / 2, dy = (screenH - DH) / 2;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);
    SDL_Rect full{0, 0, screenW, screenH};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    fillRect(r, dx, dy, DW, DH, COL_BG);
    drawOutline(r, dx, dy, DW, DH, COL_CONFIRM_BDR, 2);

    drawText(r, "Import this save?",     dx + DW/2 - 80, dy + 16, COL_TEXT_MAIN, 2);
    drawText(r, name,                    dx + DW/2 - (int)name.size()*3, dy + 44, COL_TEXT_TEAL, 1);
    drawText(r, "One-way copy into active memory card.",
                                         dx + DW/2 - 100, dy + 60, COL_TEXT_DIM, 1);
    drawText(r, "[A] Confirm    [B] Cancel", dx + DW/2 - 65, dy + 90, COL_TEXT_DIM, 1);
}

void OmniSaveImport::renderErrorToast(SDL_Renderer* r, int screenW, int screenH) {
    int tw = PANEL_W - 40, tx = (screenW - tw) / 2;
    int panelH = HEADER_H + VISIBLE_ROWS * ROW_H + FOOTER_H + PANEL_PADDING * 2;
    int panelY = (screenH - panelH) / 2;
    int ty = panelY + panelH + 12;

    fillRect(r, tx, ty, tw, 34, COL_ERROR_BG);
    drawOutline(r, tx, ty, tw, 34, {200, 60, 60, 255});
    drawText(r, m_errorMsg, tx + 10, ty + 12, COL_TEXT_MAIN, 1);
}
