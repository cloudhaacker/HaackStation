#include "memcard_manager.h"
#include <iostream>
#include <fstream>
#include <vector>

MemCardManager::MemCardManager() {
    ensureDirectories();
}

void MemCardManager::ensureDirectories() const {
    fs::create_directories(m_baseDir + "shared/");
    fs::create_directories(m_baseDir + "per_game/");
}

std::string MemCardManager::prepareSlot1(const std::string& gameSerial) {
    ensureDirectories();
    std::string path = slotPath(1, gameSerial);

    // Create a blank memory card if one doesn't exist yet
    if (!fs::exists(path)) {
        std::ofstream f(path, std::ios::binary);
        if (f.is_open()) {
            // PS1 memory card format:
            // First 128 bytes: header frame
            // Remaining: directory and data frames (all 0xFF initially)
            std::vector<uint8_t> blank(STANDARD_CARD_BYTES, 0xFF);

            // Write the memory card header (MC magic bytes)
            const char header[] = "MC";
            blank[0] = 'M'; blank[1] = 'C';
            // Checksum byte at offset 127
            blank[127] = 0x0E;

            f.write((char*)blank.data(), blank.size());
            std::cout << "[MemCard] Created new memory card: " << path << "\n";
        }
    }

    std::cout << "[MemCard] Slot 1: " << path << "\n";
    return path;
}

std::string MemCardManager::prepareSlot2(const std::string& gameSerial) {
    ensureDirectories();
    std::string path = slotPath(2, gameSerial);

    // Slot 2 starts empty — only create if per-game mode
    if (m_mode == MemCardMode::PER_GAME && !fs::exists(path)) {
        std::ofstream f(path, std::ios::binary);
        if (f.is_open()) {
            std::vector<uint8_t> blank(STANDARD_CARD_BYTES, 0xFF);
            blank[0] = 'M'; blank[1] = 'C';
            blank[127] = 0x0E;
            f.write((char*)blank.data(), blank.size());
        }
    }

    std::cout << "[MemCard] Slot 2: " << path << "\n";
    return path;
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
