#pragma once
// disc_formats.h
// Identifies and validates PS1 disc image formats.
// Supports: ISO, BIN/CUE, CHD, M3U (multi-disc playlists)

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

enum class DiscFormat {
    UNKNOWN,
    ISO,        // Single .iso file
    BIN_CUE,    // .bin + .cue sheet pair
    CHD,        // Compressed Hunks of Data (.chd)
    M3U,        // Multi-disc playlist
    PBP,        // PSP/PS3 encrypted (not supported — detected to warn user)
};

struct DiscInfo {
    std::string   path;         // Path passed to the core
    std::string   displayName;  // Clean name without extension/tags
    DiscFormat    format;
    bool          valid = false;
    std::string   errorReason;  // Set if !valid

    // M3U only
    std::vector<std::string> m3uDiscs;
};

class DiscFormats {
public:
    // Detect format from extension
    static DiscFormat detectFormat(const std::string& path);

    // Full validation (checks that companion files exist, CUE parses, etc.)
    static DiscInfo validate(const std::string& path);

    // Returns a clean game title from filename
    // e.g. "Final Fantasy IX (USA) [SCUS-94922].iso" -> "Final Fantasy IX"
    static std::string cleanTitle(const std::string& filename);

    // True if this extension could be a PS1 disc image
    static bool isDiscExtension(const std::string& ext);

private:
    static DiscInfo validateIso(const std::string& path);
    static DiscInfo validateBinCue(const std::string& path);
    static DiscInfo validateChd(const std::string& path);
    static DiscInfo validateM3u(const std::string& path);
};
