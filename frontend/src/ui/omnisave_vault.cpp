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
static constexpr int MCR_BLOCK_SIZE    = 8192;
static constexpr int MCR_DIR_FRAME_SZ  = 128;
static constexpr int MCR_MAX_BLOCKS    = 15;
static constexpr int MCR_HEADER_OFFSET = 128;
static constexpr int MCR_ICON_W        = 16;
static constexpr int MCR_ICON_H        = 16;
static constexpr int MCR_ICON_BYTES    = 128;
static constexpr int MCR_PALETTE_BYTES = 32;

static constexpr int MCR_ICON_PAL_OFF  = 96;
static constexpr int MCR_ICON_PIX_OFF  = 128;

static constexpr uint32_t ALLOC_FIRST  = 0x51;
static constexpr uint32_t ALLOC_MID    = 0x52;
static constexpr uint32_t ALLOC_LAST   = 0x53;
static constexpr uint32_t ALLOC_FREE   = 0xA0;

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
    freeTimeMachineTextures();
    if (m_gameScreenshot) {
        SDL_FreeSurface(m_gameScreenshot);
        m_gameScreenshot = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  open
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::open(const std::string& gameTitle,
                         const std::string& gameSerial,
                         OmniSaveMode mode,
                         SDL_Surface* gameScreenshot)
{
    if (m_gameScreenshot) {
        SDL_FreeSurface(m_gameScreenshot);
        m_gameScreenshot = nullptr;
    }
    m_gameScreenshot = gameScreenshot;
    m_gameTitle  = gameTitle;
    m_gameSerial = gameSerial;
    m_mode       = mode;
    m_wantsClose  = false;
    m_saveWritten = false;

    m_focus = OmniPanel::SAVESTATES;

    m_cardSel    = 0;  m_cardScroll  = 0;
    m_stateSel   = 0;  m_stateScroll = 0;
    m_snapSel    = 0;  m_snapScroll  = 0;
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

void OmniSaveVault::freeTimeMachineTextures() {
    for (auto& snap : m_snapshots) {
        if (snap.thumbTex) {
            SDL_DestroyTexture(snap.thumbTex);
            snap.thumbTex = nullptr;
        }
        for (auto& e : snap.entries) {
            for (int f = 0; f < 3; ++f) {
                if (e.iconTextures[f]) {
                    SDL_DestroyTexture(e.iconTextures[f]);
                    e.iconTextures[f] = nullptr;
                }
            }
        }
    }
    m_snapshots.clear();
}

void OmniSaveVault::loadMemCardEntries() {
    freeMemCardTextures();
    if (!m_memCards) return;

    std::string mcrPath = m_memCards->prepareSlot1(m_gameSerial);

    std::string srmPath;
    if (!m_gameTitle.empty()) {
        fs::path dir = fs::path(mcrPath).parent_path();
        fs::path candidate = dir / (m_gameTitle + ".srm");
        if (fs::exists(candidate))
            srmPath = candidate.string();
        if (srmPath.empty() && !m_gameSerial.empty()) {
            candidate = dir / (m_gameSerial + ".srm");
            if (fs::exists(candidate))
                srmPath = candidate.string();
        }
    }

    std::string path = mcrPath;
    if (!srmPath.empty()) {
        size_t srmSz = fs::exists(srmPath) ? fs::file_size(srmPath) : 0;
        size_t mcrSz = fs::exists(mcrPath) ? fs::file_size(mcrPath) : 0;
        if (srmSz > mcrSz) {
            path = srmPath;
            std::cout << "[OmniSave] Using .srm from core: " << srmPath << "\n";
        }
    }

    if (!fs::exists(path)) return;

    m_cardEntries = parseMcr(path);

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

    SaveSlot newSlot;
    newSlot.slotNumber = -2;
    newSlot.exists     = false;
    m_slots.push_back(newSlot);

    m_thumbTex.resize(m_slots.size(), nullptr);
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].exists && m_slots[i].slotNumber != -2)
            m_thumbTex[i] = m_saves->loadThumbnail(m_slots[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  loadTimeMachineEntries
//  Scans memcards/history/<SERIAL>_1/ for .mcr snapshots, pairs them with
//  same-stem .png thumbnails, parses SpriteCard icons, loads thumbnails.
//  Sorted newest-first (reverse lexicographic on the timestamp filename stem).
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::loadTimeMachineEntries() {
    freeTimeMachineTextures();
    if (m_gameSerial.empty()) return;

    std::string histDir = "memcards/history/" + m_gameSerial + "_1/";
    if (!fs::exists(histDir)) {
        std::cout << "[TimeMachine] No history folder: " << histDir << "\n";
        return;
    }

    std::vector<fs::path> mcrFiles;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(histDir, ec)) {
        if (entry.path().extension() == ".mcr")
            mcrFiles.push_back(entry.path());
    }
    // Lexicographic sort on timestamp stems → oldest first, then reverse for newest-first
    std::sort(mcrFiles.begin(), mcrFiles.end());
    std::reverse(mcrFiles.begin(), mcrFiles.end());

    for (const auto& mcrPath : mcrFiles) {
        TimeMachineEntry snap;
        snap.mcrPath     = mcrPath.string();
        snap.rawFilename = mcrPath.stem().string();
        snap.timestamp   = formatSnapshotTimestamp(snap.rawFilename);

        // Look for paired .png with same stem
        fs::path pngPath = mcrPath;
        pngPath.replace_extension(".png");
        if (fs::exists(pngPath))
            snap.pngPath = pngPath.string();

        // Parse SpriteCard icons from this snapshot
        snap.entries = parseMcr(snap.mcrPath);
        for (auto& e : snap.entries) {
            for (int f = 0; f < e.frameCount; ++f) {
                if (!e.iconFrames[f].empty())
                    e.iconTextures[f] = buildIconTexture(e.iconFrames[f]);
            }
        }

        // Load game frame thumbnail texture
        if (!snap.pngPath.empty()) {
            SDL_Surface* surf = IMG_Load(snap.pngPath.c_str());
            if (surf) {
                snap.thumbTex = SDL_CreateTextureFromSurface(m_renderer, surf);
                SDL_FreeSurface(surf);
            }
        }

        m_snapshots.push_back(std::move(snap));
    }

    std::cout << "[TimeMachine] Loaded " << m_snapshots.size()
              << " snapshots from " << histDir << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  formatSnapshotTimestamp
//  Converts a stem like "2026-05-08_21-04-33" → "May 08  21:04"
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveVault::formatSnapshotTimestamp(const std::string& stem) {
    if (stem.size() < 19) return stem;
    int month = 0, day = 0, hour = 0, min = 0;
    try {
        month = std::stoi(stem.substr(5, 2));
        day   = std::stoi(stem.substr(8, 2));
        hour  = std::stoi(stem.substr(11, 2));
        min   = std::stoi(stem.substr(14, 2));
    } catch (...) {
        return stem;
    }
    static const char* months[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (month < 1 || month > 12) return stem;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %02d  %02d:%02d",
                  months[month], day, hour, min);
    return std::string(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MCR parser
// ─────────────────────────────────────────────────────────────────────────────
std::vector<MemCardEntry> OmniSaveVault::parseMcr(const std::string& path) {
    std::vector<MemCardEntry> results;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return results;

    f.seekg(0, std::ios::end);
    size_t fileSize = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    if (fileSize < 131072) return results;

    std::vector<uint8_t> data(fileSize);
    f.read((char*)data.data(), fileSize);
    f.close();

    if (data[0] != 'M' || data[1] != 'C') {
        std::cerr << "[OmniSave] Not a valid PS1 memory card: " << path << "\n";
        return results;
    }

    for (int block = 0; block < MCR_MAX_BLOCKS; ++block) {
        size_t frameOff = MCR_HEADER_OFFSET + (size_t)block * MCR_DIR_FRAME_SZ;
        if (frameOff + MCR_DIR_FRAME_SZ > fileSize) break;

        const uint8_t* frame = data.data() + frameOff;
        uint32_t allocState = frame[0];
        if (allocState != ALLOC_FIRST) continue;

        uint32_t fileBytes = (uint32_t)frame[4]
                           | ((uint32_t)frame[5] << 8)
                           | ((uint32_t)frame[6] << 16)
                           | ((uint32_t)frame[7] << 24);
        int blocksUsed = (int)std::max(1u, (fileBytes + MCR_BLOCK_SIZE - 1) / MCR_BLOCK_SIZE);

        char productBuf[13] = {};
        std::memcpy(productBuf, frame + 10, 12);
        char identBuf[9] = {};
        std::memcpy(identBuf, frame + 22, 8);

        std::string title = decodeShiftJis(frame + 64, 64);

        size_t dataBlockOff = (size_t)(block + 1) * MCR_BLOCK_SIZE;
        if (dataBlockOff + 512 > fileSize) continue;

        const uint8_t* blk = data.data() + dataBlockOff;
        if (blk[0] != 'S' || blk[1] != 'C') continue;

        uint8_t iconFlag  = blk[2];
        int     frameCount = 1;
        if      (iconFlag == 0x12) frameCount = 2;
        else if (iconFlag == 0x13) frameCount = 3;

        uint32_t palette[16] = {};
        for (int c = 0; c < 16; ++c) {
            uint16_t raw = (uint16_t)blk[MCR_ICON_PAL_OFF + c*2]
                         | ((uint16_t)blk[MCR_ICON_PAL_OFF + c*2 + 1] << 8);
            uint8_t r5  = (raw >> 0)  & 0x1F;
            uint8_t g5  = (raw >> 5)  & 0x1F;
            uint8_t b5  = (raw >> 10) & 0x1F;
            uint8_t stp = (raw >> 15) & 0x01;
            uint8_t r = (r5 << 3) | (r5 >> 2);
            uint8_t g = (g5 << 3) | (g5 >> 2);
            uint8_t b = (b5 << 3) | (b5 >> 2);
            uint8_t a = 255;
            if (r5 == 0 && g5 == 0 && b5 == 0)
                a = (stp == 0) ? 0 : 255;
            palette[c] = ((uint32_t)r << 24) | ((uint32_t)g << 16)
                       | ((uint32_t)b << 8)  |  (uint32_t)a;
        }

        MemCardEntry entry;
        entry.productCode = std::string(productBuf);
        entry.identifier  = std::string(identBuf);

        std::string cleanTitle = title;
        if (!cleanTitle.empty() && !entry.productCode.empty()) {
            std::string baPrefix = "BA" + entry.productCode;
            if (cleanTitle.size() > baPrefix.size() &&
                cleanTitle.compare(0, baPrefix.size(), baPrefix) == 0) {
                cleanTitle = cleanTitle.substr(baPrefix.size());
            } else if (cleanTitle.size() > entry.productCode.size() &&
                       cleanTitle.compare(0, entry.productCode.size(), entry.productCode) == 0) {
                cleanTitle = cleanTitle.substr(entry.productCode.size());
            }
        }
        size_t firstChar = cleanTitle.find_first_not_of(' ');
        if (firstChar != std::string::npos)
            cleanTitle = cleanTitle.substr(firstChar);

        if (!cleanTitle.empty())
            entry.title = cleanTitle;
        else if (!m_gameTitle.empty())
            entry.title = m_gameTitle;
        else
            entry.title = entry.productCode;

        entry.blocksUsed = blocksUsed;
        entry.frameCount = frameCount;
        entry.firstBlock = block;

        for (int frm = 0; frm < frameCount; ++frm) {
            size_t pixOff = MCR_ICON_PIX_OFF + (size_t)frm * MCR_ICON_BYTES;
            if (dataBlockOff + pixOff + MCR_ICON_BYTES > fileSize) break;

            const uint8_t* pixData = blk + pixOff;
            std::vector<uint32_t> rgba(MCR_ICON_W * MCR_ICON_H);

            for (int px = 0; px < MCR_ICON_W * MCR_ICON_H; px += 2) {
                uint8_t byte = pixData[px / 2];
                uint8_t lo   = byte & 0x0F;
                uint8_t hi   = (byte >> 4) & 0x0F;
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

// ─────────────────────────────────────────────────────────────────────────────
//  decodeShiftJis
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveVault::decodeShiftJis(const uint8_t* data, int len) {
    std::string out;
    out.reserve(len);
    int i = 0;
    while (i < len && data[i] != 0) {
        uint8_t b = data[i];
        if (b < 0x80) {
            out += (char)b;
            ++i;
        } else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            if (i + 1 >= len) break;
            uint8_t b2 = data[i + 1];
            uint16_t code = ((uint16_t)b << 8) | b2;
            if (code == 0x8140) {
                out += ' ';
            } else if (code >= 0x8260 && code <= 0x8279) {
                out += (char)('A' + (code - 0x8260));
            } else if (code >= 0x8281 && code <= 0x8299) {
                out += (char)('a' + (code - 0x8281));
            } else if (code >= 0x824F && code <= 0x8258) {
                out += (char)('0' + (code - 0x824F));
            } else {
                out += '?';
            }
            i += 2;
        } else if (b >= 0xA1 && b <= 0xDF) {
            out += (char)b;
            ++i;
        } else {
            out += '?';
            ++i;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '?'))
        out.pop_back();
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  buildIconTexture
// ─────────────────────────────────────────────────────────────────────────────
SDL_Texture* OmniSaveVault::buildIconTexture(const std::vector<uint32_t>& rgba) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        (void*)rgba.data(),
        MCR_ICON_W, MCR_ICON_H,
        32,
        MCR_ICON_W * 4,
        0xFF000000,
        0x00FF0000,
        0x0000FF00,
        0x000000FF
    );
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    return tex;
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::update(float deltaMs) {
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

    // ── Confirm dialog intercept ──────────────────────────────────────────────
    if (m_confirmAction != ConfirmAction::NONE) {
        if (a == NavAction::CONFIRM) {
            ConfirmAction action = m_confirmAction;
            m_confirmAction = ConfirmAction::NONE;
            if      (action == ConfirmAction::LOAD_STATE)       doLoadAction();
            else if (action == ConfirmAction::DELETE_STATE)     doDeleteState();
            else if (action == ConfirmAction::OVERWRITE_STATE)  doSaveAction();
            else if (action == ConfirmAction::DELETE_ENTRY)     doDeleteEntry();
            else if (action == ConfirmAction::RESTORE_SNAPSHOT) doRestoreSnapshot();
            else if (action == ConfirmAction::RELOAD_CARD) {
                m_wantsCardReload = true;
                m_wantsClose      = true;
            }
        } else if (a == NavAction::BACK) {
            m_confirmAction = ConfirmAction::NONE;
        }
        return;
    }

    // ── Time Machine panel handles its own BACK (returns to MEMCARD) ──────────
    if (m_focus == OmniPanel::TIMEMACHINE) {
        handleTimeMachineNav(a);
        return;
    }

    if (a == NavAction::BACK) {
        m_wantsClose = true;
        return;
    }

    // L1 / R1 — switch panel focus (not while Time Machine is open)
    if (a == NavAction::SHOULDER_L) { m_focus = OmniPanel::MEMCARD;    return; }
    if (a == NavAction::SHOULDER_R) { m_focus = OmniPanel::SAVESTATES; return; }
    if (a == NavAction::PAGE_UP)    { m_focus = OmniPanel::MEMCARD;    return; }
    if (a == NavAction::PAGE_DOWN)  { m_focus = OmniPanel::SAVESTATES; return; }

    if (m_focus == OmniPanel::MEMCARD)  handleMemCardNav(a);
    else                                 handleSaveStateNav(a);
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleMemCardNav
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleMemCardNav(NavAction a) {
    int count = (int)m_cardEntries.size();

    if (a == NavAction::UP)   { m_cardSel = std::max(0, m_cardSel - 1); return; }
    if (a == NavAction::DOWN) { m_cardSel = std::min(std::max(0, count - 1), m_cardSel + 1); return; }

    // Y / Triangle — open Card Time Machine history panel.
    // Uses FAVORITE NavAction since that is how Y/Triangle is mapped in this project.
    // The memcard panel has no favorites concept so this binding is unambiguous here.
    if (a == NavAction::FAVORITE) {
        loadTimeMachineEntries();
        m_snapSel    = 0;
        m_snapScroll = 0;
        m_focus      = OmniPanel::TIMEMACHINE;
        return;
    }

    if (count == 0) return;

    if (a == NavAction::CONFIRM) {
        const std::string& entryTitle = m_cardEntries[m_cardSel].title;
        std::cout << "[OmniSave] Card reload confirm requested for: "
                  << entryTitle << "\n";
        m_confirmAction  = ConfirmAction::RELOAD_CARD;
        m_confirmMessage = "Reload memory card from disk?";
        m_confirmDetail  = "Unsaved in-game changes will be lost.";
        return;
    }

    if (a == NavAction::OPTIONS) {
        const std::string& title = m_cardEntries[m_cardSel].title;
        int blocks = m_cardEntries[m_cardSel].blocksUsed;
        std::string detail = title + "  \xe2\x80\x94  "
                           + std::to_string(blocks)
                           + (blocks == 1 ? " block" : " blocks")
                           + "  \xe2\x80\x94  This cannot be undone.";
        m_confirmAction  = ConfirmAction::DELETE_ENTRY;
        m_confirmMessage = "Delete this memory card save?";
        m_confirmDetail  = detail;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleSaveStateNav  (unchanged from original)
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleSaveStateNav(NavAction a) {
    int count = (int)m_slots.size();
    if (count == 0) return;

    if (a == NavAction::UP)    { m_stateSel = std::max(0, m_stateSel - STATE_COLS);         return; }
    if (a == NavAction::DOWN)  { m_stateSel = std::min(count - 1, m_stateSel + STATE_COLS); return; }
    if (a == NavAction::LEFT)  { m_stateSel = std::max(0, m_stateSel - 1);                  return; }
    if (a == NavAction::RIGHT) { m_stateSel = std::min(count - 1, m_stateSel + 1);          return; }

    if (a == NavAction::CONFIRM) {
        const SaveSlot& sel = m_slots[m_stateSel];
        if (sel.slotNumber == -2 || !sel.exists) {
            doSaveAction();
        } else {
            std::string label = (sel.slotNumber == -1)
                ? "Auto-save"
                : "Slot " + std::to_string(sel.slotNumber + 1);
            if (!sel.timestamp.empty() && sel.timestamp != label)
                label += "  \xe2\x80\xa2  " + sel.timestamp;
            m_confirmAction  = ConfirmAction::LOAD_STATE;
            m_confirmMessage = "Load this save?";
            m_confirmDetail  = label;
        }
        return;
    }
    if (a == NavAction::OPTIONS) {
        const SaveSlot& sel = m_slots[m_stateSel];
        if (sel.exists && sel.slotNumber != -2) {
            std::string label = (sel.slotNumber == -1)
                ? "Auto-save"
                : "Slot " + std::to_string(sel.slotNumber + 1);
            m_confirmAction  = ConfirmAction::OVERWRITE_STATE;
            m_confirmMessage = "Overwrite this save?";
            m_confirmDetail  = label + "  \xe2\x80\x94  Current progress will replace it.";
        } else {
            doSaveAction();
        }
        return;
    }
    if (a == NavAction::MENU) {
        const SaveSlot& sel = m_slots[m_stateSel];
        if (!sel.exists || sel.slotNumber == -2) return;
        std::string label = (sel.slotNumber == -1)
            ? "Auto-save"
            : "Slot " + std::to_string(sel.slotNumber + 1);
        m_confirmAction  = ConfirmAction::DELETE_STATE;
        m_confirmMessage = "Delete this save?";
        m_confirmDetail  = label + "  \xe2\x80\x94  This cannot be undone.";
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleTimeMachineNav
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleTimeMachineNav(NavAction a) {
    // B = back to memory card panel (Time Machine owns its own BACK)
    if (a == NavAction::BACK) {
        m_focus = OmniPanel::MEMCARD;
        return;
    }

    int count = (int)m_snapshots.size();
    if (count == 0) return;

    if (a == NavAction::UP)   { m_snapSel = std::max(0, m_snapSel - 1);         return; }
    if (a == NavAction::DOWN) { m_snapSel = std::min(count - 1, m_snapSel + 1); return; }

    if (a == NavAction::CONFIRM) {
        const TimeMachineEntry& snap = m_snapshots[m_snapSel];
        m_confirmAction  = ConfirmAction::RESTORE_SNAPSHOT;
        m_confirmMessage = "Restore this card snapshot?";
        m_confirmDetail  = snap.timestamp
                         + "  \xe2\x80\x94  Current card will be backed up first.";
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  doSaveAction
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doSaveAction() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;

    const SaveSlot& sel = m_slots[m_stateSel];
    int targetSlot;
    if (sel.slotNumber == -2) {
        int maxSlot = 0;
        for (const auto& s : m_slots) {
            if (s.slotNumber >= 0) maxSlot = std::max(maxSlot, s.slotNumber + 1);
        }
        targetSlot = maxSlot;
    } else {
        targetSlot = sel.slotNumber;
    }

    if (m_saves->saveState(targetSlot, m_gameScreenshot)) {
        std::cout << "[OmniSave] Saved to slot " << targetSlot << "\n";
        m_saveWritten = true;
        loadSaveSlots();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  doLoadAction
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doLoadAction() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;
    const SaveSlot& sel = m_slots[m_stateSel];
    if (!sel.exists || sel.slotNumber == -2) return;

    if (m_sramFlush) m_sramFlush();

    if (m_saves->loadState(sel.slotNumber)) {
        std::cout << "[OmniSave] Loaded slot " << sel.slotNumber << "\n";
        if (m_sramReload) m_sramReload();
        m_wantsClose = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  doDeleteState
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doDeleteState() {
    if (!m_saves || m_stateSel < 0 || m_stateSel >= (int)m_slots.size()) return;
    const SaveSlot& sel = m_slots[m_stateSel];
    if (!sel.exists || sel.slotNumber == -2) return;

    m_saves->deleteSlot(sel.slotNumber);
    std::cout << "[OmniSave] Deleted slot " << sel.slotNumber << "\n";

    if (m_thumbTex[m_stateSel]) {
        SDL_DestroyTexture(m_thumbTex[m_stateSel]);
        m_thumbTex[m_stateSel] = nullptr;
    }
    loadSaveSlots();
    m_stateSel = std::min(m_stateSel, (int)m_slots.size() - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  doDeleteEntry
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doDeleteEntry() {
    if (m_cardSel < 0 || m_cardSel >= (int)m_cardEntries.size()) return;
    const MemCardEntry& entry = m_cardEntries[m_cardSel];

    if (!m_memCards) return;
    std::string mcrPath = m_memCards->prepareSlot1(m_gameSerial);

    std::string path = mcrPath;
    if (!m_gameTitle.empty()) {
        fs::path dir = fs::path(mcrPath).parent_path();
        for (const auto& stem : { m_gameTitle, m_gameSerial }) {
            if (stem.empty()) continue;
            fs::path cand = dir / (stem + ".srm");
            if (fs::exists(cand)) {
                size_t srmSz = fs::file_size(cand);
                size_t mcrSz = fs::exists(mcrPath) ? fs::file_size(mcrPath) : 0;
                if (srmSz > mcrSz) { path = cand.string(); break; }
            }
        }
    }

    if (!fs::exists(path)) {
        std::cerr << "[OmniSave] Delete failed — card not found: " << path << "\n";
        return;
    }

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        std::cerr << "[OmniSave] Delete failed — cannot open: " << path << "\n";
        return;
    }
    fin.seekg(0, std::ios::end);
    size_t fileSize = (size_t)fin.tellg();
    fin.seekg(0);
    if (fileSize < 131072) {
        std::cerr << "[OmniSave] Delete failed — file too small\n";
        return;
    }
    std::vector<uint8_t> data(fileSize);
    fin.read((char*)data.data(), fileSize);
    fin.close();

    int block = entry.firstBlock;
    int safetyLimit = MCR_MAX_BLOCKS;

    while (block >= 0 && block < MCR_MAX_BLOCKS && safetyLimit-- > 0) {
        size_t frameOff = MCR_HEADER_OFFSET + (size_t)block * MCR_DIR_FRAME_SZ;
        if (frameOff + MCR_DIR_FRAME_SZ > fileSize) break;

        uint8_t* frame = data.data() + frameOff;

        int nextBlock = (int)frame[8] | ((int)frame[9] << 8);
        if (nextBlock == 0xFFFF) nextBlock = -1;

        frame[0] = ALLOC_FREE;
        frame[4] = frame[5] = frame[6] = frame[7] = 0;
        frame[8] = 0xFF; frame[9] = 0xFF;
        std::memset(frame + 10, 0, 12);
        std::memset(frame + 22, 0, 8);
        std::memset(frame + 64, 0, 63);

        uint8_t xsum = 0;
        for (int b = 0; b < 127; ++b) xsum ^= frame[b];
        frame[127] = xsum;

        std::cout << "[OmniSave] Freed block " << block
                  << " (chain next=" << nextBlock << ")\n";
        block = nextBlock;
    }

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream fout(tmpPath, std::ios::binary | std::ios::trunc);
        if (!fout.is_open()) {
            std::cerr << "[OmniSave] Delete failed — cannot write temp\n";
            return;
        }
        fout.write((const char*)data.data(), (std::streamsize)data.size());
        fout.flush();
    }

    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        std::cerr << "[OmniSave] Delete failed — rename error: " << ec.message() << "\n";
        fs::remove(tmpPath, ec);
        return;
    }

    std::cout << "[OmniSave] Deleted memcard entry: " << entry.title
              << "  (firstBlock=" << entry.firstBlock << ")\n";

    if (m_sramReload) m_sramReload();

    freeMemCardTextures();
    m_cardEntries = parseMcr(path);
    for (auto& e : m_cardEntries) {
        for (int f = 0; f < e.frameCount; ++f) {
            if (!e.iconFrames[f].empty())
                e.iconTextures[f] = buildIconTexture(e.iconFrames[f]);
        }
    }
    m_cardSel = std::min(m_cardSel, std::max(0, (int)m_cardEntries.size() - 1));
}

// ─────────────────────────────────────────────────────────────────────────────
//  doRestoreSnapshot
//  Restores the selected Time Machine snapshot as the active memory card.
//
//  Steps:
//    1. Flush SRAM → disk (ensures in-game writes since open() are persisted)
//    2. Safety-snapshot the current card to history (nothing ever unrecoverable)
//    3. Atomic copy: snapshot .mcr → active card path (tmp → rename)
//    4. Reload core SRAM from the restored card
//    5. Refresh Time Machine list and memcard entries
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doRestoreSnapshot() {
    if (m_snapSel < 0 || m_snapSel >= (int)m_snapshots.size()) return;

    const TimeMachineEntry& snap = m_snapshots[m_snapSel];
    if (!fs::exists(snap.mcrPath)) {
        std::cerr << "[TimeMachine] Snapshot file missing: " << snap.mcrPath << "\n";
        return;
    }

    // Resolve the active card destination path
    std::string destPath = m_activeCardPath;
    if (destPath.empty() && m_memCards) {
        destPath = fs::absolute(
            m_memCards->activeCardPath(m_gameSerial)).string();
    }
    if (destPath.empty()) {
        std::cerr << "[TimeMachine] Cannot restore — active card path unknown\n";
        return;
    }

    // ── Step 1: flush current SRAM to disk ───────────────────────────────────
    if (m_sramFlush) m_sramFlush();

    // ── Step 2: safety-snapshot current card before overwriting ──────────────
    if (fs::exists(destPath)) {
        fs::path cardPath(destPath);
        std::string stem    = cardPath.stem().string();
        std::string histDir = "memcards/history/" + stem + "/";
        std::error_code ec;
        fs::create_directories(histDir, ec);
        if (!ec) {
            time_t now = time(nullptr);
            tm* t = localtime(&now);
            char ts[32];
            strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", t);
            std::string safetyPath = histDir + ts + ".mcr";
            fs::copy_file(destPath, safetyPath,
                          fs::copy_options::overwrite_existing, ec);
            if (!ec)
                std::cout << "[TimeMachine] Safety snapshot: " << safetyPath << "\n";
            else
                std::cerr << "[TimeMachine] Safety snapshot failed: " << ec.message() << "\n";
        }
    }

    // ── Step 3: atomic copy snapshot → active card ───────────────────────────
    std::string tmpPath = destPath + ".restore.tmp";
    {
        std::ifstream src(snap.mcrPath, std::ios::binary);
        std::ofstream dst(tmpPath,      std::ios::binary | std::ios::trunc);
        if (!src.is_open() || !dst.is_open()) {
            std::cerr << "[TimeMachine] Restore failed — cannot open files\n";
            return;
        }
        dst << src.rdbuf();
        dst.flush();
    }

    std::error_code ec;
    fs::rename(tmpPath, destPath, ec);
    if (ec) {
        std::cerr << "[TimeMachine] Restore rename failed: " << ec.message() << "\n";
        fs::remove(tmpPath, ec);
        return;
    }

    std::cout << "[TimeMachine] Restored: " << snap.mcrPath
              << " → " << destPath << "\n";

    // ── Step 4: reload core SRAM from restored card ───────────────────────────
    if (m_sramReload) m_sramReload();

    // ── Step 5: refresh UI ────────────────────────────────────────────────────
    loadTimeMachineEntries();  // re-scan (safety snapshot is now newest entry)
    loadMemCardEntries();      // refresh main card panel
    m_snapSel = 0;             // jump to top (newest = just-snapped safety card)

    std::cout << "[TimeMachine] Restore complete\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  render
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::render() {
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    renderHeader();

    int contentY = HEADER_H;
    int contentH = m_h - HEADER_H - FOOTER_H;
    int divX     = (m_w * DIVIDER_X_PC) / 100;
    int leftW    = divX;
    int rightW   = m_w - divX - DIVIDER_W;

    if (m_focus == OmniPanel::TIMEMACHINE) {
        // Time Machine takes the full content area — both panels step aside
        renderTimeMachinePanel(0, contentY, m_w, contentH);
    } else {
        renderMemCardPanel(0, contentY, leftW, contentH);
        renderDivider(divX, contentY, contentH);
        renderSaveStatePanel(divX + DIVIDER_W, contentY, rightW, contentH);
    }

    renderFooter();

    if (m_confirmAction != ConfirmAction::NONE)
        renderConfirmOverlay();
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderConfirmOverlay
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderConfirmOverlay() {
    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
    SDL_Rect full = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &full);

    int boxW = 480, boxH = 160;
    int boxX = (m_w - boxW) / 2;
    int boxY = (m_h - boxH) / 2;

    SDL_SetRenderDrawColor(m_renderer,
        pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 245);
    SDL_Rect box = { boxX, boxY, boxW, boxH };
    SDL_RenderFillRect(m_renderer, &box);

    // Border — colour-coded by action severity
    SDL_Color borderClr;
    if (m_confirmAction == ConfirmAction::DELETE_STATE ||
        m_confirmAction == ConfirmAction::DELETE_ENTRY)
        borderClr = { 220, 80,  80,  255 };   // red    — permanent destruction
    else if (m_confirmAction == ConfirmAction::OVERWRITE_STATE)
        borderClr = { 220, 160, 40,  255 };   // amber  — replaces existing data
    else if (m_confirmAction == ConfirmAction::RESTORE_SNAPSHOT)
        borderClr = { 160, 80,  220, 255 };   // purple — time travel!
    else
        borderClr = { 60,  160, 220, 255 };   // blue   — safe (load / reload)

    SDL_SetRenderDrawColor(m_renderer,
        borderClr.r, borderClr.g, borderClr.b, 255);
    SDL_RenderDrawRect(m_renderer, &box);
    SDL_Rect box2 = { boxX + 1, boxY + 1, boxW - 2, boxH - 2 };
    SDL_RenderDrawRect(m_renderer, &box2);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    int cx = boxX + boxW / 2;
    m_theme->drawTextCentered(m_confirmMessage,
        cx, boxY + 24, pal.textPrimary, FontSize::BODY);
    if (!m_confirmDetail.empty()) {
        m_theme->drawTextCentered(m_confirmDetail,
            cx, boxY + 58, pal.textSecond, FontSize::SMALL);
    }

    int hintY      = boxY + boxH - 44;
    int hintSpacing = 140;
    int hintCX     = cx - hintSpacing / 2;
    m_theme->drawButtonHint(hintCX - 60, hintY, "A", "Confirm", borderClr);
    m_theme->drawButtonHint(hintCX + 60, hintY, "B", "Cancel",  pal.textSecond);
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

    SDL_Color accent = m_theme->palette().accent;
    if (m_focus == OmniPanel::MEMCARD) {
        SDL_SetRenderDrawColor(m_renderer, accent.r, accent.g, accent.b, 120);
        SDL_Rect glow = { x + DIVIDER_W, y, 3, h };
        SDL_RenderFillRect(m_renderer, &glow);
    } else {
        SDL_SetRenderDrawColor(m_renderer, accent.r, accent.g, accent.b, 120);
        SDL_Rect glow = { x - 3, y, 3, h };
        SDL_RenderFillRect(m_renderer, &glow);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderMemCardPanel  (unchanged from original)
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderMemCardPanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::MEMCARD);

    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("MEMORY CARD", x + MARGIN, y + 12, labelCol, FontSize::SMALL);

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

    if (m_cardSel < m_cardScroll)
        m_cardScroll = m_cardSel;
    if (m_cardSel >= m_cardScroll + visRows)
        m_cardScroll = m_cardSel - visRows + 1;

    int endIdx = std::min((int)m_cardEntries.size(), m_cardScroll + visRows);
    for (int i = m_cardScroll; i < endIdx; ++i) {
        const auto& entry = m_cardEntries[i];
        bool sel = focused && (i == m_cardSel);

        SDL_Rect rowRect = { x + 4, rowY, w - 8, rowH - 4 };
        if (sel) {
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCardHover.r, pal.bgCardHover.g, pal.bgCardHover.b, 200);
            SDL_RenderFillRect(m_renderer, &rowRect);
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { x + 4, rowY, 3, rowH - 4 };
            SDL_RenderFillRect(m_renderer, &border);
        }

        int frame = std::min(m_iconFrame, entry.frameCount - 1);
        SDL_Texture* iconTex = entry.iconTextures[frame];
        SDL_Rect iconDst = { x + MARGIN, rowY + (rowH - ICON_SIZE) / 2,
                             ICON_SIZE, ICON_SIZE };
        if (iconTex) {
            SDL_SetTextureBlendMode(iconTex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(m_renderer, iconTex, nullptr, &iconDst);

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 40);
            int pixelH = ICON_SIZE / MCR_ICON_H;
            for (int scanY = iconDst.y; scanY < iconDst.y + ICON_SIZE; scanY += pixelH) {
                SDL_Rect scanline = { iconDst.x, scanY, ICON_SIZE, 1 };
                SDL_RenderFillRect(m_renderer, &scanline);
            }
        } else {
            SDL_SetRenderDrawColor(m_renderer, 80, 80, 120, 255);
            SDL_RenderFillRect(m_renderer, &iconDst);
            m_theme->drawTextCentered("?",
                iconDst.x + ICON_SIZE / 2,
                iconDst.y + ICON_SIZE / 2 - 8,
                pal.textDisable, FontSize::SMALL);
        }

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

    if ((int)m_cardEntries.size() > visRows) {
        int sbH  = (h - 62);
        int sbX  = x + w - 6;
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

// ─────────────────────────────────────────────────────────────────────────────
//  renderSaveStatePanel  (unchanged from original)
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderSaveStatePanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::SAVESTATES);

    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("SAVE STATES", x + MARGIN, y + 12, labelCol, FontSize::SMALL);

    int cardW   = STATE_CARD_W;
    int cardH   = STATE_CARD_H;
    int padX    = 14;
    int padY    = 14;
    int gridX   = x + MARGIN;
    int gridY   = y + 54;
    int cols    = std::max(1, (w - MARGIN * 2 + padX) / (cardW + padX));
    int visRows = std::max(1, (h - 54) / (cardH + padY));

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

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = sel ? pal.bgCardHover : pal.bgCard;
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 230);
        SDL_RenderFillRect(m_renderer, &card);

        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { cx - 2, cy - 2, cardW + 4, cardH + 4 };
            SDL_RenderDrawRect(m_renderer, &border);
        }

        if (isNew) {
            m_theme->drawTextCentered("+", cx + cardW / 2, cy + cardH / 2 - 20,
                                      sel ? pal.accent : pal.textDisable,
                                      FontSize::HEADER);
            m_theme->drawTextCentered("New Save",
                                      cx + cardW / 2, cy + cardH - 26,
                                      pal.textDisable, FontSize::TINY);
        } else if (slot.exists && i < (int)m_thumbTex.size() && m_thumbTex[i]) {
            SDL_Rect thumbDst = { cx, cy, cardW, cardH - 36 };
            SDL_RenderCopy(m_renderer, m_thumbTex[i], nullptr, &thumbDst);

            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 220);
            SDL_Rect strip = { cx, cy + cardH - 36, cardW, 36 };
            SDL_RenderFillRect(m_renderer, &strip);

            std::string label = (slot.slotNumber == -1) ? "Auto" :
                                 "Slot " + std::to_string(slot.slotNumber + 1);
            m_theme->drawText(label, cx + 6, cy + cardH - 32,
                              pal.textPrimary, FontSize::TINY);
            if (!slot.timestamp.empty()) {
                m_theme->drawText(slot.timestamp, cx + 6, cy + cardH - 16,
                                  pal.textDisable, FontSize::TINY);
            }
        } else {
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

// ─────────────────────────────────────────────────────────────────────────────
//  renderTimeMachinePanel
//
//  Full-screen takeover. Each row shows:
//    [game thumb 128×72] | [timestamp] [save count] | [SpriteCard mini-icons]
//
//  Header: "CARD TIME MACHINE — <SERIAL>_1 — N snapshots (oldest: ...)"
//  Footer: [A] Restore  [B] Back
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderTimeMachinePanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();

    // ── Panel header ──────────────────────────────────────────────────────────
    // Purple accent for Time Machine — visually distinct from normal panels
    SDL_Color tmAccent = { 160, 100, 255, 255 };
    m_theme->drawText("CARD TIME MACHINE", x + MARGIN, y + 12, tmAccent, FontSize::SMALL);

    if (!m_gameSerial.empty()) {
        std::string cardLabel = m_gameSerial + "_1";
        m_theme->drawText(cardLabel, x + MARGIN + 190, y + 12,
                          pal.textDisable, FontSize::SMALL);
    }

    // Retention info line
    if (!m_snapshots.empty()) {
        const auto& oldest = m_snapshots.back();
        std::string info = std::to_string(m_snapshots.size())
                         + " snapshot" + (m_snapshots.size() == 1 ? "" : "s")
                         + "  \xe2\x80\x94  oldest: " + oldest.timestamp;
        m_theme->drawText(info, x + MARGIN, y + 34, pal.textDisable, FontSize::TINY);
    }

    int rowY    = y + 62;
    int rowH    = TM_ROW_H;
    int visRows = std::max(1, (h - 62) / rowH);

    // ── Empty state ───────────────────────────────────────────────────────────
    if (m_snapshots.empty()) {
        int midY = y + h / 2 - 30;
        m_theme->drawTextCentered("No card history yet.",
            x + w / 2, midY, pal.textDisable, FontSize::BODY);
        m_theme->drawTextCentered("Snapshots are created automatically each time",
            x + w / 2, midY + 28, pal.textDisable, FontSize::TINY);
        m_theme->drawTextCentered("the game saves to the memory card (LiveCard detection).",
            x + w / 2, midY + 46, pal.textDisable, FontSize::TINY);
        return;
    }

    // Clamp scroll
    if (m_snapSel < m_snapScroll)
        m_snapScroll = m_snapSel;
    if (m_snapSel >= m_snapScroll + visRows)
        m_snapScroll = m_snapSel - visRows + 1;

    int endIdx = std::min((int)m_snapshots.size(), m_snapScroll + visRows);

    for (int i = m_snapScroll; i < endIdx; ++i) {
        const TimeMachineEntry& snap = m_snapshots[i];
        bool sel = (i == m_snapSel);

        // ── Row background ─────────────────────────────────────────────────────
        SDL_Rect rowRect = { x + 4, rowY + 2, w - 8, rowH - 6 };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCardHover.r, pal.bgCardHover.g, pal.bgCardHover.b, 220);
            SDL_RenderFillRect(m_renderer, &rowRect);
            // Purple accent left border for Time Machine selection
            SDL_SetRenderDrawColor(m_renderer, 160, 100, 255, 255);
            SDL_Rect border = { x + 4, rowY + 2, 3, rowH - 6 };
            SDL_RenderFillRect(m_renderer, &border);
        } else {
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 100);
            SDL_RenderFillRect(m_renderer, &rowRect);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        // Row separator
        if (i > m_snapScroll) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 60);
            SDL_RenderDrawLine(m_renderer, x + MARGIN, rowY, x + w - MARGIN, rowY);
        }

        int colX = x + MARGIN;
        int midRowY = rowY + rowH / 2;

        // ── Game frame thumbnail ───────────────────────────────────────────────
        int thumbY = rowY + (rowH - TM_THUMB_H) / 2;
        SDL_Rect thumbDst = { colX, thumbY, TM_THUMB_W, TM_THUMB_H };
        if (snap.thumbTex) {
            SDL_RenderCopy(m_renderer, snap.thumbTex, nullptr, &thumbDst);
            SDL_SetRenderDrawColor(m_renderer,
                pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 160);
            SDL_RenderDrawRect(m_renderer, &thumbDst);
        } else {
            SDL_SetRenderDrawColor(m_renderer, 30, 30, 50, 220);
            SDL_RenderFillRect(m_renderer, &thumbDst);
            SDL_SetRenderDrawColor(m_renderer, 60, 60, 90, 255);
            SDL_RenderDrawRect(m_renderer, &thumbDst);
            m_theme->drawTextCentered("No preview",
                colX + TM_THUMB_W / 2, thumbY + TM_THUMB_H / 2 - 6,
                pal.textDisable, FontSize::TINY);
        }
        colX += TM_THUMB_W + 16;

        // ── Text column ───────────────────────────────────────────────────────
        SDL_Color tsCol = sel ? pal.textPrimary : pal.textSecond;
        m_theme->drawText(snap.timestamp, colX, rowY + 14, tsCol, FontSize::BODY);

        if (!snap.entries.empty()) {
            std::string saveInfo = std::to_string(snap.entries.size())
                + " save" + (snap.entries.size() == 1 ? "" : "s") + " on card";
            m_theme->drawText(saveInfo, colX, rowY + 40, pal.textDisable, FontSize::TINY);
        } else {
            m_theme->drawText("Empty card", colX, rowY + 40, pal.textDisable, FontSize::TINY);
        }
        colX += 160;

        // ── SpriteCard mini-icons from this snapshot ───────────────────────────
        // Up to 4 icons shown as a small strip
        int iconY = rowY + (rowH - TM_ICON_SIZE) / 2;
        int maxIcons = std::min((int)snap.entries.size(), 4);
        for (int ei = 0; ei < maxIcons; ++ei) {
            const MemCardEntry& entry = snap.entries[ei];
            int frame = std::min(m_iconFrame, std::max(0, entry.frameCount - 1));
            SDL_Texture* iconTex = (frame >= 0 && frame < 3)
                ? entry.iconTextures[frame] : nullptr;

            SDL_Rect iconDst = { colX, iconY, TM_ICON_SIZE, TM_ICON_SIZE };
            if (iconTex) {
                SDL_SetTextureBlendMode(iconTex, SDL_BLENDMODE_BLEND);
                SDL_RenderCopy(m_renderer, iconTex, nullptr, &iconDst);
                // CRT scanline overlay on mini-icons too
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 30);
                int pixH = std::max(1, TM_ICON_SIZE / MCR_ICON_H);
                for (int scanY = iconDst.y; scanY < iconDst.y + TM_ICON_SIZE; scanY += pixH) {
                    SDL_Rect scanline = { iconDst.x, scanY, TM_ICON_SIZE, 1 };
                    SDL_RenderFillRect(m_renderer, &scanline);
                }
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            } else {
                SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 200);
                SDL_RenderFillRect(m_renderer, &iconDst);
            }
            colX += TM_ICON_SIZE + 6;
        }

        // "NEWEST" badge on the first row only
        if (i == 0) {
            SDL_Color gold = { 210, 170, 50, 255 };
            m_theme->drawText("NEWEST", x + w - MARGIN - 64, rowY + 14,
                              gold, FontSize::TINY);
        }

        rowY += rowH;
    }

    // ── Scrollbar ─────────────────────────────────────────────────────────────
    if ((int)m_snapshots.size() > visRows) {
        int sbH  = h - 62;
        int sbX  = x + w - 6;
        float frac = (float)visRows / (float)m_snapshots.size();
        float top  = (float)m_snapScroll / (float)m_snapshots.size();
        int thumbH = std::max(20, (int)(frac * sbH));
        int thumbY = y + 62 + (int)(top * sbH);
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 255);
        SDL_Rect track = { sbX, y + 62, 4, sbH };
        SDL_RenderFillRect(m_renderer, &track);
        SDL_SetRenderDrawColor(m_renderer,
            pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 200);
        SDL_Rect thumb = { sbX, thumbY, 4, thumbH };
        SDL_RenderFillRect(m_renderer, &thumb);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderFooter
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderFooter() {
    if (m_focus == OmniPanel::TIMEMACHINE) {
        m_theme->drawFooterHints(m_w, m_h,
            "Restore Snapshot",  // A
            "Back",              // B
            "",                  // X
            "");                 // Y
        return;
    }
    if (m_focus == OmniPanel::SAVESTATES) {
        m_theme->drawFooterHints(m_w, m_h,
            "Load / New Save",
            "Back",
            "Save Here",
            "");
    } else {
        // MEMCARD panel
        m_theme->drawFooterHints(m_w, m_h,
            "Reload Card",
            "Back",
            "Delete Save",
            "History");   // Y = open Time Machine
    }

    int footY = m_h - m_theme->layout().footerH;
    int cy    = footY + (m_theme->layout().footerH - 34) / 2;
    m_theme->drawButtonHint(m_w - 220, cy, "L1/R1",
                            "Switch Panel", m_theme->palette().textSecond);

    if (m_focus == OmniPanel::SAVESTATES) {
        m_theme->drawButtonHint(m_w - 390, cy, "Start",
                                "Delete", m_theme->palette().textSecond);
    }
}
