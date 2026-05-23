#include "memcard_manager.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>

MemCardManager::MemCardManager() {
    ensureDirectories();
}

void MemCardManager::ensureDirectories() const {
    fs::create_directories(m_baseDir + "shared/");
    fs::create_directories(m_baseDir + "per_game/");
}

static constexpr int MCR_SIZE  = 131072;  // 128 KB
static constexpr int NUM_SLOTS = 15;
static constexpr int BLOCK_SIZE = 8192;

// ─── buildBlankCard ───────────────────────────────────────────────────────────
// Returns a properly-formatted 128 KB PS1 memory card image that Beetle PSX
// will accept, read into its internal SRAM buffer, and expose via
// RETRO_MEMORY_SAVE_RAM.
//
// PS1 memory card structure (each frame = 128 bytes):
//   Frame 0     : Header  — "MC" + 0x00 padding + 0x0E checksum at byte 127
//   Frames 1–15 : Directory entries — first byte 0xA0 (free/fresh),
//                 bytes 1–126 = 0x00, byte 127 = XOR checksum of bytes 0–126
//   Frame 16–63 : Broken sector list — all 0xFF
//   Frame 64+   : Data blocks — all 0x00
//
// The critical thing Beetle checks: frame 0 must start with 'M','C' and the
// directory frames must have valid 0xA0 free-slot markers. A card filled with
// 0xFF (our old format) causes Beetle to treat the card as corrupt and keep
// its SRAM buffer zeroed — so flushSaveRAM() was writing zeros back to disk.
static std::vector<uint8_t> buildBlankCard() {
    static constexpr size_t CARD_BYTES  = 131072;
    static constexpr size_t FRAME_BYTES = 128;
    static constexpr int    NUM_FRAMES  = CARD_BYTES / FRAME_BYTES; // 1024

    std::vector<uint8_t> card(CARD_BYTES, 0x00);

    // ── Frame 0: Header ───────────────────────────────────────────────────────
    card[0] = 'M';
    card[1] = 'C';
    // Bytes 2–126 already 0x00
    // Checksum = XOR of bytes 0–126
    uint8_t hdrCheck = 0;
    for (int i = 0; i < 127; ++i) hdrCheck ^= card[i];
    card[127] = hdrCheck;  // should be 'M' ^ 'C' = 0x01 ^ ... = 0x0E

    // ── Frames 1–15: Directory entries (free/fresh slots) ────────────────────
    for (int f = 1; f <= 15; ++f) {
        size_t base = f * FRAME_BYTES;
        card[base + 0] = 0xA0;  // free, freshly formatted
        // bytes 1–7: link info (0x00 = end of chain / unused)
        // bytes 8–11: block size = 0 for free slots
        // bytes 12–20: region string (empty for free)
        // bytes 21–126: filename (empty for free)
        // All already 0x00 from the initial fill.

        // Checksum = XOR of bytes 0–126
        uint8_t check = 0;
        for (size_t i = base; i < base + 127; ++i) check ^= card[i];
        card[base + 127] = check;
    }

    // ── Frames 16–63: Broken sector list — 0xFF ───────────────────────────────
    for (int f = 16; f <= 63; ++f) {
        size_t base = f * FRAME_BYTES;
        for (size_t i = base; i < base + FRAME_BYTES; ++i)
            card[i] = 0xFF;
    }

    // Frames 64–1023 (data blocks) remain 0x00 from initial fill.
    return card;
}

