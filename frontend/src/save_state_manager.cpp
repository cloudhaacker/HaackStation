#include "save_state_manager.h"
#include "libretro_bridge.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

namespace fs = std::filesystem;

SaveStateManager::SaveStateManager() {}

SaveStateManager::~SaveStateManager() {}

// ─── Game setup ───────────────────────────────────────────────────────────────
void SaveStateManager::setCurrentGame(const std::string& gameTitle,
                                       const std::string& gamePath) {
    m_gameTitle = gameTitle;
    m_gamePath  = gamePath;
    ensureDir();
    std::cout << "[SaveState] Game set: " << gameTitle << "\n";
    std::cout << "[SaveState] State dir: " << stateDir() << "\n";
}

// ─── Path helpers ─────────────────────────────────────────────────────────────
std::string SaveStateManager::stateDir() const {
    // Sanitize game title for use as folder name
    std::string safe = m_gameTitle;
    for (auto& c : safe) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    return m_baseDir + safe + "/";
}

std::string SaveStateManager::slotPath(int slot) const {
    if (slot < 0) return stateDir() + "slot_auto.state";
    std::ostringstream ss;
    ss << stateDir() << "slot_" << std::setw(3) << std::setfill('0')
       << slot << ".state";
    return ss.str();
}

std::string SaveStateManager::thumbPath(int slot) const {
    if (slot < 0) return stateDir() + "slot_auto.png";
    std::ostringstream ss;
    ss << stateDir() << "slot_" << std::setw(3) << std::setfill('0')
       << slot << ".png";
    return ss.str();
}

void SaveStateManager::ensureDir() const {
    fs::create_directories(stateDir());
}

std::string SaveStateManager::formatTimestamp() const {
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return ss.str();
}

// ─── Save ─────────────────────────────────────────────────────────────────────
bool SaveStateManager::saveState(int slot, SDL_Surface* screenshot) {
    if (!m_bridge || !m_bridge->isGameLoaded()) {
        std::cerr << "[SaveState] No game loaded\n";
        return false;
    }

    ensureDir();
    std::string path = slotPath(slot);

    // Get required buffer size from core
    size_t size = m_bridge->getSerializeSize();
    if (size == 0) {
        std::cerr << "[SaveState] Core returned 0 serialize size\n";
        return false;
    }

    // Allocate buffer and serialize
    std::vector<uint8_t> buffer(size);
    if (!m_bridge->serialize(buffer.data(), size)) {
        std::cerr << "[SaveState] Core serialize failed\n";
        return false;
    }

    // Write state file
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[SaveState] Cannot write: " << path << "\n";
        return false;
    }
    f.write((char*)buffer.data(), size);
    f.close();

    // Save thumbnail if provided
    if (screenshot) {
        saveThumbnail(screenshot, thumbPath(slot));
    } else {
        // Capture from current frame
        SDL_Surface* cap = captureScreenshot();
        if (cap) {
            saveThumbnail(cap, thumbPath(slot));
            SDL_FreeSurface(cap);
        }
    }

    std::string label = (slot < 0) ? "auto-save" : "slot " + std::to_string(slot);
    std::cout << "[SaveState] Saved to " << label
              << " (" << size << " bytes)\n";
    return true;
}

// ─── Load ─────────────────────────────────────────────────────────────────────
bool SaveStateManager::loadState(int slot) {
    if (!m_bridge || !m_bridge->isGameLoaded()) {
        std::cerr << "[SaveState] No game loaded\n";
        return false;
    }

    std::string path = slotPath(slot);
    if (!fs::exists(path)) {
        std::cerr << "[SaveState] State not found: " << path << "\n";
        return false;
    }

    // Read state file
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[SaveState] Cannot read: " << path << "\n";
        return false;
    }

    std::vector<uint8_t> buffer(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    f.close();

    if (buffer.empty()) {
        std::cerr << "[SaveState] Empty state file\n";
        return false;
    }

    if (!m_bridge->unserialize(buffer.data(), buffer.size())) {
        std::cerr << "[SaveState] Core unserialize failed\n";
        return false;
    }

    std::string label = (slot < 0) ? "auto-save" : "slot " + std::to_string(slot);
    std::cout << "[SaveState] Loaded from " << label << "\n";
    return true;
}

