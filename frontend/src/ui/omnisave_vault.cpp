#include "omnisave_vault.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  PS1 memory card format constants
//  Reference: https://www.psdevwiki.com/ps3/PS1_Savedata
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int MCR_BLOCK_SIZE    = 8192;   // 8 KB per block
static constexpr int MCR_DIR_FRAME_SZ  = 128;    // each directory frame
static constexpr int MCR_MAX_BLOCKS    = 15;     // usable save blocks
static constexpr int MCR_HEADER_OFFSET = 128;    // first directory frame offset
static constexpr int MCR_ICON_W        = 16;
static constexpr int MCR_ICON_H        = 16;
static constexpr int MCR_ICON_BYTES    = 128;    // 16x16 @ 4bpp
static constexpr int MCR_PALETTE_BYTES = 32;     // 16 colours × 2 bytes (BGR555)

// Directory frame allocation state flags
static constexpr uint32_t ALLOC_FIRST  = 0x51;  // first block of a save
static constexpr uint32_t ALLOC_MID    = 0x52;  // middle block
static constexpr uint32_t ALLOC_LAST   = 0x53;  // last block
static constexpr uint32_t ALLOC_FREE   = 0xA0;  // free block

// ─────────────────────────────────────────────────────────────────────────────
//  Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
OmniSaveVault::OmniSaveVault(SDL_Renderer* renderer, ThemeEngine* theme,
                             ControllerNav* nav,
                             SaveStateManager* saveStates,
                             MemCardManager* memCards)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
    , m_saves(saveStates), m_memCards(memCards)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

OmniSaveVault::~OmniSaveVault() {
    freeMemCardTextures();
    freeSaveSlotTextures();
}