std::string MemCardManager::prepareSlot1(const std::string& gameSerial) {
    ensureDirectories();
    std::string path = slotPath(1, gameSerial);

    if (!fs::exists(path)) {
        std::ofstream f(path, std::ios::binary);
        if (f.is_open()) {
            auto blank = buildBlankCard();
            f.write(reinterpret_cast<const char*>(blank.data()),
                    static_cast<std::streamsize>(blank.size()));
            std::cout << "[MemCard] Created new memory card: " << path << "\n";
        }
    } else {
        // Detect the old broken blank format (0xFF fill): check frame 1 first byte.
        // A valid card has 0xA0 (free) or 0x51 (used) there. 0xFF means the old
        // bad blank — delete and recreate with correct PS1 formatting so Beetle
        // will accept it and expose real SRAM data via RETRO_MEMORY_SAVE_RAM.
        std::ifstream check(path, std::ios::binary);
        if (check.is_open()) {
            uint8_t frame1byte0 = 0;
            check.seekg(128); // start of frame 1
            check.read(reinterpret_cast<char*>(&frame1byte0), 1);
            check.close();
            if (frame1byte0 == 0xFF) {
                fs::remove(path);
                std::ofstream f(path, std::ios::binary);
                if (f.is_open()) {
                    auto blank = buildBlankCard();
                    f.write(reinterpret_cast<const char*>(blank.data()),
                            static_cast<std::streamsize>(blank.size()));
                    std::cout << "[MemCard] Rebuilt stale blank card: " << path << "\n";
                }
            }
        }
    }

    std::cout << "[MemCard] Slot 1: " << path << "\n";
    return path;
}

std::string MemCardManager::prepareSlot2(const std::string& gameSerial) {
    ensureDirectories();
    std::string path = slotPath(2, gameSerial);

    if (m_mode == MemCardMode::PER_GAME && !fs::exists(path)) {
        std::ofstream f(path, std::ios::binary);
        if (f.is_open()) {
            auto blank = buildBlankCard();
            f.write(reinterpret_cast<const char*>(blank.data()),
                    static_cast<std::streamsize>(blank.size()));
        }
    }

    std::cout << "[MemCard] Slot 2: " << path << "\n";
    return path;
}

std::string MemCardManager::saveDirectory(const std::string& gameSerial) const {
    if (m_mode == MemCardMode::PER_GAME && !gameSerial.empty())
        return m_baseDir + "per_game/";
    return m_baseDir + "shared/";
}

std::string MemCardManager::slotPath(int slot,
                                      const std::string& gameSerial) const {
    if (m_mode == MemCardMode::PER_GAME && !gameSerial.empty()) {
        return m_baseDir + "per_game/" + gameSerial +
               "_" + std::to_string(slot) + ".mcr";
    }
    // Shared mode — everyone uses the same card
    return m_baseDir + "shared/MemoryCard" + std::to_string(slot) + ".mcr";
}

std::string MemCardManager::statusString(const std::string& gameSerial) const {
    if (m_mode == MemCardMode::PER_GAME) {
        return "Per-game memory cards (" + gameSerial + ")";
    }
    std::string path = m_baseDir + "shared/MemoryCard1.mcr";
    if (fs::exists(path)) {
        size_t sz = cardSize(path);
        int blocks = (int)(sz / 8192);  // Each block is 8KB
        return "Shared memory card (" + std::to_string(blocks) + " blocks)";
    }
    return "Shared memory card (new)";
}

std::vector<std::string> MemCardManager::listPerGameCards() const {
    std::vector<std::string> cards;
    std::string dir = m_baseDir + "per_game/";
    if (!fs::exists(dir)) return cards;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".mcr")
            cards.push_back(entry.path().string());
    }
    return cards;
}

size_t MemCardManager::cardSize(const std::string& path) {
    try { return fs::file_size(path); }
    catch (...) { return 0; }
}


// ─────────────────────────────────────────────────────────────────────────────
//  Helper: read raw MCR bytes from disk
// ─────────────────────────────────────────────────────────────────────────────
static bool readMcrRaw(const std::string& path,
                        std::vector<uint8_t>& data)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    data.resize(MCR_SIZE);
    size_t read = fread(data.data(), 1, MCR_SIZE, f);
    fclose(f);
    return (read == MCR_SIZE);
}


