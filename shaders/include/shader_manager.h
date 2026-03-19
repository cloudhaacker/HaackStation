#pragma once
// shader_manager.h
// Manages GLSL shader packs (CRT, scanlines, upscale filters, etc.)
// Compatible with the libretro slangp / glslp shader preset format
// so existing RetroArch shader packs work out of the box.

#include <string>
#include <vector>
#include <unordered_map>

struct ShaderPreset {
    std::string name;
    std::string path;       // .glslp or .slangp file
    std::string description;
    std::string author;
    bool        supportsVulkan = false;
    bool        supportsGL     = true;
};

enum class BuiltinShader {
    NONE,           // Raw output (no shader)
    SHARP_BILINEAR, // Crisp upscale, slight smoothing
    CRT_LOTTES,     // Classic CRT phosphor simulation
    CRT_ROYALE,     // High-quality CRT with scanlines + curvature
    SCANLINES_SIMPLE, // Lightweight scanlines
    LCD_GRID,       // LCD grid effect
    XBRZ_FREESCALE, // xBRZ pixel-art upscaler
};

class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    // Load all .glslp/.slangp presets from a folder
    void scanShaderPacks(const std::string& directory);

    // Switch to a shader pack by index or name
    bool applyPreset(const std::string& name);
    bool applyBuiltin(BuiltinShader shader);
    void clearShader();

    const std::vector<ShaderPreset>& availablePresets() const { return m_presets; }
    const ShaderPreset* activePreset() const { return m_activePreset; }
    BuiltinShader activeBuiltin() const { return m_activeBuiltin; }

    void addSearchPath(const std::string& path);

private:
    void loadBuiltinPresets();

    std::vector<ShaderPreset>         m_presets;
    std::vector<std::string>          m_searchPaths;
    const ShaderPreset*               m_activePreset  = nullptr;
    BuiltinShader                     m_activeBuiltin = BuiltinShader::NONE;
};