// ─────────────────────────────────────────────────────────────────────────────
//  open — call before making visible
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::open(const std::string& gameTitle,
                         const std::string& gameSerial,
                         OmniSaveMode mode)
{
    m_gameTitle  = gameTitle;
    m_gameSerial = gameSerial;
    m_mode       = mode;
    m_wantsClose = false;
    m_saveWritten = false;

    // Start focused on the right (save states) when saving/loading,
    // otherwise default to save states as the primary action panel.
    m_focus = OmniPanel::SAVESTATES;

    m_cardSel    = 0;  m_cardScroll  = 0;
    m_stateSel   = 0;  m_stateScroll = 0;
    m_iconAnimMs = 0.f; m_iconFrame  = 0;

    loadMemCardEntries();
    loadSaveSlots();

    std::cout << "[OmniSave] Opened for: " << gameTitle
              << "  card entries=" << m_cardEntries.size()
              << "  state slots=" << m_slots.size() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Data loading
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::freeMemCardTextures() {
    for (auto& e : m_cardEntries) {
        for (int f = 0; f < 3; ++f) {
            if (e.iconTextures[f]) {
                SDL_DestroyTexture(e.iconTextures[f]);
                e.iconTextures[f] = nullptr;
            }
        }
    }
    m_cardEntries.clear();
}

void OmniSaveVault::freeSaveSlotTextures() {
    for (auto* t : m_thumbTex) {
        if (t) SDL_DestroyTexture(t);
    }
    m_thumbTex.clear();
    m_slots.clear();
}

void OmniSaveVault::loadMemCardEntries() {
    freeMemCardTextures();
    if (!m_memCards) return;

    // Prepare the slot-1 path (creates file if missing) then parse it
    std::string path = m_memCards->prepareSlot1(m_gameSerial);
    if (!fs::exists(path)) return;

    m_cardEntries = parseMcr(path);

    // Build SDL textures from decoded icon pixel data
    for (auto& e : m_cardEntries) {
        for (int f = 0; f < e.frameCount; ++f) {
            if (!e.iconFrames[f].empty())
                e.iconTextures[f] = buildIconTexture(e.iconFrames[f]);
        }
    }
}

void OmniSaveVault::loadSaveSlots() {
    freeSaveSlotTextures();
    if (!m_saves) return;

    m_slots = m_saves->listSlots();

    // Append a sentinel "new slot" entry so the player can always create one
    SaveSlot newSlot;
    newSlot.slotNumber = -2;  // sentinel: "new save" card
    newSlot.exists     = false;
    m_slots.push_back(newSlot);

    // Load thumbnails
    m_thumbTex.resize(m_slots.size(), nullptr);
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].exists && m_slots[i].slotNumber != -2)
            m_thumbTex[i] = m_saves->loadThumbnail(m_slots[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  MCR parser
//  Reads the PS1 memory card binary and extracts all valid save entries,
//  including their animated icon frames decoded to RGBA8888.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<MemCardEntry> OmniSaveVault::parseMcr(const std::string& path) {
    std::vector<MemCardEntry> results;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return results;

    // Read entire file into memory for random access
    f.seekg(0, std::ios::end);
    size_t fileSize = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    if (fileSize < 131072) return results;  // must be at least 128KB

    std::vector<uint8_t> data(fileSize);
    f.read((char*)data.data(), fileSize);
    f.close();

    // Validate header magic bytes
    if (data[0] != 'M' || data[1] != 'C') {
        std::cerr << "[OmniSave] Not a valid PS1 memory card: " << path << "\n";
        return results;
    }

    // Walk directory frames — each is 128 bytes, starting at offset 128
    // Frames 1–15 are save directory entries; frame 0 is the header
    for (int block = 0; block < MCR_MAX_BLOCKS; ++block) {
        size_t frameOff = MCR_HEADER_OFFSET + (size_t)block * MCR_DIR_FRAME_SZ;
        if (frameOff + MCR_DIR_FRAME_SZ > fileSize) break;

        const uint8_t* frame = data.data() + frameOff;

        uint32_t allocState = frame[0];
        if (allocState != ALLOC_FIRST) continue;  // only process first blocks

        // ── Parse directory frame fields ───────────────────────────────────
        // Bytes 0:    allocation state
        // Bytes 4–7:  file size (little-endian)
        // Bytes 8–9:  next block link (0xFFFF = none)
        // Bytes 10–21: product code (ASCII, null-padded)  e.g. "SCUS-94900"
        // Bytes 22–31: identifier (ASCII, null-padded)
        // Bytes 64–127: display title (Shift-JIS, null-terminated)

        uint32_t fileBytes = (uint32_t)frame[4]
                           | ((uint32_t)frame[5] << 8)
                           | ((uint32_t)frame[6] << 16)
                           | ((uint32_t)frame[7] << 24);
        int blocksUsed = (int)std::max(1u, (fileBytes + MCR_BLOCK_SIZE - 1) / MCR_BLOCK_SIZE);

        char productBuf[13] = {};
        std::memcpy(productBuf, frame + 10, 12);
        char identBuf[9] = {};
        std::memcpy(identBuf, frame + 22, 8);

        // Display title starts at byte 64, up to 64 bytes of Shift-JIS
        std::string title = decodeShiftJis(frame + 64, 64);

        // ── Icon data lives in the save data block itself (not directory) ──
        // The save data block starts at:  frame (block+1) × 8192 bytes from file start
        // Within that block:
        //   Offset 0:     "SC" magic (2 bytes)
        //   Offset 2:     icon display flag (1 byte) — 0x11=1 frame, 0x12=2, 0x13=3
        //   Offset 3:     title length in bytes (1 byte, but we already have it)
        //   Offset 4–35:  palette — 16 × BGR555 colours (32 bytes)
        //   Offset 128:   frame 1 pixel data (128 bytes = 16×16 @ 4bpp)
        //   Offset 256:   frame 2 pixel data (if frameCount >= 2)
        //   Offset 384:   frame 3 pixel data (if frameCount == 3)

        size_t dataBlockOff = (size_t)(block + 1) * MCR_BLOCK_SIZE;
        if (dataBlockOff + 512 > fileSize) continue;

        const uint8_t* blk = data.data() + dataBlockOff;
        if (blk[0] != 'S' || blk[1] != 'C') continue;  // no SC magic → skip

        uint8_t iconFlag  = blk[2];
        int     frameCount = 1;
        if      (iconFlag == 0x12) frameCount = 2;
        else if (iconFlag == 0x13) frameCount = 3;

        // Decode palette: 16 BGR555 entries → RGBA8888
        uint32_t palette[16] = {};
        for (int c = 0; c < 16; ++c) {
            uint16_t raw = (uint16_t)blk[4 + c*2] | ((uint16_t)blk[5 + c*2] << 8);
            uint8_t r = (uint8_t)(((raw >>  0) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((raw >>  5) & 0x1F) << 3);
            uint8_t b = (uint8_t)(((raw >> 10) & 0x1F) << 3);
            // Entry 0 is transparent in PS1 icon rendering
            uint8_t a = (c == 0) ? 0 : 255;
            palette[c] = ((uint32_t)a << 24) | ((uint32_t)b << 16)
                       | ((uint32_t)g << 8)  |  (uint32_t)r;
        }

        // Decode each frame — 128 bytes of 4bpp → 256 nibbles → 256 palette indices
        MemCardEntry entry;
        entry.productCode = std::string(productBuf);
        entry.identifier  = std::string(identBuf);
        entry.title       = title.empty() ? entry.productCode : title;
        entry.blocksUsed  = blocksUsed;
        entry.frameCount  = frameCount;
        entry.firstBlock  = block;

        for (int frm = 0; frm < frameCount; ++frm) {
            size_t pixOff = 128 + (size_t)frm * MCR_ICON_BYTES;
            if (dataBlockOff + pixOff + MCR_ICON_BYTES > fileSize) break;

            const uint8_t* pixData = blk + pixOff;
            std::vector<uint32_t> rgba(MCR_ICON_W * MCR_ICON_H);

            for (int px = 0; px < MCR_ICON_W * MCR_ICON_H; px += 2) {
                uint8_t byte    = pixData[px / 2];
                uint8_t lo      = byte & 0x0F;
                uint8_t hi      = (byte >> 4) & 0x0F;
                rgba[px]     = palette[lo];
                rgba[px + 1] = palette[hi];
            }
            entry.iconFrames[frm] = std::move(rgba);
        }

        results.push_back(std::move(entry));
    }

    std::cout << "[OmniSave] Parsed " << results.size()
              << " entries from " << path << "\n";
    return results;
}

// Very light Shift-JIS → UTF-8 decoder covering the subset PS1 titles use.
// Full-width ASCII (0x8140–0x8196) → standard ASCII.
// Anything we can't decode we replace with '?'.
std::string OmniSaveVault::decodeShiftJis(const uint8_t* data, int len) {
    std::string out;
    out.reserve(len);
    int i = 0;
    while (i < len && data[i] != 0) {
        uint8_t b = data[i];
        if (b < 0x80) {
            // Standard ASCII
            out += (char)b;
            ++i;
        } else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            // Two-byte SJIS sequence
            if (i + 1 >= len) break;
            uint8_t b2 = data[i + 1];
            // Full-width space and alphanumeric (common in PS1 titles)
            uint16_t code = ((uint16_t)b << 8) | b2;
            if (code == 0x8140) {
                out += ' ';
            } else if (code >= 0x8260 && code <= 0x8279) {
                // Full-width A–Z
                out += (char)('A' + (code - 0x8260));
            } else if (code >= 0x8281 && code <= 0x8299) {
                // Full-width a–z (not standard SJIS but seen in some titles)
                out += (char)('a' + (code - 0x8281));
            } else if (code >= 0x824F && code <= 0x8258) {
                // Full-width 0–9
                out += (char)('0' + (code - 0x824F));
            } else {
                out += '?';
            }
            i += 2;
        } else if (b >= 0xA1 && b <= 0xDF) {
            // Half-width katakana — output as-is (close enough for display)
            out += (char)b;
            ++i;
        } else {
            out += '?';
            ++i;
        }
    }
    // Trim trailing spaces / question marks
    while (!out.empty() && (out.back() == ' ' || out.back() == '?'))
        out.pop_back();
    return out;
}

SDL_Texture* OmniSaveVault::buildIconTexture(const std::vector<uint32_t>& rgba) {
    // Create a 16×16 streaming texture from raw RGBA8888 pixels
    SDL_Texture* tex = SDL_CreateTexture(m_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STATIC,
        MCR_ICON_W, MCR_ICON_H);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, rgba.data(), MCR_ICON_W * sizeof(uint32_t));
    return tex;
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::update(float deltaMs) {
    // Advance SpriteCard animation
    m_iconAnimMs += deltaMs;
    if (m_iconAnimMs >= ICON_FRAME_MS) {
        m_iconAnimMs -= ICON_FRAME_MS;
        m_iconFrame = (m_iconFrame + 1) % 3;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleEvent
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleEvent(const SDL_Event& e) {
    NavAction a = m_nav->processEvent(e);
    if (a == NavAction::NONE) a = m_nav->updateHeld(SDL_GetTicks());

    if (a == NavAction::BACK) {
        m_wantsClose = true;
        return;
    }

    // L1 / R1 — switch panel focus
    if (a == NavAction::SHOULDER_L) {
        m_focus = OmniPanel::MEMCARD;
        return;
    }
    if (a == NavAction::SHOULDER_R) {
        m_focus = OmniPanel::SAVESTATES;
        return;
    }
    // PAGE_UP / PAGE_DOWN also switch panels for convenience
    if (a == NavAction::PAGE_UP)   { m_focus = OmniPanel::MEMCARD;    return; }
    if (a == NavAction::PAGE_DOWN) { m_focus = OmniPanel::SAVESTATES; return; }

    if (m_focus == OmniPanel::MEMCARD)    handleMemCardNav(a);
    else                                   handleSaveStateNav(a);
}

void OmniSaveVault::handleMemCardNav(NavAction a) {
    int count = (int)m_cardEntries.size();
    if (count == 0) return;

    if (a == NavAction::UP)   { m_cardSel = std::max(0, m_cardSel - 1); return; }
    if (a == NavAction::DOWN) { m_cardSel = std::min(count - 1, m_cardSel + 1); return; }

    if (a == NavAction::OPTIONS) {
        // X / delete — remove memory card entry (not implemented: requires
        // rewriting entire .mcr block table — scaffold here for future work)
        std::cout << "[OmniSave] Delete memcard entry: "
                  << m_cardEntries[m_cardSel].title << " (TODO)\n";
    }
}

void OmniSaveVault::handleSaveStateNav(NavAction a) {
    int count = (int)m_slots.size();
    if (count == 0) return;

    if (a == NavAction::UP) {
        m_stateSel = std::max(0, m_stateSel - STATE_COLS);
        return;
    }
    if (a == NavAction::DOWN) {
        m_stateSel = std::min(count - 1, m_stateSel + STATE_COLS);
        return;
    }
    if (a == NavAction::LEFT) {
        m_stateSel = std::max(0, m_stateSel - 1);
        return;
    }
    if (a == NavAction::RIGHT) {
        m_stateSel = std::min(count - 1, m_stateSel + 1);
        return;
    }

    if (a == NavAction::CONFIRM) {
        doLoadAction();
        return;
    }
    if (a == NavAction::MENU) {
        // Start / square = save to this slot
        doSaveAction();
        return;
    }
    if (a == NavAction::OPTIONS) {
        doDeleteState();
        return;
    }
}

void OmniSaveVault::doSaveAction() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;

    const SaveSlot& sel = m_slots[m_stateSel];
    int targetSlot;
    if (sel.slotNumber == -2) {
        // "New" sentinel — find next available slot number
        int maxSlot = 0;
        for (const auto& s : m_slots) {
            if (s.slotNumber >= 0) maxSlot = std::max(maxSlot, s.slotNumber + 1);
        }
        targetSlot = maxSlot;
    } else {
        targetSlot = sel.slotNumber;
    }

    if (m_saves->saveState(targetSlot)) {
        std::cout << "[OmniSave] Saved to slot " << targetSlot << "\n";
        m_saveWritten = true;
        loadSaveSlots();  // refresh list
    }
}

void OmniSaveVault::doLoadAction() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;
    const SaveSlot& sel = m_slots[m_stateSel];
    if (!sel.exists || sel.slotNumber == -2) return;

    if (m_saves->loadState(sel.slotNumber)) {
        std::cout << "[OmniSave] Loaded slot " << sel.slotNumber << "\n";
        m_wantsClose = true;  // return to game after successful load
    }
}

void OmniSaveVault::doDeleteState() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;
    const SaveSlot& sel = m_slots[m_stateSel];
    if (!sel.exists || sel.slotNumber == -2) return;

    m_saves->deleteSlot(sel.slotNumber);
    std::cout << "[OmniSave] Deleted slot " << sel.slotNumber << "\n";

    // Free the texture for this slot before refreshing
    if (m_thumbTex[m_stateSel]) {
        SDL_DestroyTexture(m_thumbTex[m_stateSel]);
        m_thumbTex[m_stateSel] = nullptr;
    }
    loadSaveSlots();
    m_stateSel = std::min(m_stateSel, (int)m_slots.size() - 1);
}

