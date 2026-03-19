#pragma once
// texture_replacer.h
// High-resolution texture replacement system.
//
// How it works:
//   1. The GPU backend intercepts texture uploads from the PS1 VRAM
//   2. Each texture region is hashed (content + palette hash)
//   3. If a replacement PNG exists in the pack, it is loaded and substituted
//   4. Replacements are cached in GPU memory for the session
//
// Pack format:
//   texture_packs/
//     <game_serial>/
//       textures/
//         <hash_hex>.png       Direct replacement
//         <hash_hex>_norm.png  Optional normal map
//       pack.json              Pack metadata (name, author, version)

#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>

struct TexturePack {
    std::string name;
    std::string author;
    std::string version;
    std::string gameSerial;
    int         textureCount = 0;
};

struct ReplacementTexture {
    uint32_t gpuTextureId = 0;   // OpenGL/Vulkan handle
    int      width  = 0;
    int      height = 0;
    bool     loaded = false;
    std::string path;
};

class TextureReplacer {
public:
    TextureReplacer();
    ~TextureReplacer();

    // Load pack for game
    bool loadPackForGame(const std::string& gameSerial);
    void unloadPack();

    // Called by GPU backend when a texture is about to be drawn
    // Returns nullptr if no replacement, or pointer to replacement texture
    const ReplacementTexture* getReplacementTexture(uint64_t textureHash);

    // Called by GPU backend: register a hash seen this frame (for preloading)
    void notifyTextureUsed(uint64_t hash);

    bool isEnabled() const   { return m_enabled; }
    void setEnabled(bool e)  { m_enabled = e; }

    const TexturePack* currentPack() const { return m_pack.get(); }
    void addSearchPath(const std::string& path);

    // AI upscaling: submit a raw PS1 texture for upscaling and cache result
    // (Calls into the NCNN upscaler asynchronously)
    void requestAiUpscale(uint64_t hash, const std::vector<uint8_t>& rawPixels,
                          int w, int h);

private:
    bool loadTextureFromFile(ReplacementTexture& tex);

    bool m_enabled = true;

    std::unique_ptr<TexturePack> m_pack;
    std::unordered_map<uint64_t, ReplacementTexture> m_cache;
    std::vector<std::string> m_searchPaths;
    std::string m_currentSerial;
};