// ─────────────────────────────────────────────────────────────────────────────
//  loadCardForImport
// ─────────────────────────────────────────────────────────────────────────────
bool MemCardManager::loadCardForImport(const std::string& path,
                                        std::vector<ImportBlock>& out)
{
    out.clear();

    std::vector<uint8_t> data;
    if (!readMcrRaw(path, data)) {
        SDL_Log("[MemCardManager] loadCardForImport: failed to read %s", path.c_str());
        return false;
    }

    // PS1 MCR directory table starts at 0x0200.
    // Each entry is 128 bytes.  Slots 0-14 are user saves (slot 0 = frame 1).
    // Byte 0 of each frame = allocation state:
    //   0x51 = first block of a save
    //   0x52 = middle block
    //   0x53 = last block
    //   0xA0 = first block (deleted)
    //   0xFF = free
    static constexpr int DIR_BASE   = 0x0200;
    static constexpr int FRAME_SIZE = 128;
    static constexpr int DATA_BASE  = 0x2000;   // save data starts at 0x2000

    for (int i = 0; i < NUM_SLOTS; ++i) {
        ImportBlock blk;
        blk.slotIndex = i;

        const uint8_t* frame = data.data() + DIR_BASE + (i * FRAME_SIZE);
        uint8_t state = frame[0];

        // Only import "first block" entries — we follow the chain from there
        if (state == 0xFF) {
            // Free slot
            blk.isEmpty = true;
            out.push_back(blk);
            continue;
        }
        if (state != 0x51 && state != 0xA1) {
            // Middle/last block, or deleted first block — skip as standalone entry
            // (will be picked up when we process the chain for a 0x51 slot)
            blk.isEmpty = true;
            out.push_back(blk);
            continue;
        }

        blk.isEmpty = (state == 0xA1); // deleted saves shown greyed-out

        // --- Serial  (bytes 0x0A–0x13, null-terminated ASCII) ---
        char serial[16] = {};
        memcpy(serial, frame + 0x0A, 12);
        serial[12] = '\0';
        blk.serial = serial;

        // --- Title  (bytes 0x60–0xBF, Shift-JIS, 64 bytes max) ---
        // Reuse the existing decodeMcrTitle helper
        // Read raw title bytes (Shift-JIS, offset 0x60 in the directory frame)
        const uint8_t* titleBytes = frame + 0x60;
        std::string title;
        for (int t = 0; t < 64 && titleBytes[t] != 0; ++t)
            title += (char)titleBytes[t];
        blk.title = title.empty() ? blk.serial : title;

        // --- Block count: follow next-block chain ---
        blk.blocksUsed = 1;
        int next = (frame[0x08] | (frame[0x09] << 8)); // link pointer
        int safety = 0;
        while (next != 0xFFFF && safety++ < NUM_SLOTS) {
            blk.blocksUsed++;
            if (next < 0 || next >= NUM_SLOTS) {
                blk.isCorrupted = true;
                break;
            }
            const uint8_t* nf = data.data() + DIR_BASE + (next * FRAME_SIZE);
            next = (nf[0x08] | (nf[0x09] << 8));
        }

        // --- Icon frames (reuse existing sprite loader) ---
        // loadSpriteFrames reads the save-data block at DATA_BASE + slot*BLOCK_SIZE
        // and returns up to 3 animation frames as 16x16 colour arrays.
        // Leave frames empty for now — OmniSaveImport will render a placeholder
        blk.frameCount = 0;

        out.push_back(blk);
    }

    return true;
}


