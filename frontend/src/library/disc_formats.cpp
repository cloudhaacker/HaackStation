#include "disc_formats.h"
#include <algorithm>
#include <fstream>
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
            d.path   = path;
            d.format = DiscFormat::PBP;
            d.valid  = false;
            d.errorReason = "PBP format not supported. Convert to CHD or BIN/CUE.";
            return d;
        }
        default: {
            DiscInfo d;
            d.path   = path;
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
    std::ifstream cue(path);
    if (!cue.is_open()) {
        d.errorReason = "Cannot open CUE file";
        return d;
    }
    fs::path cueDir = fs::path(path).parent_path();
    std::string line;
    bool foundFile = false;
    while (std::getline(cue, line)) {
        std::string upper = line;
        for (size_t i = 0; i < upper.size(); i++)
            upper[i] = (char)toupper((unsigned char)upper[i]);
        if (upper.find("FILE") == std::string::npos) continue;
        auto q1 = line.find('"');
        if (q1 == std::string::npos) continue;
        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string binName = line.substr(q1 + 1, q2 - q1 - 1);
        if (binName.empty()) continue;
        fs::path binPath = cueDir / binName;
        if (!fs::exists(binPath)) {
            d.errorReason = "BIN not found: " + binPath.string();
            return d;
        }
        foundFile = true;
        break;
    }
    if (!foundFile) {
        d.errorReason = "No FILE entry in CUE sheet";
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
    std::ifstream f(path, std::ios::binary);
    char magic[8] = {};
    f.read(magic, 8);
    if (std::string(magic, 8) != "MComprHD") {
        d.errorReason = "Not a valid CHD file";
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
        if (line.empty() || line[0] == '#') continue;
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

    auto removeWrapped = [](std::string& s, char open, char close) {
        size_t pos;
        while ((pos = s.rfind(open)) != std::string::npos) {
            size_t end = s.find(close, pos);
            if (end == std::string::npos) break;
            if (end - pos < 40)
                s.erase(pos, end - pos + 1);
            else
                break;
        }
    };

    removeWrapped(title, '(', ')');
    removeWrapped(title, '[', ']');

    for (size_t i = 0; i < title.size(); i++)
        if (title[i] == '_') title[i] = ' ';

    while (!title.empty() && title.back() == ' ')
        title.pop_back();

    size_t first = title.find_first_not_of(' ');
    if (first != std::string::npos)
        title = title.substr(first);

    return title.empty() ? filename : title;
}
