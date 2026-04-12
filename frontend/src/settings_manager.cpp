#include "settings_manager.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

SettingsManager::SettingsManager() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    m_configDir  = appdata ? std::string(appdata) + "\\HaackStation\\"
                           : ".\\";
#elif defined(__ANDROID__)
    m_configDir  = "/sdcard/HaackStation/";
#else
    const char* home = std::getenv("HOME");
    m_configDir  = home ? std::string(home) + "/.config/haackstation/"
                        : "./";
#endif
    m_configPath = m_configDir + "haackstation.cfg";
}

void SettingsManager::ensureDir() const {
    fs::create_directories(m_configDir);
}

std::string SettingsManager::configPath() const { return m_configPath; }
std::string SettingsManager::configDir()  const { return m_configDir;  }

// ─── Load ─────────────────────────────────────────────────────────────────────
bool SettingsManager::load(HaackSettings& s) {
    std::ifstream f(m_configPath);
    if (!f.is_open()) {
        std::cout << "[Settings] No config found at " << m_configPath
                  << " — using defaults\n";
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        // Skip section headers like [General]
        if (line[0] == '[') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        // ── General ──────────────────────────────────────────────────────────
        if      (key == "roms_path")          s.romsPath           = val;
        else if (key == "bios_path")          s.biosPath           = val;
        else if (key == "fullscreen")         s.fullscreen         = parseBool(val);
        else if (key == "vsync")              s.vsync              = parseBool(val);
        else if (key == "show_fps")           s.showFps            = parseBool(val);

        // ── Emulation ─────────────────────────────────────────────────────────
        else if (key == "fast_boot")          s.fastBoot           = parseBool(val);
        else if (key == "fast_forward_speed") s.fastForwardSpeed   = parseInt(val, 1);
        else if (key == "turbo_speed")        s.turboSpeed         = parseInt(val, 0);
        else if (key == "top_row_mode")       s.topRowMode         = parseInt(val, 0);

        // ── Video ─────────────────────────────────────────────────────────────
        else if (key == "renderer")           s.rendererChoice     = parseInt(val, 0);
        else if (key == "internal_res")       s.internalRes        = parseInt(val, 1);
        else if (key == "shader")             s.shaderChoice       = parseInt(val, 0);

        // ── Audio ─────────────────────────────────────────────────────────────
        else if (key == "audio_volume")       s.audioVolume        = parseInt(val, 100);
        else if (key == "audio_replacement")  s.audioReplacement   = parseBool(val);

        // ── Textures ──────────────────────────────────────────────────────────
        else if (key == "texture_replace")    s.textureReplacement = parseBool(val);
        else if (key == "ai_upscaling")       s.aiUpscaling        = parseBool(val);
        else if (key == "ai_upscale_scale")   s.aiUpscaleScale     = parseInt(val, 0);

        // ── ScreenScraper ─────────────────────────────────────────────────────
        else if (key == "ss_user")            s.ssUser             = val;
        else if (key == "ss_password")        s.ssPassword         = val;
        else if (key == "ss_dev_id")          s.ssDevId            = val;
        else if (key == "ss_dev_password")    s.ssDevPassword      = val;

        // ── RetroAchievements ─────────────────────────────────────────────────
        else if (key == "ra_user")            s.raUser             = val;
        else if (key == "ra_api_key")         s.raApiKey           = val;
        else if (key == "ra_password")        s.raPassword         = val;
        else if (key == "ra_hardcore")        s.raHardcore         = parseBool(val);
    }

    std::cout << "[Settings] Loaded from " << m_configPath << "\n";
    return true;
}

// ─── Save ─────────────────────────────────────────────────────────────────────
bool SettingsManager::save(const HaackSettings& s) {
    ensureDir();
    std::ofstream f(m_configPath);
    if (!f.is_open()) {
        std::cerr << "[Settings] Could not write config: " << m_configPath << "\n";
        return false;
    }

    f << "# HaackStation Configuration\n";
    f << "# This file is auto-generated. You can edit it manually.\n\n";

    f << "[General]\n";
    f << "roms_path="  << s.romsPath  << "\n";
    f << "bios_path="  << s.biosPath  << "\n";
    f << "fullscreen=" << (s.fullscreen ? "true" : "false") << "\n";
    f << "vsync="      << (s.vsync      ? "true" : "false") << "\n";
    f << "show_fps="   << (s.showFps    ? "true" : "false") << "\n";
    f << "\n";

    f << "[Emulation]\n";
    f << "fast_boot="          << (s.fastBoot ? "true" : "false") << "\n";
    f << "fast_forward_speed=" << s.fastForwardSpeed               << "\n";
    f << "turbo_speed="        << s.turboSpeed                     << "\n";
    f << "top_row_mode="       << s.topRowMode                     << "\n";
    f << "\n";

    f << "[Video]\n";
    f << "renderer="     << s.rendererChoice << "\n";
    f << "internal_res=" << s.internalRes    << "\n";
    f << "shader="       << s.shaderChoice   << "\n";
    f << "\n";

    f << "[Audio]\n";
    f << "audio_volume="      << s.audioVolume                            << "\n";
    f << "audio_replacement=" << (s.audioReplacement ? "true" : "false") << "\n";
    f << "\n";

    f << "[Textures]\n";
    f << "texture_replace="  << (s.textureReplacement ? "true" : "false") << "\n";
    f << "ai_upscaling="     << (s.aiUpscaling        ? "true" : "false") << "\n";
    f << "ai_upscale_scale=" << s.aiUpscaleScale                          << "\n";
    f << "\n";

    f << "[Scraper]\n";
    f << "ss_user="         << s.ssUser        << "\n";
    f << "ss_password="     << s.ssPassword    << "\n";
    f << "ss_dev_id="       << s.ssDevId       << "\n";
    f << "ss_dev_password=" << s.ssDevPassword << "\n";
    f << "\n";

    f << "[RetroAchievements]\n";
    f << "ra_user="     << s.raUser                              << "\n";
    f << "ra_api_key="  << s.raApiKey                            << "\n";
    f << "ra_password=" << s.raPassword                          << "\n";
    f << "ra_hardcore=" << (s.raHardcore ? "true" : "false")    << "\n";

    std::cout << "[Settings] Saved to " << m_configPath << "\n";
    return true;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
bool SettingsManager::parseBool(const std::string& val) {
    return val == "true" || val == "1" || val == "yes" || val == "enabled";
}

int SettingsManager::parseInt(const std::string& val, int defaultVal) {
    try { return std::stoi(val); }
    catch (...) { return defaultVal; }
}

std::string SettingsManager::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