void OmniSaveVault::doDeleteEntry() {
    // Placeholder — deleting a memcard entry requires rewriting the .mcr
    // directory frame allocation table. Scaffold for Session 23.
    std::cout << "[OmniSave] Memcard entry delete: TODO\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  render
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::render() {
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    renderHeader();

    int contentY = HEADER_H;
    int contentH = m_h - HEADER_H - FOOTER_H;

    // Compute panel split
    int divX    = (m_w * DIVIDER_X_PC) / 100;
    int leftW   = divX;
    int rightW  = m_w - divX - DIVIDER_W;

    renderMemCardPanel(0, contentY, leftW, contentH);
    renderDivider(divX, contentY, contentH);
    renderSaveStatePanel(divX + DIVIDER_W, contentY, rightW, contentH);

    renderFooter();
}

void OmniSaveVault::renderHeader() {
    m_theme->drawHeader(m_w, m_h, "OmniSave", m_gameTitle, 0);
}

void OmniSaveVault::renderDivider(int x, int y, int h) {
    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer,
        pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_Rect line = { x, y, DIVIDER_W, h };
    SDL_RenderFillRect(m_renderer, &line);

    // Accent highlight on the active-panel side of the divider
    SDL_Color accent = m_theme->palette().accent;
    if (m_focus == OmniPanel::MEMCARD) {
        // Glow on right edge of divider (left panel active)
        SDL_SetRenderDrawColor(m_renderer, accent.r, accent.g, accent.b, 120);
        SDL_Rect glow = { x + DIVIDER_W, y, 3, h };
        SDL_RenderFillRect(m_renderer, &glow);
    } else {
        // Glow on left edge of divider (right panel active)
        SDL_SetRenderDrawColor(m_renderer, accent.r, accent.g, accent.b, 120);
        SDL_Rect glow = { x - 3, y, 3, h };
        SDL_RenderFillRect(m_renderer, &glow);
    }
}

// ─── Memory Card panel ────────────────────────────────────────────────────────
void OmniSaveVault::renderMemCardPanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::MEMCARD);

    // Panel label
    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("MEMORY CARD", x + MARGIN, y + 12, labelCol, FontSize::SMALL);

    // Card filename hint
    std::string cardLabel = "MemoryCard1.mcr";
    m_theme->drawText(cardLabel, x + MARGIN, y + 32, pal.textDisable, FontSize::TINY);

    int rowY = y + 62;
    int rowH = CARD_ROW_H;
    int visRows = (h - 62) / rowH;

    if (m_cardEntries.empty()) {
        m_theme->drawText("No saves on this memory card",
                          x + MARGIN, rowY + 20, pal.textDisable, FontSize::BODY);
        return;
    }

    // Clamp scroll so selected is visible
    if (m_cardSel < m_cardScroll)
        m_cardScroll = m_cardSel;
    if (m_cardSel >= m_cardScroll + visRows)
        m_cardScroll = m_cardSel - visRows + 1;

    int endIdx = std::min((int)m_cardEntries.size(), m_cardScroll + visRows);
    for (int i = m_cardScroll; i < endIdx; ++i) {
        const auto& entry = m_cardEntries[i];
        bool sel = focused && (i == m_cardSel);

        // Row background
        SDL_Rect rowRect = { x + 4, rowY, w - 8, rowH - 4 };
        if (sel) {
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCardHover.r, pal.bgCardHover.g, pal.bgCardHover.b, 200);
            SDL_RenderFillRect(m_renderer, &rowRect);
            // Accent left border
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { x + 4, rowY, 3, rowH - 4 };
            SDL_RenderFillRect(m_renderer, &border);
        }

        // Animated SpriteCard icon
        int frame = std::min(m_iconFrame, entry.frameCount - 1);
        SDL_Texture* iconTex = entry.iconTextures[frame];
        SDL_Rect iconDst = { x + MARGIN, rowY + (rowH - ICON_SIZE) / 2,
                             ICON_SIZE, ICON_SIZE };
        if (iconTex) {
            SDL_SetTextureBlendMode(iconTex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(m_renderer, iconTex, nullptr, &iconDst);
        } else {
            // Fallback: coloured placeholder square
            SDL_SetRenderDrawColor(m_renderer, 80, 80, 120, 255);
            SDL_RenderFillRect(m_renderer, &iconDst);
            m_theme->drawTextCentered("?",
                iconDst.x + ICON_SIZE / 2,
                iconDst.y + ICON_SIZE / 2 - 8,
                pal.textDisable, FontSize::SMALL);
        }

        // Text: title + block count
        int textX = x + MARGIN + ICON_SIZE + 10;
        int textW = w - MARGIN - ICON_SIZE - 14;

        m_theme->drawTextTruncated(entry.title, textX, rowY + 10,
                                   textW, sel ? pal.textPrimary : pal.textSecond,
                                   FontSize::BODY);

        std::string blockStr = std::to_string(entry.blocksUsed)
                             + (entry.blocksUsed == 1 ? " block" : " blocks");
        m_theme->drawText(blockStr, textX, rowY + 36,
                          pal.textDisable, FontSize::TINY);

        rowY += rowH;
    }

    // Scrollbar indicator
    if ((int)m_cardEntries.size() > visRows) {
        int sbH   = (h - 62);
        int sbX   = x + w - 6;
        float frac = (float)visRows / (float)m_cardEntries.size();
        float top  = (float)m_cardScroll / (float)m_cardEntries.size();
        int trackH = sbH;
        int thumbH = std::max(20, (int)(frac * trackH));
        int thumbY = y + 62 + (int)(top * trackH);
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 255);
        SDL_Rect track = { sbX, y + 62, 4, trackH };
        SDL_RenderFillRect(m_renderer, &track);
        SDL_SetRenderDrawColor(m_renderer,
            pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 200);
        SDL_Rect thumb = { sbX, thumbY, 4, thumbH };
        SDL_RenderFillRect(m_renderer, &thumb);
    }
}