// ─── Auto-save ────────────────────────────────────────────────────────────────
bool SaveStateManager::autoSave(SDL_Surface* screenshot) {
    return saveState(-1, screenshot);
}

bool SaveStateManager::loadAutoSave() {
    return loadState(-1);
}

bool SaveStateManager::hasAutoSave() const {
    return fs::exists(slotPath(-1));
}

// ─── List slots ───────────────────────────────────────────────────────────────
std::vector<SaveSlot> SaveStateManager::listSlots() const {
    std::vector<SaveSlot> slots;

    // Auto-save slot first
    {
        SaveSlot s;
        s.slotNumber = -1;
        s.statePath  = slotPath(-1);
        s.thumbPath  = thumbPath(-1);
        s.exists     = fs::exists(s.statePath);
        if (s.exists) {
            s.fileSize  = fs::file_size(s.statePath);
            // Get file modification time as timestamp
            auto ftime = fs::last_write_time(s.statePath);
            s.timestamp = "Auto-save";
        }
        slots.push_back(s);
    }

    // Manual slots 0-999
    for (int i = 0; i < 1000; i++) {
        std::string sp = slotPath(i);
        if (!fs::exists(sp)) continue;

        SaveSlot s;
        s.slotNumber = i;
        s.statePath  = sp;
        s.thumbPath  = thumbPath(i);
        s.exists     = true;
        s.fileSize   = fs::file_size(sp);
        s.timestamp  = "Slot " + std::to_string(i + 1);
        slots.push_back(s);
    }

    return slots;
}

SaveSlot SaveStateManager::getSlot(int slot) const {
    SaveSlot s;
    s.slotNumber = slot;
    s.statePath  = slotPath(slot);
    s.thumbPath  = thumbPath(slot);
    s.exists     = fs::exists(s.statePath);
    if (s.exists) s.fileSize = fs::file_size(s.statePath);
    return s;
}

// ─── Thumbnail ────────────────────────────────────────────────────────────────
bool SaveStateManager::saveThumbnail(SDL_Surface* screenshot,
                                      const std::string& path) const {
    if (!screenshot) return false;

    // Scale to 320x180 thumbnail
    SDL_Surface* thumb = SDL_CreateRGBSurface(
        0, 320, 180, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!thumb) return false;

    SDL_BlitScaled(screenshot, nullptr, thumb, nullptr);
    int result = IMG_SavePNG(thumb, path.c_str());
    SDL_FreeSurface(thumb);

    if (result != 0) {
        std::cerr << "[SaveState] Failed to save thumbnail: "
                  << IMG_GetError() << "\n";
        return false;
    }
    return true;
}

SDL_Texture* SaveStateManager::loadThumbnail(const SaveSlot& slot) const {
    if (!m_renderer || !fs::exists(slot.thumbPath)) return nullptr;
    SDL_Surface* surf = IMG_Load(slot.thumbPath.c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

SDL_Surface* SaveStateManager::captureScreenshot() const {
    if (!m_renderer) return nullptr;
    int w, h;
    SDL_GetRendererOutputSize(m_renderer, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurface(
        0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) return nullptr;
    if (SDL_RenderReadPixels(m_renderer, nullptr,
                              surface->format->format,
                              surface->pixels,
                              surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    return surface;
}

bool SaveStateManager::deleteSlot(int slot) {
    bool deleted = false;
    std::string sp = slotPath(slot);
    std::string tp = thumbPath(slot);
    if (fs::exists(sp)) { fs::remove(sp); deleted = true; }
    if (fs::exists(tp)) { fs::remove(tp); }
    return deleted;
}
