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
