#include "library/disc_formats.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <iostream>

DiscFormat DiscFormats::detectFormat(const std::string& path) {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".iso") return DiscFormat::ISO;
    if (ext == ".cue") return DiscFormat::BIN_CUE;
    if (ext == ".chd") return DiscFormat::CHD;
    if (ext == ".m3u") return DiscFormat::M3U;
    if (ext == ".pbp") return DiscFormat::PBP;
    return DiscFormat::UNKNOWN;
}

bool DiscFormats::isDiscExtension(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e == ".iso" || e == ".cue" || e == ".chd" || e == ".m3u";
}

DiscInfo DiscFormats::validate(const std::string& path) {
    DiscFormat fmt = detectFormat(path);
    switch (fmt) {
        case DiscFormat::ISO:     return validateIso(path);
        case DiscFormat::BIN_CUE: return validateBinCue(path);
        case DiscFormat::CHD:     return validateChd(path);
        case DiscFormat::M3U:     return validateM3u(path);
        case DiscFormat::PBP: {
            DiscInfo d;
            d.path  = path;
            d.format = DiscFormat::PBP;
            d.valid  = false;
            d.errorReason = "PBP format is not supported (encrypted Sony format). "
                            "Please convert to CHD or BIN/CUE.";
            return d;
        }
        default: {
            DiscInfo d;
            d.path  = path;
            d.format = DiscFormat::UNKNOWN;
            d.valid  = false;
            d.errorReason = "Unknown disc format";
            return d;
        }
    }
}

DiscInfo DiscFormats::validateIso(const std::string& path) {
    DiscInfo d;
    d.path   = path;
    d.format = DiscFormat::ISO;

    if (!fs::exists(path)) {
        d.errorReason = "File not found: " + path;
        return d;
    }
    if (fs::file_size(path) < 2048) {
        d.errorReason = "File too small to be a valid ISO";
        return d;
    }

    d.displayName = cleanTitle(fs::path(path).stem().string());
    d.valid = true;
    return d;
}

DiscInfo DiscFormats::validateBinCue(const std::string& path) {
    DiscInfo d;
    d.path   = path;
    d.format = DiscFormat::BIN_CUE;

    if (!fs::exists(path)) {
        d.errorReason = "CUE file not found: " + path;
        return d;
    }

    // Parse the CUE to find the BIN filename
    std::ifstream cue(path);
    if (!cue.is_open()) {
        d.errorReason = "Cannot open CUE file";
        return d;
    }

    fs::path cueDir = fs::path(path).parent_path();
    std::string line;
    bool foundFile = false;

    while (std::getline(cue, line)) {
        // CUE FILE directive: FILE "name.bin" BINARY
        std::regex fileRx(R"(FILE\s+"?([^"]+)"?\s+BINARY)", std::regex::icase);
        std::smatch m;
        if (std::regex_search(line, m, fileRx)) {
            std::string binName = m[1].str();
            fs::path binPath = cueDir / binName;
            if (!fs::exists(binPath)) {
                d.errorReason = "BIN file referenced in CUE not found: " + binPath.string();
                return d;
            }
            foundFile = true;
            break;
        }
    }

    if (!foundFile) {
        d.errorReason = "No valid FILE entry found in CUE sheet";
        return d;
    }

    d.displayName = cleanTitle(fs::path(path).stem().string());
    d.valid = true;
    return d;
}

DiscInfo DiscFormats::validateChd(const std::string& path) {
    DiscInfo d;
    d.path   = path;
    d.format = DiscFormat::CHD;

    if (!fs::exists(path)) {
        d.errorReason = "CHD file not found: " + path;
        return d;
    }

    // Validate CHD magic bytes: "MComprHD"
    std::ifstream f(path, std::ios::binary);
    char magic[8];
    f.read(magic, 8);
    if (std::string(magic, 8) != "MComprHD") {
        d.errorReason = "Not a valid CHD file (bad magic bytes)";
        return d;
    }

    d.displayName = cleanTitle(fs::path(path).stem().string());
    d.valid = true;
    return d;
}

DiscInfo DiscFormats::validateM3u(const std::string& path) {
    DiscInfo d;
    d.path   = path;
    d.format = DiscFormat::M3U;

    if (!fs::exists(path)) {
        d.errorReason = "M3U file not found: " + path;
        return d;
    }

    std::ifstream f(path);
    std::string line;
    fs::path m3uDir = fs::path(path).parent_path();

    while (std::getline(f, line)) {
        // Skip comments and blank lines
        if (line.empty() || line[0] == '#') continue;
        // Trim \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        fs::path discPath = m3uDir / line;
        if (!fs::exists(discPath)) {
            d.errorReason = "M3U references missing file: " + discPath.string();
            return d;
        }
        d.m3uDiscs.push_back(discPath.string());
    }

    if (d.m3uDiscs.empty()) {
        d.errorReason = "M3U playlist is empty";
        return d;
    }

    d.displayName = cleanTitle(fs::path(path).stem().string());
    d.valid = true;
    return d;
}

std::string DiscFormats::cleanTitle(const std::string& filename) {
    std::string title = filename;

    // Remove region tags: (USA), (Europe), (Japan), (NTSC), (PAL), etc.
    title = std::regex_replace(title, std::regex(R"(\s*\((USA|Europe|Japan|NTSC|PAL|World)[^)]*\))"), "");
    // Remove serial numbers: [SCUS-94922], [SLUS-12345]
    title = std::regex_replace(title, std::regex(R"(\s*\[[^\]]+\])"), "");
    // Remove revision tags: (Rev 1), (v1.1)
    title = std::regex_replace(title, std::regex(R"(\s*\(Rev\s*\d+\))"), "");
    title = std::regex_replace(title, std::regex(R"(\s*\(v[\d.]+\))"), "");
    // Replace underscores with spaces
    std::replace(title.begin(), title.end(), '_', ' ');
    // Trim trailing spaces
    while (!title.empty() && title.back() == ' ') title.pop_back();

    return title.empty() ? filename : title;
}
