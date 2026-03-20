#include "per_game_settings.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

PerGameSettings::PerGameSettings() {}

bool PerGameSettings::load(const std::string& gameSerial,
                            const std::string& gamePath) {
    m_loaded = false;
    m_overrides = GameOverrides{};
    m_currentSerial = gameSerial;

    // Try serial first, then filename stem
    std::vector<std::string> attempts;
    if (!gameSerial.empty()) attempts.push_back(configPath(gameSerial));

    fs::path p(gamePath);
    std::string stem = p.stem().string();
    if (!stem.empty() && stem != gameSerial)
        attempts.push_back(configPath(stem));

    for (const auto& cfgPath : attempts) {
        if (!fs::exists(cfgPath)) continue;

        std::ifstream f(cfgPath);
        if (!f.is_open()) continue;

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            // Trim whitespace
            while (!key.empty() && key.back() == ' ') key.pop_back();
            while (!val.empty() && val.front() == ' ') val = val.substr(1);

            auto boolVal = [&](bool& flag, bool& override_flag) {
                override_flag = true;
                flag = (val == "true" || val == "1" || val == "enabled");
            };
            auto intVal = [&](int& field, bool& override_flag) {
                override_flag = true;
                try { field = std::stoi(val); } catch (...) {}
            };

            if      (key == "renderer")         intVal(m_overrides.rendererChoice,     m_overrides.overrideRenderer);
            else if (key == "internal_res")      intVal(m_overrides.internalRes,        m_overrides.overrideResolution);
            else if (key == "shader")            intVal(m_overrides.shaderChoice,       m_overrides.overrideShader);
            else if (key == "texture_replace")   boolVal(m_overrides.textureReplacement, m_overrides.overrideTextures);
            else if (key == "ai_upscaling")      boolVal(m_overrides.aiUpscaling,       m_overrides.overrideAiUpscaling);
            else if (key == "audio_replace")     boolVal(m_overrides.audioReplacement,  m_overrides.overrideAudioReplace);
            else if (key == "memcard_slot")      intVal(m_overrides.memCardSlot,        m_overrides.overrideMemCard);
        }

        m_loaded = true;
        std::cout << "[PerGame] Loaded config: " << cfgPath << "\n";
        return true;
    }

    return false;
}

void PerGameSettings::save(const std::string& gameSerial,
                            const GameOverrides& overrides) {
    if (gameSerial.empty()) return;

    std::string path = configPath(gameSerial);
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[PerGame] Could not write: " << path << "\n";
        return;
    }

    f << "# HaackStation per-game settings for " << gameSerial << "\n";
    f << "# Delete this file to revert to global settings\n\n";

    if (overrides.overrideRenderer)
        f << "renderer=" << overrides.rendererChoice << "\n";
    if (overrides.overrideResolution)
        f << "internal_res=" << overrides.internalRes << "\n";
    if (overrides.overrideShader)
        f << "shader=" << overrides.shaderChoice << "\n";
    if (overrides.overrideTextures)
        f << "texture_replace=" << (overrides.textureReplacement ? "true" : "false") << "\n";
    if (overrides.overrideAiUpscaling)
        f << "ai_upscaling=" << (overrides.aiUpscaling ? "true" : "false") << "\n";
    if (overrides.overrideAudioReplace)
        f << "audio_replace=" << (overrides.audioReplacement ? "true" : "false") << "\n";
    if (overrides.overrideMemCard)
        f << "memcard_slot=" << overrides.memCardSlot << "\n";

    std::cout << "[PerGame] Saved config: " << path << "\n";
}

std::string PerGameSettings::configPath(const std::string& serial) const {
    return m_configDir + serial + ".cfg";
}
