#pragma once
// settings_manager.h
// Saves and loads HaackSettings to a simple INI-style config file.
// Location: %APPDATA%\HaackStation\haackstation.cfg  (Windows)
//           ~/.config/haackstation/haackstation.cfg   (Linux)
//           /sdcard/HaackStation/haackstation.cfg     (Android)
//
// Format is plain text key=value, one per line.
// Unknown keys are ignored so older config files work with newer versions.

#include "settings_screen.h"
#include <string>

class SettingsManager {
public:
    SettingsManager();

    // Load settings from disk into the provided struct
    // Returns true if a config file was found and loaded
    bool load(HaackSettings& settings);

    // Save settings from the struct to disk
    bool save(const HaackSettings& settings);

    // Get the config file path for this platform
    std::string configPath() const;

    // Get the config directory (created automatically if missing)
    std::string configDir() const;

private:
    std::string m_configDir;
    std::string m_configPath;

    void ensureDir() const;

    // Parse helpers
    static bool   parseBool(const std::string& val);
    static int    parseInt(const std::string& val, int defaultVal = 0);
    static std::string trim(const std::string& s);
};
