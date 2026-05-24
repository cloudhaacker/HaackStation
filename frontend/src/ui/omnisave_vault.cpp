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

static constexpr uint32_t ALLOC_FIRST = 0x51;
static constexpr uint32_t ALLOC_FREE  = 0xA0;

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
	
    m_importScreen = std::make_unique<OmniSaveImport>(
        m_memCards, m_renderer);

    // Ensure import directories exist
    fs::create_directories("memcards/import");
    fs::create_directories("memcards/import/done");	
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

    m_cardSel        = 0;  m_cardScroll  = 0;
    m_stateSel       = 0;  m_stateScroll = 0;
    m_snapSel        = 0;  m_snapScroll  = 0;
    m_iconAnimMs     = 0.f; m_iconFrame  = 0;
    m_cardSlotSel    = 0;
    m_cardSlotFocus  = false;
    m_pendingSwapPath.clear();

    loadGameCardSlots();
    loadMemCardEntries();
    loadSaveSlots();

    std::cout << "[OmniSave] Opened for: " << gameTitle
              << "  card entries=" << m_cardEntries.size()
              << "  state slots=" << m_slots.size() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeMemCardTextures
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

// ─────────────────────────────────────────────────────────────────────────────
//  freeSaveSlotTextures
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::freeSaveSlotTextures() {
    for (auto* t : m_thumbTex) {
        if (t) SDL_DestroyTexture(t);
    }
    m_thumbTex.clear();
    m_slots.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  freeTimeMachineTextures
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  loadGameCardSlots
//  Enumerates memcards/per_game/ for .mcr files whose name starts with the
//  current game's serial (e.g. "SCUS-94900_1.mcr", "SCUS-94900_2.mcr").
//  Produces a sorted, friendly-named list for the card switcher strip.
//  m_activeCardPath is used to mark which slot is currently live.
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::loadGameCardSlots() {
    m_gameCards.clear();
    if (m_gameSerial.empty() || !m_memCards) return;

    std::string cardDir = "memcards/per_game/";
    std::string prefix  = m_gameSerial + "_";   // e.g. "SCUS-94900_"
    std::error_code ec;
    if (!fs::exists(cardDir, ec)) return;

    std::string absActive = m_activeCardPath.empty()
        ? "" : fs::absolute(fs::path(m_activeCardPath)).string();

    std::vector<GameCardSlot> slots;
    for (auto& entry : fs::directory_iterator(cardDir, ec)) {
        if (entry.path().extension() != ".mcr") continue;
        std::string stem = entry.path().stem().string();
        // Must start with SERIAL_ and have a numeric suffix
        if (stem.rfind(prefix, 0) != 0) continue;
        std::string suffix = stem.substr(prefix.size());
        bool isNum = !suffix.empty() &&
                     std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        if (!isNum) continue;

        int slotNum = std::stoi(suffix);
        std::string absPath = fs::absolute(entry.path()).string();

        GameCardSlot slot;
        slot.path        = absPath;
        slot.slotNumber  = slotNum;
        slot.isActive    = (!absActive.empty() && absPath == absActive);
        slot.displayName = m_gameTitle.empty()
            ? (m_gameSerial + " — Card " + suffix)
            : (m_gameTitle  + " — Card " + suffix);
        slots.push_back(slot);
    }

    std::sort(slots.begin(), slots.end(),
        [](const GameCardSlot& a, const GameCardSlot& b) {
            return a.slotNumber < b.slotNumber;
        });

    m_gameCards = std::move(slots);

    // Pre-select the active card in the switcher
    m_cardSlotSel = 0;
    for (int i = 0; i < (int)m_gameCards.size(); ++i) {
        if (m_gameCards[i].isActive) { m_cardSlotSel = i; break; }
    }

    std::cout << "[OmniSave] Card slots for " << m_gameSerial
              << ": " << m_gameCards.size() << " found\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  loadMemCardEntries
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  loadSaveSlots
// ─────────────────────────────────────────────────────────────────────────────
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
//  Scans memcards/history/<SERIAL>_1/ for .mcr snapshots. Pairs each with a
//  same-stem .png if one exists. Parses SpriteCard icons from each .mcr.
//  Sorted newest-first (reverse lexicographic on timestamp filename stems).
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

    std::sort(mcrFiles.begin(), mcrFiles.end());
    std::reverse(mcrFiles.begin(), mcrFiles.end());  // newest-first

    for (const auto& mcrPath : mcrFiles) {
        TimeMachineEntry snap;
        snap.mcrPath     = mcrPath.string();
        snap.rawFilename = mcrPath.stem().string();
        snap.timestamp   = formatSnapshotTimestamp(snap.rawFilename);
        snap.relativeAge = formatRelativeAge(snap.rawFilename);

        fs::path pngPath = mcrPath;
        pngPath.replace_extension(".png");
        if (fs::exists(pngPath))
            snap.pngPath = pngPath.string();

        snap.entries = parseMcr(snap.mcrPath);
        for (auto& e : snap.entries) {
            for (int f = 0; f < e.frameCount; ++f) {
                if (!e.iconFrames[f].empty())
                    e.iconTextures[f] = buildIconTexture(e.iconFrames[f]);
            }
        }

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
//  "2026-05-08_21-04-33" → "May 08, 2026  21:04"
//  Full date + time so there is never any ambiguity between entries.
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveVault::formatSnapshotTimestamp(const std::string& stem) {
    if (stem.size() < 19) return stem;
    int year = 0, month = 0, day = 0, hour = 0, min = 0;
    try {
        year  = std::stoi(stem.substr(0, 4));
        month = std::stoi(stem.substr(5, 2));
        day   = std::stoi(stem.substr(8, 2));
        hour  = std::stoi(stem.substr(11, 2));
        min   = std::stoi(stem.substr(14, 2));
    } catch (...) { return stem; }

    static const char* months[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (month < 1 || month > 12) return stem;

    char buf[40];
    std::snprintf(buf, sizeof(buf), "%s %02d, %04d  %02d:%02d",
                  months[month], day, year, hour, min);
    return std::string(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  formatRelativeAge
//  "2026-05-08_21-04-33" → "Today", "Yesterday", "3 days ago", "2 weeks ago"…
//  Gives instant at-a-glance context without replacing the full timestamp.
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveVault::formatRelativeAge(const std::string& stem) {
    if (stem.size() < 10) return "";
    int year = 0, month = 0, day = 0;
    try {
        year  = std::stoi(stem.substr(0, 4));
        month = std::stoi(stem.substr(5, 2));
        day   = std::stoi(stem.substr(8, 2));
    } catch (...) { return ""; }

    time_t now = time(nullptr);
    tm* t = localtime(&now);
    int todayYear  = t->tm_year + 1900;
    int todayMonth = t->tm_mon + 1;
    int todayDay   = t->tm_mday;

    // Approximate day ordinal — accurate enough for relative display
    auto ordinal = [](int y, int m, int d) { return y * 365 + m * 30 + d; };
    int diff = ordinal(todayYear, todayMonth, todayDay)
             - ordinal(year, month, day);

    if (diff <= 0)  return "Today";
    if (diff == 1)  return "Yesterday";
    if (diff < 7)   return std::to_string(diff) + " days ago";
    if (diff < 14)  return "1 week ago";
    if (diff < 30)  return std::to_string(diff / 7) + " weeks ago";
    if (diff < 60)  return "1 month ago";
    if (diff < 365) return std::to_string(diff / 30) + " months ago";
    if (diff < 730) return "1 year ago";
    return std::to_string(diff / 365) + " years ago";
}

// ─────────────────────────────────────────────────────────────────────────────
//  parseMcr
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
        if (frame[0] != ALLOC_FIRST) continue;

        uint32_t fileBytes = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8)
                           | ((uint32_t)frame[6] << 16) | ((uint32_t)frame[7] << 24);
        int blocksUsed = (int)std::max(1u, (fileBytes + MCR_BLOCK_SIZE - 1) / MCR_BLOCK_SIZE);

        char productBuf[13] = {}; std::memcpy(productBuf, frame + 10, 12);
        char identBuf[9]    = {}; std::memcpy(identBuf,   frame + 22, 8);
        std::string title = decodeShiftJis(frame + 64, 64);

        size_t dataBlockOff = (size_t)(block + 1) * MCR_BLOCK_SIZE;
        if (dataBlockOff + 512 > fileSize) continue;

        const uint8_t* blk = data.data() + dataBlockOff;
        if (blk[0] != 'S' || blk[1] != 'C') continue;

        uint8_t iconFlag   = blk[2];
        int     frameCount = 1;
        if      (iconFlag == 0x12) frameCount = 2;
        else if (iconFlag == 0x13) frameCount = 3;

        uint32_t palette[16] = {};
        for (int c = 0; c < 16; ++c) {
            uint16_t raw = (uint16_t)blk[MCR_ICON_PAL_OFF + c*2]
                         | ((uint16_t)blk[MCR_ICON_PAL_OFF + c*2 + 1] << 8);
            uint8_t r5 = (raw>>0)&0x1F, g5 = (raw>>5)&0x1F, b5 = (raw>>10)&0x1F;
            uint8_t stp = (raw>>15)&0x01;
            uint8_t r = (r5<<3)|(r5>>2), g = (g5<<3)|(g5>>2), b = (b5<<3)|(b5>>2);
            uint8_t a = (r5==0 && g5==0 && b5==0) ? (stp==0 ? 0 : 255) : 255;
            palette[c] = ((uint32_t)r<<24)|((uint32_t)g<<16)|((uint32_t)b<<8)|(uint32_t)a;
        }

        MemCardEntry entry;
        entry.productCode = std::string(productBuf);
        entry.identifier  = std::string(identBuf);

        std::string cleanTitle = title;
        if (!cleanTitle.empty() && !entry.productCode.empty()) {
            std::string baPrefix = "BA" + entry.productCode;
            if (cleanTitle.size() > baPrefix.size() &&
                cleanTitle.compare(0, baPrefix.size(), baPrefix) == 0)
                cleanTitle = cleanTitle.substr(baPrefix.size());
            else if (cleanTitle.size() > entry.productCode.size() &&
                     cleanTitle.compare(0, entry.productCode.size(), entry.productCode) == 0)
                cleanTitle = cleanTitle.substr(entry.productCode.size());
        }
        size_t fc = cleanTitle.find_first_not_of(' ');
        if (fc != std::string::npos) cleanTitle = cleanTitle.substr(fc);

        entry.title      = !cleanTitle.empty() ? cleanTitle
                         : !m_gameTitle.empty() ? m_gameTitle
                         : entry.productCode;
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
                rgba[px]     = palette[byte & 0x0F];
                rgba[px + 1] = palette[(byte >> 4) & 0x0F];
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
        if (b < 0x80) { out += (char)b; ++i; }
        else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            if (i + 1 >= len) break;
            uint16_t code = ((uint16_t)b << 8) | data[i+1];
            if      (code == 0x8140)                       out += ' ';
            else if (code >= 0x8260 && code <= 0x8279)    out += (char)('A' + (code - 0x8260));
            else if (code >= 0x8281 && code <= 0x8299)    out += (char)('a' + (code - 0x8281));
            else if (code >= 0x824F && code <= 0x8258)    out += (char)('0' + (code - 0x824F));
            else                                           out += '?';
            i += 2;
        } else if (b >= 0xA1 && b <= 0xDF) { out += (char)b; ++i; }
        else { out += '?'; ++i; }
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
        (void*)rgba.data(), MCR_ICON_W, MCR_ICON_H, 32, MCR_ICON_W * 4,
        0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
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
	
    // Delegate to import screen while it's open
    if (m_importPhase == ImportPhase::IMPORTING) {
        m_importScreen->update(deltaMs);
        if (!m_importScreen->isOpen()) {
            bool didImport = m_importScreen->needsVaultRefresh();

            if (didImport) {
                loadMemCardEntries();
                // Reload the core's SRAM buffer from disk so it picks up
                // the imported save. The flush already happened in
                // openImportScreen() before importBlock wrote the file.
                if (m_sramReload) m_sramReload();
            }

            // Move source file to import/done/
            if (!m_importPending.empty()) {
                moveToImportDone(m_importPending);
                m_importPending.clear();
            }
            m_importPhase = ImportPhase::NONE;
            m_importBannerVisible = false;
            m_importBannerAlpha   = 0.f;
        }
        return;  // don't run normal vault update while import screen is open
    }

    // Scan for new import files (once per second)
    m_importScanTimer += deltaMs;
    if (m_importScanTimer >= 1000.f) {
        m_importScanTimer = 0.f;
        scanImportFolder();
    }

    // Banner alpha animation
    if (m_importBannerVisible) {
        m_importBannerAlpha = std::min(255.f, m_importBannerAlpha + deltaMs * 0.4f);
    } else {
        m_importBannerAlpha = std::max(0.f, m_importBannerAlpha - deltaMs * 0.4f);
    }	
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleEvent
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleEvent(const SDL_Event& e) {
    NavAction a = m_nav->processEvent(e);
    if (a == NavAction::NONE) a = m_nav->updateHeld(SDL_GetTicks());

    if (m_confirmAction != ConfirmAction::NONE) {
        if (a == NavAction::CONFIRM) {
            ConfirmAction action = m_confirmAction;
            m_confirmAction = ConfirmAction::NONE;
            if      (action == ConfirmAction::LOAD_STATE)       doLoadAction();
            else if (action == ConfirmAction::DELETE_STATE)     doDeleteState();
            else if (action == ConfirmAction::OVERWRITE_STATE)  doSaveAction();
            else if (action == ConfirmAction::DELETE_ENTRY)     doDeleteEntry();
            else if (action == ConfirmAction::RESTORE_SNAPSHOT) doRestoreSnapshot();
            else if (action == ConfirmAction::COPY_STATE)       doBranchState();
            else if (action == ConfirmAction::RELOAD_CARD) {
                m_wantsCardReload = true;
                m_wantsClose      = true;
            }
            else if (action == ConfirmAction::SWAP_CARD) {
                doSwapCard();
            }
        } else if (a == NavAction::BACK) {
            m_confirmAction = ConfirmAction::NONE;
        }
        return;
    }
	
    // Delegate to import screen while it's open
    if (m_importPhase == ImportPhase::IMPORTING) {
        m_importScreen->handleInput(e);
        return;
    }

    // Banner input: [X] / Square opens import screen
    handleImportInput(e);	
	

    // Time Machine owns its own BACK — returns to MEMCARD, does not close vault
    if (m_focus == OmniPanel::CHRONICLE) {
        handleTimeMachineNav(a);
        return;
    }

    if (a == NavAction::BACK)        { m_wantsClose = true;              return; }
    if (a == NavAction::SHOULDER_L)  { m_focus = OmniPanel::MEMCARD;    return; }
    if (a == NavAction::SHOULDER_R)  { m_focus = OmniPanel::SAVESTATES; return; }
    if (a == NavAction::PAGE_UP)     { m_focus = OmniPanel::MEMCARD;    return; }
    if (a == NavAction::PAGE_DOWN)   { m_focus = OmniPanel::SAVESTATES; return; }

    if (m_focus == OmniPanel::MEMCARD) handleMemCardNav(a);
    else                                handleSaveStateNav(a);
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleMemCardNav
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleMemCardNav(NavAction a) {
    int slotCount  = (int)m_gameCards.size();
    int entryCount = (int)m_cardEntries.size();

    // ── Card slot switcher strip (top of panel) ───────────────────────────────
    // L/R navigates between available cards for this game.
    // A on a non-active card → confirm swap.
    // A on the active card   → confirm reload.
    if (a == NavAction::LEFT && slotCount > 1) {
        m_cardSlotSel = (m_cardSlotSel - 1 + slotCount) % slotCount;
        return;
    }
    if (a == NavAction::RIGHT && slotCount > 1) {
        m_cardSlotSel = (m_cardSlotSel + 1) % slotCount;
        return;
    }

    // UP/DOWN navigates the save entry list below the strip
    if (a == NavAction::UP)   { m_cardSel = std::max(0, m_cardSel - 1); return; }
    if (a == NavAction::DOWN) { m_cardSel = std::min(std::max(0, entryCount-1), m_cardSel+1); return; }

    // Y / Triangle → open Card Chronicle
    if (a == NavAction::FAVORITE) {
        loadTimeMachineEntries();
        m_snapSel    = 0;
        m_snapScroll = 0;
        m_focus      = OmniPanel::CHRONICLE;
        return;
    }

    // A → swap/reload depending on which card is highlighted in the switcher
    if (a == NavAction::CONFIRM) {
        if (slotCount > 0) {
            const GameCardSlot& sel = m_gameCards[m_cardSlotSel];
            if (sel.isActive) {
                m_confirmAction  = ConfirmAction::RELOAD_CARD;
                m_confirmMessage = "Reload memory card from disk?";
                m_confirmDetail  = "Unsaved in-game changes will be lost.";
            } else {
                m_confirmAction  = ConfirmAction::SWAP_CARD;
                m_confirmMessage = "Swap to " + sel.displayName + "?";
                m_confirmDetail  = "Current card is flushed to disk first.  "
                                   "Soft reset recommended after swap.";
            }
        }
        return;
    }

    // Options → delete entry from active card
    if (a == NavAction::OPTIONS && entryCount > 0) {
        const std::string& title = m_cardEntries[m_cardSel].title;
        int blocks = m_cardEntries[m_cardSel].blocksUsed;
        m_confirmAction  = ConfirmAction::DELETE_ENTRY;
        m_confirmMessage = "Delete this memory card save?";
        m_confirmDetail  = title + "  \xe2\x80\x94  "
                         + std::to_string(blocks)
                         + (blocks == 1 ? " block" : " blocks")
                         + "  \xe2\x80\x94  This cannot be undone.";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleSaveStateNav
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleSaveStateNav(NavAction a) {
    int count = (int)m_slots.size();
    if (count == 0) return;

    if (a == NavAction::UP)    { m_stateSel = std::max(0, m_stateSel - STATE_COLS);         return; }
    if (a == NavAction::DOWN)  { m_stateSel = std::min(count-1, m_stateSel + STATE_COLS);   return; }
    if (a == NavAction::LEFT)  { m_stateSel = std::max(0, m_stateSel - 1);                  return; }
    if (a == NavAction::RIGHT) { m_stateSel = std::min(count-1, m_stateSel + 1);            return; }

    if (a == NavAction::CONFIRM) {
        const SaveSlot& sel = m_slots[m_stateSel];
        if (sel.slotNumber == -2 || !sel.exists) {
            doSaveAction();
        } else {
            std::string label = (sel.slotNumber == -1) ? "Auto-save"
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
            std::string label = (sel.slotNumber == -1) ? "Auto-save"
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
        std::string label = (sel.slotNumber == -1) ? "Auto-save"
            : "Slot " + std::to_string(sel.slotNumber + 1);
        m_confirmAction  = ConfirmAction::DELETE_STATE;
        m_confirmMessage = "Delete this save?";
        m_confirmDetail  = label + "  \xe2\x80\x94  This cannot be undone.";
        return;
    }

    // Y / Triangle → branch (copy slot to a new slot)
    if (a == NavAction::FAVORITE) {
        const SaveSlot& sel = m_slots[m_stateSel];
        if (!sel.exists || sel.slotNumber == -2) return;
        std::string label = (sel.slotNumber == -1) ? "Auto-save"
            : "Slot " + std::to_string(sel.slotNumber + 1);
        m_branchSourceSlot = sel.slotNumber;
        m_confirmAction  = ConfirmAction::COPY_STATE;
        m_confirmMessage = "Branch from this point?";
        m_confirmDetail  = "Creates a copy of " + label + " as a new slot.";
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleTimeMachineNav
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::handleTimeMachineNav(NavAction a) {
    if (a == NavAction::BACK) { m_focus = OmniPanel::MEMCARD; return; }
    // (CHRONICLE panel — formerly TimeMachine)

    int count = (int)m_snapshots.size();
    if (count == 0) return;

    if (a == NavAction::UP)   { m_snapSel = std::max(0, m_snapSel - 1);         return; }
    if (a == NavAction::DOWN) { m_snapSel = std::min(count-1, m_snapSel + 1);   return; }

    if (a == NavAction::CONFIRM) {
        const TimeMachineEntry& snap = m_snapshots[m_snapSel];
        m_confirmAction  = ConfirmAction::RESTORE_SNAPSHOT;
        m_confirmMessage = "Restore this card snapshot?";
        m_confirmDetail  = snap.timestamp + "  (" + snap.relativeAge + ")";
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
        for (const auto& s : m_slots)
            if (s.slotNumber >= 0) maxSlot = std::max(maxSlot, s.slotNumber + 1);
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
    // Also clean up the .branch sidecar if one exists
    if (!sel.statePath.empty()) {
        fs::path branchFile = fs::path(sel.statePath);
        branchFile.replace_extension(".branch");
        std::error_code ec;
        if (fs::exists(branchFile, ec)) fs::remove(branchFile, ec);
    }
    std::cout << "[OmniSave] Deleted slot " << sel.slotNumber << "\n";
    if (m_thumbTex[m_stateSel]) {
        SDL_DestroyTexture(m_thumbTex[m_stateSel]);
        m_thumbTex[m_stateSel] = nullptr;
    }
    loadSaveSlots();
    m_stateSel = std::min(m_stateSel, (int)m_slots.size() - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  readBranchLabel
//  Checks for a .branch sidecar alongside a .state file and returns its
//  content (e.g. "Branch of Slot 2"). Returns empty string if none exists.
// ─────────────────────────────────────────────────────────────────────────────
static std::string readBranchLabel(const std::string& statePath) {
    if (statePath.empty()) return "";
    fs::path branchPath = fs::path(statePath);
    branchPath.replace_extension(".branch");
    std::error_code ec;
    if (!fs::exists(branchPath, ec)) return "";
    std::ifstream f(branchPath.string());
    if (!f.is_open()) return "";
    std::string line;
    std::getline(f, line);
    return line;
}

// ─────────────────────────────────────────────────────────────────────────────
//  doBranchState
//  Copies slot N to the next available slot. Also copies the paired .png
//  thumbnail. Writes a .branch sidecar ("Branch of Slot N") so the UI can
//  display the lineage. After copy, refreshes the slot list and moves the
//  cursor to the new slot so users can immediately see what was created.
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doBranchState() {
    if (!m_saves || m_branchSourceSlot < -1) return;

    // Find the source slot entry so we have its resolved paths
    const SaveSlot* srcSlot = nullptr;
    for (const auto& s : m_slots) {
        if (s.slotNumber == m_branchSourceSlot) { srcSlot = &s; break; }
    }
    if (!srcSlot || !srcSlot->exists || srcSlot->statePath.empty()) {
        std::cerr << "[OmniSave] Branch failed — source slot not found\n";
        return;
    }
    if (!fs::exists(srcSlot->statePath)) {
        std::cerr << "[OmniSave] Branch failed — source file missing: "
                  << srcSlot->statePath << "\n";
        return;
    }

    // Find the next free slot number (highest existing + 1, skip auto -1)
    int newSlot = 0;
    for (const auto& s : m_slots)
        if (s.slotNumber >= 0) newSlot = std::max(newSlot, s.slotNumber + 1);

    // Build destination paths using SaveStateManager's own path resolver
    SaveSlot dstSlot = m_saves->getSlot(newSlot);

    // Atomic copy of .state
    std::string tmpPath = dstSlot.statePath + ".tmp";
    {
        std::ifstream src(srcSlot->statePath, std::ios::binary);
        std::ofstream dst(tmpPath, std::ios::binary | std::ios::trunc);
        if (!src.is_open() || !dst.is_open()) {
            std::cerr << "[OmniSave] Branch failed — cannot open files for copy\n";
            return;
        }
        dst << src.rdbuf();
        dst.flush();
    }
    std::error_code ec;
    fs::rename(tmpPath, dstSlot.statePath, ec);
    if (ec) {
        std::cerr << "[OmniSave] Branch rename failed: " << ec.message() << "\n";
        fs::remove(tmpPath, ec);
        return;
    }

    // Copy .png thumbnail (non-fatal if missing)
    if (!srcSlot->thumbPath.empty() && fs::exists(srcSlot->thumbPath)) {
        std::error_code cpec;
        fs::copy_file(srcSlot->thumbPath, dstSlot.thumbPath,
                      fs::copy_options::overwrite_existing, cpec);
        if (cpec)
            std::cerr << "[OmniSave] Branch: thumbnail copy failed (non-fatal): "
                      << cpec.message() << "\n";
    }

    // Write .branch sidecar with lineage label
    std::string sourceLabel = (m_branchSourceSlot == -1) ? "Auto-save"
        : "Slot " + std::to_string(m_branchSourceSlot + 1);
    fs::path branchFile = fs::path(dstSlot.statePath);
    branchFile.replace_extension(".branch");
    {
        std::ofstream bf(branchFile.string());
        if (bf.is_open()) bf << "Branch of " << sourceLabel << "\n";
    }

    std::cout << "[OmniSave] Branched slot " << m_branchSourceSlot
              << " → slot " << newSlot
              << " (\"" << sourceLabel << "\" → Slot " << newSlot + 1 << ")\n";

    // Refresh and move cursor to the new slot
    loadSaveSlots();
    for (int i = 0; i < (int)m_slots.size(); ++i) {
        if (m_slots[i].slotNumber == newSlot) {
            m_stateSel = i;
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  doDeleteEntry
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doDeleteEntry() {
    if (m_cardSel < 0 || m_cardSel >= (int)m_cardEntries.size()) return;
    const MemCardEntry& entry = m_cardEntries[m_cardSel];
    if (!m_memCards) return;

    std::string mcrPath = m_memCards->prepareSlot1(m_gameSerial);
    std::string path    = mcrPath;
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
    if (!fs::exists(path)) { std::cerr << "[OmniSave] Delete failed — card not found\n"; return; }

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) { std::cerr << "[OmniSave] Delete failed — cannot open\n"; return; }
    fin.seekg(0, std::ios::end);
    size_t fileSize = (size_t)fin.tellg();
    fin.seekg(0);
    if (fileSize < 131072) { std::cerr << "[OmniSave] Delete failed — file too small\n"; return; }
    std::vector<uint8_t> data(fileSize);
    fin.read((char*)data.data(), fileSize);
    fin.close();

    int block = entry.firstBlock;
    int safety = MCR_MAX_BLOCKS;
    while (block >= 0 && block < MCR_MAX_BLOCKS && safety-- > 0) {
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
        std::cout << "[OmniSave] Freed block " << block << "\n";
        block = nextBlock;
    }

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream fout(tmpPath, std::ios::binary | std::ios::trunc);
        if (!fout.is_open()) { std::cerr << "[OmniSave] Delete failed — cannot write temp\n"; return; }
        fout.write((const char*)data.data(), (std::streamsize)data.size());
        fout.flush();
    }
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        std::cerr << "[OmniSave] Delete rename failed: " << ec.message() << "\n";
        fs::remove(tmpPath, ec);
        return;
    }

    std::cout << "[OmniSave] Deleted memcard entry: " << entry.title << "\n";
    if (m_sramReload) m_sramReload();

    freeMemCardTextures();
    m_cardEntries = parseMcr(path);
    for (auto& e : m_cardEntries)
        for (int f = 0; f < e.frameCount; ++f)
            if (!e.iconFrames[f].empty())
                e.iconTextures[f] = buildIconTexture(e.iconFrames[f]);
    m_cardSel = std::min(m_cardSel, std::max(0, (int)m_cardEntries.size() - 1));
}

// ─────────────────────────────────────────────────────────────────────────────
//  doRestoreSnapshot
//
//  Restores a Time Machine snapshot as the active memory card.
//  No safety copy is made — the history itself is the safety net, exactly
//  like Apple Time Machine. Every LiveCard snapshot remains in the list
//  unchanged, so the user can always navigate to any prior state.
//
//  Steps:
//    1. Flush SRAM → disk (persists in-game saves since OmniSave opened)
//    2. Atomic copy: snapshot → active card path (tmp → rename)
//    3. Reload core SRAM from the restored card
//    4. Refresh Time Machine list + memcard entries
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doRestoreSnapshot() {
    if (m_snapSel < 0 || m_snapSel >= (int)m_snapshots.size()) return;

    const TimeMachineEntry& snap = m_snapshots[m_snapSel];
    if (!fs::exists(snap.mcrPath)) {
        std::cerr << "[TimeMachine] Snapshot file missing: " << snap.mcrPath << "\n";
        return;
    }

    std::string destPath = m_activeCardPath;
    if (destPath.empty() && m_memCards)
        destPath = fs::absolute(m_memCards->activeCardPath(m_gameSerial)).string();
    if (destPath.empty()) {
        std::cerr << "[TimeMachine] Cannot restore — active card path unknown\n";
        return;
    }

    // Step 1: flush
    if (m_sramFlush) m_sramFlush();

    // Step 2: read snapshot into memory, write directly to destPath.
    // We don't use temp+rename because the core holds the card file open
    // on Windows and rename over an open file fails. The flush in step 1
    // ensures the core's buffer is clean before we overwrite.
    {
        std::ifstream src(snap.mcrPath, std::ios::binary);
        if (!src.is_open()) {
            SDL_Log("[TimeMachine] Restore failed — cannot open snapshot: %s",
                    snap.mcrPath.c_str());
            return;
        }
        std::vector<char> buf(std::istreambuf_iterator<char>(src), {});
        src.close();

        std::ofstream dst(destPath, std::ios::binary | std::ios::trunc);
        if (!dst.is_open()) {
            SDL_Log("[TimeMachine] Restore failed — cannot write to: %s",
                    destPath.c_str());
            return;
        }
        dst.write(buf.data(), buf.size());
        if (!dst.good()) {
            SDL_Log("[TimeMachine] Restore write error for: %s", destPath.c_str());
            return;
        }
    }

    std::cout << "[TimeMachine] Restored: " << snap.mcrPath
              << " → " << destPath << "\n";

    // Step 3: reload SRAM
    if (m_sramReload) m_sramReload();

    // Step 4: refresh UI — keep selection on the entry just restored
    loadTimeMachineEntries();
    loadMemCardEntries();
    m_snapSel = std::min(m_snapSel, std::max(0, (int)m_snapshots.size() - 1));

    std::cout << "[TimeMachine] Restore complete\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  doSwapCard
//  Fires when the user confirms a card swap from the card switcher strip.
//  Sets m_pendingSwapPath so app.cpp can do the actual flush/load/reinit,
//  then marks wantsClose so OmniSave exits cleanly.
//  The swap toast and rewind reinit are handled in app.cpp's update loop,
//  same pattern as consumeCardReload().
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::doSwapCard() {
    if (m_cardSlotSel < 0 || m_cardSlotSel >= (int)m_gameCards.size()) return;
    const GameCardSlot& chosen = m_gameCards[m_cardSlotSel];

    if (chosen.isActive) {
        // Already active — treat as a card reload instead
        m_confirmAction  = ConfirmAction::RELOAD_CARD;
        m_confirmMessage = "Reload memory card from disk?";
        m_confirmDetail  = "Unsaved in-game changes will be lost.";
        return;
    }

    m_pendingSwapPath = chosen.path;
    m_wantsClose      = true;
    std::cout << "[OmniSave] Card swap requested: " << chosen.path << "\n";
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

    if (m_focus == OmniPanel::CHRONICLE) {
        renderTimeMachinePanel(0, contentY, m_w, contentH);
    } else {
        renderMemCardPanel(0, contentY, leftW, contentH);
        renderDivider(divX, contentY, contentH);
        renderSaveStatePanel(divX + DIVIDER_W, contentY, rightW, contentH);
    }

    renderFooter();
    if (m_confirmAction != ConfirmAction::NONE)
        renderConfirmOverlay();

    // Import screen renders on top when active
    if (m_importPhase == ImportPhase::IMPORTING) {
        m_importScreen->render(m_renderer, m_w, m_h);
    }

    // Import banner (shows at bottom of vault when file detected)
    if (m_importBannerAlpha > 1.f)
        renderImportBanner(m_renderer, m_w, m_h);	
	
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
    int boxX = (m_w - boxW) / 2, boxY = (m_h - boxH) / 2;

    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 245);
    SDL_Rect box = { boxX, boxY, boxW, boxH };
    SDL_RenderFillRect(m_renderer, &box);

    SDL_Color borderClr;
    if      (m_confirmAction == ConfirmAction::DELETE_STATE ||
             m_confirmAction == ConfirmAction::DELETE_ENTRY)
        borderClr = { 220, 80,  80,  255 };
    else if (m_confirmAction == ConfirmAction::OVERWRITE_STATE)
        borderClr = { 220, 160, 40,  255 };
    else if (m_confirmAction == ConfirmAction::RESTORE_SNAPSHOT)
        borderClr = { 160, 80,  220, 255 };
    else if (m_confirmAction == ConfirmAction::COPY_STATE)
        borderClr = { 80,  200, 160, 255 };  // teal-green for branch
    else
        borderClr = { 60,  160, 220, 255 };

    SDL_SetRenderDrawColor(m_renderer, borderClr.r, borderClr.g, borderClr.b, 255);
    SDL_RenderDrawRect(m_renderer, &box);
    SDL_Rect box2 = { boxX+1, boxY+1, boxW-2, boxH-2 };
    SDL_RenderDrawRect(m_renderer, &box2);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    int cx = boxX + boxW / 2;
    m_theme->drawTextCentered(m_confirmMessage, cx, boxY + 24, pal.textPrimary, FontSize::BODY);
    if (!m_confirmDetail.empty())
        m_theme->drawTextCentered(m_confirmDetail, cx, boxY + 58, pal.textSecond, FontSize::SMALL);

    int hintCX = cx - 70;
    int hintY  = boxY + boxH - 44;
    m_theme->drawButtonHint(hintCX - 60, hintY, "A", "Confirm", borderClr);
    m_theme->drawButtonHint(hintCX + 60, hintY, "B", "Cancel",  pal.textSecond);
}

void OmniSaveVault::renderHeader() {
    m_theme->drawHeader(m_w, m_h, "OmniSave", m_gameTitle, 0);
}

void OmniSaveVault::renderDivider(int x, int y, int h) {
    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_Rect line = { x, y, DIVIDER_W, h };
    SDL_RenderFillRect(m_renderer, &line);
    SDL_Color accent = m_theme->palette().accent;
    SDL_SetRenderDrawColor(m_renderer, accent.r, accent.g, accent.b, 120);
    SDL_Rect glow = (m_focus == OmniPanel::MEMCARD)
        ? SDL_Rect{ x + DIVIDER_W, y, 3, h }
        : SDL_Rect{ x - 3, y, 3, h };
    SDL_RenderFillRect(m_renderer, &glow);
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderMemCardPanel
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderMemCardPanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::MEMCARD);

    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("MEMORY CARD", x + MARGIN, y + 12, labelCol, FontSize::SMALL);
    // Show which card is active, or a count if multiple cards exist
    {
        std::string subLabel;
        if (!m_gameCards.empty()) {
            for (const auto& slot : m_gameCards) {
                if (slot.isActive) { subLabel = slot.displayName; break; }
            }
            if (subLabel.empty())
                subLabel = std::to_string(m_gameCards.size()) + " cards available";
        } else {
            subLabel = "MemoryCard1.mcr";
        }
        m_theme->drawText(subLabel, x + MARGIN, y + 32, pal.textDisable, FontSize::TINY);
    }

    // ── Card slot switcher strip ──────────────────────────────────────────────
    // Shows all available cards for this game as pill buttons.
    // Active card is highlighted with the accent colour.
    // Left/Right navigates, A swaps (or reloads if already active).
    if (!m_gameCards.empty()) {
        int stripY  = y + 52;
        int pillX   = x + MARGIN;
        int pillY   = stripY + (SLOT_STRIP_H - SLOT_PILL_H) / 2;

        for (int i = 0; i < (int)m_gameCards.size(); ++i) {
            const GameCardSlot& slot = m_gameCards[i];
            bool isSel    = (focused && i == m_cardSlotSel);
            bool isActive = slot.isActive;

            SDL_Color pillBg = isActive
                ? SDL_Color{ (Uint8)(pal.accent.r/3), (Uint8)(pal.accent.g/3),
                             (Uint8)(pal.accent.b/3), 200 }
                : SDL_Color{ pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 180 };

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, pillBg.r, pillBg.g, pillBg.b, pillBg.a);
            SDL_Rect pill = { pillX, pillY, SLOT_PILL_W, SLOT_PILL_H };
            SDL_RenderFillRect(m_renderer, &pill);

            // Border: accent for active, selection highlight for focused, dim for others
            SDL_Color borderCol = isActive  ? pal.accent
                                : isSel     ? pal.textPrimary
                                :             pal.gridLine;
            SDL_SetRenderDrawColor(m_renderer,
                borderCol.r, borderCol.g, borderCol.b, isSel ? 255 : 160);
            SDL_RenderDrawRect(m_renderer, &pill);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

            // Label: "Card N" — keep it short so pills stay compact
            std::string pillLabel = "Card " + std::to_string(slot.slotNumber);
            if (isActive) pillLabel += " \xe2\x80\xa2";   // bullet = active
            SDL_Color textCol = isActive  ? pal.accent
                              : isSel     ? pal.textPrimary
                              :             pal.textSecond;
            m_theme->drawTextCentered(pillLabel,
                pillX + SLOT_PILL_W / 2,
                pillY + SLOT_PILL_H / 2 - 7,
                textCol, FontSize::TINY);

            pillX += SLOT_PILL_W + 8;
            if (pillX + SLOT_PILL_W > x + w - MARGIN) break; // don't overflow panel
        }
    }

    // If we have card slots, push the entry list down past the strip
    int stripOffset = m_gameCards.empty() ? 0 : SLOT_STRIP_H + 8;
    int rowY    = y + 62 + stripOffset;
    int rowH    = CARD_ROW_H;
    int visRows = (h - 62 - stripOffset) / rowH;

    if (m_cardEntries.empty()) {
        m_theme->drawText("No saves on this memory card",
            x + MARGIN, rowY + 20, pal.textDisable, FontSize::BODY);
        return;
    }

    if (m_cardSel < m_cardScroll) m_cardScroll = m_cardSel;
    if (m_cardSel >= m_cardScroll + visRows) m_cardScroll = m_cardSel - visRows + 1;

    int endIdx = std::min((int)m_cardEntries.size(), m_cardScroll + visRows);
    for (int i = m_cardScroll; i < endIdx; ++i) {
        const auto& entry = m_cardEntries[i];
        bool sel = focused && (i == m_cardSel);

        SDL_Rect rowRect = { x+4, rowY, w-8, rowH-4 };
        if (sel) {
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCardHover.r, pal.bgCardHover.g, pal.bgCardHover.b, 200);
            SDL_RenderFillRect(m_renderer, &rowRect);
            SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { x+4, rowY, 3, rowH-4 };
            SDL_RenderFillRect(m_renderer, &border);
        }

        int frame = std::min(m_iconFrame, entry.frameCount - 1);
        SDL_Texture* iconTex = entry.iconTextures[frame];
        SDL_Rect iconDst = { x+MARGIN, rowY+(rowH-ICON_SIZE)/2, ICON_SIZE, ICON_SIZE };
        if (iconTex) {
            SDL_SetTextureBlendMode(iconTex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(m_renderer, iconTex, nullptr, &iconDst);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 40);
            int pixH = ICON_SIZE / MCR_ICON_H;
            for (int sy = iconDst.y; sy < iconDst.y + ICON_SIZE; sy += pixH) {
                SDL_Rect sl = { iconDst.x, sy, ICON_SIZE, 1 };
                SDL_RenderFillRect(m_renderer, &sl);
            }
        } else {
            SDL_SetRenderDrawColor(m_renderer, 80, 80, 120, 255);
            SDL_RenderFillRect(m_renderer, &iconDst);
            m_theme->drawTextCentered("?", iconDst.x+ICON_SIZE/2, iconDst.y+ICON_SIZE/2-8,
                pal.textDisable, FontSize::SMALL);
        }

        int textX = x + MARGIN + ICON_SIZE + 10;
        int textW = w - MARGIN - ICON_SIZE - 14;
        m_theme->drawTextTruncated(entry.title, textX, rowY+10, textW,
            sel ? pal.textPrimary : pal.textSecond, FontSize::BODY);
        m_theme->drawText(std::to_string(entry.blocksUsed)
            + (entry.blocksUsed==1 ? " block" : " blocks"),
            textX, rowY+36, pal.textDisable, FontSize::TINY);
        rowY += rowH;
    }

    if ((int)m_cardEntries.size() > visRows) {
        int sbX = x + w - 6, sbH = h - 62;
        float frac = (float)visRows / m_cardEntries.size();
        float top  = (float)m_cardScroll / m_cardEntries.size();
        int thumbH = std::max(20, (int)(frac * sbH));
        int thumbY = y + 62 + (int)(top * sbH);
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 255);
        SDL_Rect track = { sbX, y+62, 4, sbH }; SDL_RenderFillRect(m_renderer, &track);
        SDL_SetRenderDrawColor(m_renderer,
            pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 200);
        SDL_Rect thumb = { sbX, thumbY, 4, thumbH }; SDL_RenderFillRect(m_renderer, &thumb);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderSaveStatePanel
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderSaveStatePanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    bool focused = (m_focus == OmniPanel::SAVESTATES);

    SDL_Color labelCol = focused ? pal.textPrimary : pal.textSecond;
    m_theme->drawText("SAVE STATES", x + MARGIN, y + 12, labelCol, FontSize::SMALL);

    int cardW = STATE_CARD_W, cardH = STATE_CARD_H, padX = 14, padY = 14;
    int gridX = x + MARGIN, gridY = y + 54;
    int cols    = std::max(1, (w - MARGIN*2 + padX) / (cardW + padX));
    int visRows = std::max(1, (h - 54) / (cardH + padY));

    if (m_stateSel < m_stateScroll * cols) m_stateScroll = m_stateSel / cols;
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
            SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_Rect border = { cx-2, cy-2, cardW+4, cardH+4 };
            SDL_RenderDrawRect(m_renderer, &border);
        }

        if (isNew) {
            m_theme->drawTextCentered("+", cx+cardW/2, cy+cardH/2-20,
                sel ? pal.accent : pal.textDisable, FontSize::HEADER);
            m_theme->drawTextCentered("New Save", cx+cardW/2, cy+cardH-26,
                pal.textDisable, FontSize::TINY);
        } else if (slot.exists && i < (int)m_thumbTex.size() && m_thumbTex[i]) {
            SDL_Rect thumbDst = { cx, cy, cardW, cardH-36 };
            SDL_RenderCopy(m_renderer, m_thumbTex[i], nullptr, &thumbDst);
            SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 220);
            SDL_Rect strip = { cx, cy+cardH-36, cardW, 36 };
            SDL_RenderFillRect(m_renderer, &strip);
            std::string label = (slot.slotNumber==-1) ? "Auto"
                : "Slot " + std::to_string(slot.slotNumber+1);
            m_theme->drawText(label, cx+6, cy+cardH-32, pal.textPrimary, FontSize::TINY);
            // Line 2: branch lineage label if this is a branched copy, timestamp otherwise
            {
                std::string branchLabel = readBranchLabel(slot.statePath);
                if (!branchLabel.empty()) {
                    SDL_Color branchCol = { 80, 200, 160, 255 };
                    m_theme->drawText(branchLabel, cx+6, cy+cardH-16, branchCol, FontSize::TINY);
                } else if (!slot.timestamp.empty()) {
                    m_theme->drawText(slot.timestamp, cx+6, cy+cardH-16, pal.textDisable, FontSize::TINY);
                }
            }
        } else {
            m_theme->drawTextCentered("No Preview", cx+cardW/2, cy+cardH/2-10,
                pal.textDisable, FontSize::TINY);
            std::string label = (slot.slotNumber==-1) ? "Auto"
                : "Slot " + std::to_string(slot.slotNumber+1);
            m_theme->drawText(label, cx+6, cy+cardH-16, pal.textSecond, FontSize::TINY);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderTimeMachinePanel
//
//  Full-screen takeover. Each row:
//    [game frame thumbnail]  |  [full date + time]
//                               [relative age in purple]
//                               [N saves on card]
//                               [SpriteCard mini-icons]
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderTimeMachinePanel(int x, int y, int w, int h) {
    const auto& pal = m_theme->palette();
    SDL_Color tmAccent = { 160, 100, 255, 255 };

    // Header
    m_theme->drawText("CARD CHRONICLE", x + MARGIN, y + 12, tmAccent, FontSize::SMALL);
    if (!m_gameSerial.empty())
        m_theme->drawText(m_gameSerial + "_1",
            x + MARGIN + 196, y + 12, pal.textDisable, FontSize::SMALL);

    if (!m_snapshots.empty()) {
        const auto& oldest = m_snapshots.back();
        std::string info = std::to_string(m_snapshots.size())
            + " snapshot" + (m_snapshots.size()==1 ? "" : "s")
            + "  \xe2\x80\x94  oldest: " + oldest.timestamp;
        m_theme->drawText(info, x + MARGIN, y + 34, pal.textDisable, FontSize::TINY);
    }

    int rowY    = y + 62;
    int rowH    = TM_ROW_H;
    int visRows = std::max(1, (h - 62) / rowH);

    if (m_snapshots.empty()) {
        int midY = y + h/2 - 36;
        m_theme->drawTextCentered("No card history yet.",
            x + w/2, midY, pal.textDisable, FontSize::BODY);
        m_theme->drawTextCentered("Snapshots are created automatically each time",
            x + w/2, midY + 30, pal.textDisable, FontSize::TINY);
        m_theme->drawTextCentered("the game saves to the memory card.",
            x + w/2, midY + 48, pal.textDisable, FontSize::TINY);
        return;
    }

    if (m_snapSel < m_snapScroll) m_snapScroll = m_snapSel;
    if (m_snapSel >= m_snapScroll + visRows) m_snapScroll = m_snapSel - visRows + 1;

    int endIdx = std::min((int)m_snapshots.size(), m_snapScroll + visRows);

    for (int i = m_snapScroll; i < endIdx; ++i) {
        const TimeMachineEntry& snap = m_snapshots[i];
        bool sel = (i == m_snapSel);

        // Row background
        SDL_Rect rowRect = { x+4, rowY+2, w-8, rowH-6 };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCardHover.r, pal.bgCardHover.g, pal.bgCardHover.b, 220);
            SDL_RenderFillRect(m_renderer, &rowRect);
            SDL_SetRenderDrawColor(m_renderer, 160, 100, 255, 255);
            SDL_Rect border = { x+4, rowY+2, 3, rowH-6 };
            SDL_RenderFillRect(m_renderer, &border);
        } else {
            SDL_SetRenderDrawColor(m_renderer,
                pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 100);
            SDL_RenderFillRect(m_renderer, &rowRect);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        if (i > m_snapScroll) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 60);
            SDL_RenderDrawLine(m_renderer, x+MARGIN, rowY, x+w-MARGIN, rowY);
        }

        int colX = x + MARGIN;

        // Thumbnail
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
                colX + TM_THUMB_W/2, thumbY + TM_THUMB_H/2 - 6,
                pal.textDisable, FontSize::TINY);
        }
        colX += TM_THUMB_W + 16;

        // Text: full timestamp (line 1), relative age in purple (line 2), save count (line 3)
        SDL_Color tsCol = sel ? pal.textPrimary : pal.textSecond;
        m_theme->drawText(snap.timestamp, colX, rowY + 10, tsCol, FontSize::BODY);
        if (!snap.relativeAge.empty())
            m_theme->drawText(snap.relativeAge, colX, rowY + 34, tmAccent, FontSize::TINY);

        std::string saveInfo = snap.entries.empty()
            ? "Empty card"
            : std::to_string(snap.entries.size())
              + " save" + (snap.entries.size()==1 ? "" : "s") + " on card";
        m_theme->drawText(saveInfo, colX, rowY + 52, pal.textDisable, FontSize::TINY);

        colX += 200;

        // SpriteCard mini-icons
        int iconY    = rowY + (rowH - TM_ICON_SIZE) / 2;
        int maxIcons = std::min((int)snap.entries.size(), 4);
        for (int ei = 0; ei < maxIcons; ++ei) {
            const MemCardEntry& entry = snap.entries[ei];
            int frame = std::min(m_iconFrame, std::max(0, entry.frameCount-1));
            SDL_Texture* iconTex = (frame>=0 && frame<3) ? entry.iconTextures[frame] : nullptr;
            SDL_Rect iconDst = { colX, iconY, TM_ICON_SIZE, TM_ICON_SIZE };
            if (iconTex) {
                SDL_SetTextureBlendMode(iconTex, SDL_BLENDMODE_BLEND);
                SDL_RenderCopy(m_renderer, iconTex, nullptr, &iconDst);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 30);
                int pixH = std::max(1, TM_ICON_SIZE / MCR_ICON_H);
                for (int sy = iconDst.y; sy < iconDst.y + TM_ICON_SIZE; sy += pixH) {
                    SDL_Rect sl = { iconDst.x, sy, TM_ICON_SIZE, 1 };
                    SDL_RenderFillRect(m_renderer, &sl);
                }
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            } else {
                SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 200);
                SDL_RenderFillRect(m_renderer, &iconDst);
            }
            colX += TM_ICON_SIZE + 6;
        }

        rowY += rowH;
    }

    // Scrollbar
    if ((int)m_snapshots.size() > visRows) {
        int sbH = h - 62, sbX = x + w - 6;
        float frac = (float)visRows / m_snapshots.size();
        float top  = (float)m_snapScroll / m_snapshots.size();
        int thumbH = std::max(20, (int)(frac * sbH));
        int thumbY = y + 62 + (int)(top * sbH);
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 80, 255);
        SDL_Rect track = { sbX, y+62, 4, sbH }; SDL_RenderFillRect(m_renderer, &track);
        SDL_SetRenderDrawColor(m_renderer,
            pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 200);
        SDL_Rect thumb = { sbX, thumbY, 4, thumbH }; SDL_RenderFillRect(m_renderer, &thumb);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderFooter
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveVault::renderFooter() {
    if (m_focus == OmniPanel::CHRONICLE) {
        m_theme->drawFooterHints(m_w, m_h, "Restore Snapshot", "Back", "", "");
        return;
    }
    if (m_focus == OmniPanel::SAVESTATES) {
        m_theme->drawFooterHints(m_w, m_h, "Load / New Save", "Back", "Save Here", "Branch");
    } else {
        // If multiple cards exist, show the swap hint; otherwise reload hint
        const char* confirmHint = (m_gameCards.size() > 1) ? "Swap / Reload Card" : "Reload Card";
        m_theme->drawFooterHints(m_w, m_h, confirmHint, "Back", "Delete Save", "Chronicle");
    }
    int footY = m_h - m_theme->layout().footerH;
    int cy    = footY + (m_theme->layout().footerH - 34) / 2;
    m_theme->drawButtonHint(m_w - 220, cy, "L1/R1",
        "Switch Panel", m_theme->palette().textSecond);
    if (m_focus == OmniPanel::SAVESTATES)
        m_theme->drawButtonHint(m_w - 390, cy, "Start",
            "Delete", m_theme->palette().textSecond);
}

// ── scanImportFolder() ───────────────────────────────────────────────────────

void OmniSaveVault::scanImportFolder()
{
    // Don't clobber an already-pending file
    if (m_importPhase != ImportPhase::NONE) return;

    const std::string importDir = "memcards/import";

    SDL_Log("[OmniSaveVault] scanImportFolder: cwd-relative path '%s', exists=%d",
            importDir.c_str(), (int)fs::exists(importDir));

    if (!fs::exists(importDir)) return;

    for (const auto& entry : fs::directory_iterator(importDir)) {
        if (!entry.is_regular_file()) continue;

        std::string p = entry.path().string();
        std::string ext = entry.path().extension().string();

        // Normalise extension to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

if (ext == ".mcr" || ext == ".mcd" || ext == ".mem" ||
    ext == ".vgs" || ext == ".srm" || ext == ".gme" ||
    ext == ".zip") {
            m_importPending     = p;
            m_importPhase       = ImportPhase::DETECTED;
            m_importBannerVisible = true;
            SDL_Log("[OmniSaveVault] Import file detected: %s", p.c_str());
            return; // handle one file at a time
        }
    }
}

// ── openImportScreen() ───────────────────────────────────────────────────────

void OmniSaveVault::openImportScreen()
{
    if (m_importPending.empty()) return;

    // Flush SRAM before opening so the core's current buffer is on disk.
    // importBlock will read and write the card file directly while the
    // import screen is open. The core won't autosave during this window
    // because m_suppressSramPoll should be set by the caller — but even
    // if it isn't, flushing first ensures we start from a clean state.
    if (m_sramFlush) m_sramFlush();

    bool ok = m_importScreen->open(m_importPending, m_activeCardPath);
    if (ok) {
        m_importPhase = ImportPhase::IMPORTING;
    } else {
        // Parse failed — move to done anyway so it doesn't loop forever
        SDL_Log("[OmniSaveVault] Import screen open failed for %s",
                m_importPending.c_str());
        moveToImportDone(m_importPending);
        m_importPending.clear();
        m_importPhase         = ImportPhase::NONE;
        m_importBannerVisible = false;
    }
}

// ── moveToImportDone() ───────────────────────────────────────────────────────

void OmniSaveVault::moveToImportDone(const std::string& srcPath)
{
    const std::string doneDir = "memcards/import/done";
    fs::create_directories(doneDir);

    std::string filename  = fs::path(srcPath).filename().string();
    std::string destPath  = doneDir + "/" + filename;

    // If a file with the same name already exists in done/, append a counter
    if (fs::exists(destPath)) {
        std::string stem = fs::path(srcPath).stem().string();
        std::string ext  = fs::path(srcPath).extension().string();
        int counter = 1;
        do {
            destPath = doneDir + "/" + stem + "_" + std::to_string(counter++) + ext;
        } while (fs::exists(destPath) && counter < 999);
    }

    std::error_code ec;
    fs::rename(srcPath, destPath, ec);
    if (ec) {
        // rename can fail across filesystems — fall back to copy + delete
        fs::copy(srcPath, destPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) std::remove(srcPath.c_str());
    }

    SDL_Log("[OmniSaveVault] Moved import file to: %s", destPath.c_str());
}

// ── handleImportInput() ──────────────────────────────────────────────────────

void OmniSaveVault::handleImportInput(const SDL_Event& e)
{
    if (m_importPhase != ImportPhase::DETECTED) return;

    bool triggered = false;
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        // [X] / Square opens import; [B] dismisses banner without importing
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X)  triggered = true;
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            // B just closes the vault normally — don't touch the import file.
            // It will re-detect next time OmniSave opens.
            m_importPending.clear();
            m_importPhase         = ImportPhase::NONE;
            m_importBannerVisible = false;
        }
    } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_i) triggered = true; // keyboard shortcut
    }

    if (triggered) openImportScreen();
}