// ─── Save State panel ─────────────────────────────────────────────────────────
void OmniSaveVault::renderSaveStatePanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::SAVESTATES);

    // Panel label
    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("SAVE STATES", x + MARGIN, y + 12, labelCol, FontSize::SMALL);

    // Grid of save state cards
    int cardW    = STATE_CARD_W;
    int cardH    = STATE_CARD_H;
    int padX     = 14;
    int padY     = 14;
    int gridX    = x + MARGIN;
    int gridY    = y + 54;
    int cols     = std::max(1, (w - MARGIN * 2 + padX) / (cardW + padX));
    int visRows  = std::max(1, (h - 54) / (cardH + padY));

    if (m_stateSel < m_stateScroll * cols)
        m_stateScroll = m_stateSel / cols;
    if (m_stateSel >= (m_stateScroll + visRows) * cols)
        m_stateScroll = (m_stateSel / cols) - visRows + 1;

    int startIdx = m_stateScroll * cols;
    int endIdx   = std::min((int)m_slots.size(), startIdx + visRows * cols);

    for (int i = startIdx; i < endIdx; ++i) {
        const SaveSlot& slot = m_slots[i];
        bool sel  = focused && (i == m_stateSel);
        int  col  = (i - startIdx) % cols;
        int  row  = (i - startIdx) / cols;
        int  cx   = gridX + col * (cardW + padX);
        int  cy   = gridY + row * (cardH + padY);

        SDL_Rect card = { cx, cy, cardW, cardH };

        bool isNew = (slot.slotNumber == -2);

        // Card background
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = sel ? pal.bgCardHover : pal.bgCard;
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 230);
        SDL_RenderFillRect(m_renderer, &card);

        // Selection border
        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { cx - 2, cy - 2, cardW + 4, cardH + 4 };
            SDL_RenderDrawRect(m_renderer, &border);
        }

        if (isNew) {
            // "New Save" card
            m_theme->drawTextCentered("+", cx + cardW / 2, cy + cardH / 2 - 20,
                                      sel ? pal.accent : pal.textDisable,
                                      FontSize::HEADER);
            m_theme->drawTextCentered("New Save",
                                      cx + cardW / 2, cy + cardH - 26,
                                      pal.textDisable, FontSize::TINY);
        } else if (slot.exists && i < (int)m_thumbTex.size() && m_thumbTex[i]) {
            // Thumbnail
            SDL_Rect thumbDst = { cx, cy, cardW, cardH - 36 };
            SDL_RenderCopy(m_renderer, m_thumbTex[i], nullptr, &thumbDst);

            // Label strip below thumbnail
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 220);
            SDL_Rect strip = { cx, cy + cardH - 36, cardW, 36 };
            SDL_RenderFillRect(m_renderer, &strip);

            // Slot label
            std::string label = (slot.slotNumber == -1) ? "Auto" :
                                 "Slot " + std::to_string(slot.slotNumber + 1);
            m_theme->drawText(label, cx + 6, cy + cardH - 32,
                              pal.textPrimary, FontSize::TINY);
            if (!slot.timestamp.empty()) {
                m_theme->drawText(slot.timestamp, cx + 6, cy + cardH - 16,
                                  pal.textDisable, FontSize::TINY);
            }
        } else {
            // Exists but no thumbnail
            m_theme->drawTextCentered("No Preview",
                cx + cardW / 2, cy + cardH / 2 - 10,
                pal.textDisable, FontSize::TINY);
            std::string label = (slot.slotNumber == -1) ? "Auto" :
                                 "Slot " + std::to_string(slot.slotNumber + 1);
            m_theme->drawText(label, cx + 6, cy + cardH - 16,
                              pal.textSecond, FontSize::TINY);
        }
    }
}

// ─── Footer ───────────────────────────────────────────────────────────────────
void OmniSaveVault::renderFooter() {
    if (m_focus == OmniPanel::SAVESTATES) {
        m_theme->drawFooterHints(m_w, m_h,
            "Load",          // A
            "Back",          // B
            "Save Here",     // X (Start in this context)
            "Delete");       // Y
    } else {
        m_theme->drawFooterHints(m_w, m_h,
            "View Entry",    // A
            "Back",          // B
            "",              // X
            "Delete");       // Y
    }
    // Draw L1/R1 panel-switch hint on the right side manually
    int footY = m_h - m_theme->layout().footerH;
    int cy    = footY + (m_theme->layout().footerH - 34) / 2;
    m_theme->drawButtonHint(m_w - 220, cy, "L1/R1",
                            "Switch Panel", m_theme->palette().textSecond);
}