// ─────────────────────────────────────────────────────────────────────────────
//  importBlock
// ─────────────────────────────────────────────────────────────────────────────
bool MemCardManager::importBlock(const std::string& sourcePath,
                                  int                slotIndex,
                                  const std::string& destCardPath,
                                  std::string&       errorOut)
{
    // --- Load source card ---
    std::vector<uint8_t> src;
    if (!readMcrRaw(sourcePath, src)) {
        errorOut = "Could not read source file.";
        return false;
    }

    // --- Load active (destination) card ---
    if (destCardPath.empty()) {
        errorOut = "No active memory card.";
        return false;
    }
    std::vector<uint8_t> dst;
    if (!readMcrRaw(destCardPath, dst)) {
        errorOut = "Could not read active card.";
        return false;
    }

    static constexpr int DIR_BASE   = 0x0200;
    static constexpr int FRAME_SIZE = 128;
    static constexpr int DATA_BASE  = 0x2000;

    // --- Verify source slot is a valid first-block entry ---
    const uint8_t* srcFrame = src.data() + DIR_BASE + (slotIndex * FRAME_SIZE);
    uint8_t srcState = srcFrame[0];
    if (srcState != 0x51 && srcState != 0xA1) {
        errorOut = "Source slot is not a save entry.";
        return false;
    }

    // --- Collect all source blocks in the chain ---
    std::vector<int> srcChain;
    srcChain.push_back(slotIndex);
    int next = srcFrame[0x08] | (srcFrame[0x09] << 8);
    int safety = 0;
    while (next != 0xFFFF && safety++ < NUM_SLOTS) {
        if (next < 0 || next >= NUM_SLOTS) {
            errorOut = "Source save chain is corrupted.";
            return false;
        }
        srcChain.push_back(next);
        const uint8_t* nf = src.data() + DIR_BASE + (next * FRAME_SIZE);
        next = nf[0x08] | (nf[0x09] << 8);
    }

    // --- Find free destination slots ---
    std::vector<int> dstFree;
    for (int i = 0; i < NUM_SLOTS; ++i) {
        uint8_t state = dst[DIR_BASE + i * FRAME_SIZE];
        if (state == 0xFF) dstFree.push_back(i);
    }
    if ((int)dstFree.size() < (int)srcChain.size()) {
        errorOut = "Not enough free space on active card ("
                 + std::to_string(srcChain.size()) + " blocks needed, "
                 + std::to_string(dstFree.size()) + " free).";
        return false;
    }

    // --- Map source chain → destination slots ---
    // We'll use the first N free slots, assigned in order.
    std::vector<int> dstChain;
    for (int i = 0; i < (int)srcChain.size(); ++i)
        dstChain.push_back(dstFree[i]);

    // --- Copy directory frames into destination ---
    for (int ci = 0; ci < (int)srcChain.size(); ++ci) {
        int si = srcChain[ci];
        int di = dstChain[ci];

        uint8_t* dstFrame = dst.data() + DIR_BASE + (di * FRAME_SIZE);
        const uint8_t* srcFrameN = src.data() + DIR_BASE + (si * FRAME_SIZE);

        memcpy(dstFrame, srcFrameN, FRAME_SIZE);

        // Fix up allocation state: deleted saves become live on import
        if (dstFrame[0] == 0xA1) dstFrame[0] = 0x51;
        if (dstFrame[0] == 0xA2) dstFrame[0] = 0x52;
        if (dstFrame[0] == 0xA3) dstFrame[0] = 0x53;

        // Fix up next-block link pointer to destination slot numbering
        if (ci < (int)dstChain.size() - 1) {
            int dstNext = dstChain[ci + 1];
            dstFrame[0x08] = static_cast<uint8_t>(dstNext & 0xFF);
            dstFrame[0x09] = static_cast<uint8_t>((dstNext >> 8) & 0xFF);
        } else {
            // Last block — link = 0xFFFF
            dstFrame[0x08] = 0xFF;
            dstFrame[0x09] = 0xFF;
        }

        // Recalculate the directory-frame checksum (XOR of bytes 0x00–0x7E)
        uint8_t xorSum = 0;
        for (int b = 0; b < 127; ++b) xorSum ^= dstFrame[b];
        dstFrame[127] = xorSum;
    }

    // --- Copy save data blocks ---
    for (int ci = 0; ci < (int)srcChain.size(); ++ci) {
        int si = srcChain[ci];
        int di = dstChain[ci];
        const uint8_t* srcData = src.data() + DATA_BASE + si * BLOCK_SIZE;
        uint8_t*       dstData = dst.data() + DATA_BASE + di * BLOCK_SIZE;
        memcpy(dstData, srcData, BLOCK_SIZE);
    }

    // --- Atomic write: temp file → rename ---
    std::string tempPath = destCardPath + ".import_tmp";
    FILE* f = fopen(tempPath.c_str(), "wb");
    if (!f) {
        errorOut = "Could not write to card (disk full?).";
        return false;
    }
    size_t written = fwrite(dst.data(), 1, MCR_SIZE, f);
    fclose(f);
    if (written != MCR_SIZE) {
        std::remove(tempPath.c_str());
        errorOut = "Write error — card not modified.";
        return false;
    }
    if (std::rename(tempPath.c_str(), destCardPath.c_str()) != 0) {
        std::remove(tempPath.c_str());
        errorOut = "Could not finalize write (rename failed).";
        return false;
    }


    SDL_Log("[MemCardManager] importBlock: slot %d from %s → slot %d in %s",
            slotIndex, sourcePath.c_str(), dstChain[0], destCardPath.c_str());
    return true;
}