// ── renderImportBanner() ─────────────────────────────────────────────────────

void OmniSaveVault::renderImportBanner(SDL_Renderer* r, int screenW, int screenH)
{
    // A slim banner at the very bottom of the screen, same style as the
    // "Trophy auto-screenshot" toast.
    static constexpr int BANNER_H = 44;
    static constexpr int BANNER_W = 560;

    int bx = (screenW - BANNER_W) / 2;
    int by = screenH - BANNER_H - 14;

    uint8_t alpha = static_cast<uint8_t>(std::min(255.f, m_importBannerAlpha));

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Background
    SDL_SetRenderDrawColor(r, 15, 30, 50, static_cast<uint8_t>(alpha * 230 / 255));
    SDL_Rect bgRect{bx, by, BANNER_W, BANNER_H};
    SDL_RenderFillRect(r, &bgRect);

    // Teal border
    SDL_SetRenderDrawColor(r, 80, 200, 180, alpha);
    SDL_RenderDrawRect(r, &bgRect);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Icon dot (small teal square, left side)
    SDL_SetRenderDrawColor(r, 80, 200, 180, 255);
    SDL_Rect dot{bx + 14, by + BANNER_H / 2 - 4, 8, 8};
    SDL_RenderFillRect(r, &dot);

    // Text
    std::string filename = fs::path(m_importPending).filename().string();
    std::string msg = "Import ready: " + filename;

    // Main text
    SDL_Color textCol{220, 220, 220, alpha};
    m_theme->drawText(msg, bx + 30, by + 12, textCol, FontSize::TINY);

    // Hint on right side
    SDL_Color hintCol{120, 200, 170, alpha};
    m_theme->drawText("[X] Import", bx + BANNER_W - 90, by + 12, hintCol, FontSize::TINY);
}