#include "disc_formats.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#ifdef HAVE_LIBCHDR
#  include <libchdr/chd.h>
#  include <libchdr/cdrom.h>
#endif

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

// ─── readSerial ───────────────────────────────────────────────────────────────
// Reads the PS1 disc serial from SYSTEM.CNF embedded in the disc image.
// PS1 SYSTEM.CNF lives at sector 23 of track 1 on every retail disc.
// The relevant line looks like:
//   BOOT2 = cdrom:\SCUS_942.55;1    (PS2-style, rare on PS1)
//   BOOT = cdrom:\SCUS_942.55;1     (PS1 standard)
// We normalise SCUS_942.55 -> SCUS-94255.

// Parse a SYSTEM.CNF text blob and return the normalised serial.
static std::string parseSystemCnf(const char* buf, size_t len) {
    // Work with the raw bytes as a string — only ASCII matters
    std::string text(buf, len);

    // Look for BOOT or BOOT2 line
    // Match BOOT/BOOT2 line and capture the serial components.
    // We intentionally skip matching the path separator (cdrom:\) because
    // std::regex ECMAScript mode treats [\:] as escaped-colon inside a char
    // class, so backslash would never match. Instead we loosely match any
    // non-newline chars between "cdrom" and the serial.
    static const std::regex re(
        R"(BOOT2?\s*=[^
]*?([A-Z]{2,4})[_A-Z]?(\d{3})[.](\d{2}))",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        // m[1]=prefix (SCUS), m[2]=first 3 digits, m[3]=last 2 digits
        return m[1].str() + "-" + m[2].str() + m[3].str();
    }
    return "";
}

// BIN/CUE: the .bin file is a raw 2352-byte/sector Mode 2 RAW image.
// Sector 23 payload starts at offset 23 * 2352 + 24 (skip 24-byte header).
// Read one 2048-byte payload from a raw BIN (2352 bytes/sector) or ISO (2048 bytes/sector).
static bool binReadSector(std::ifstream& f, uint32_t lba,
                          uint32_t sectorSize, uint32_t skip, uint8_t* out) {
    uint64_t offset = (uint64_t)lba * sectorSize + skip;
    f.seekg((std::streamoff)offset);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(out), 2048);
    return f.gcount() == 2048;
}

// Walk ISO9660 root directory in a flat file (BIN or ISO) to find SYSTEM.CNF.
static std::string readSerialFlat(const std::string& path,
                                  uint32_t sectorSize, uint32_t skip,
                                  uint32_t pregapSectors = 0) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cout << "[Serial] BIN open failed: " << path << "\n";
        return "";
    }

    uint8_t sector[2048];

    // PVD at logical sector 16 (offset by pregap if present)
    if (!binReadSector(f, 16 + pregapSectors, sectorSize, skip, sector)) {
        std::cout << "[Serial] BIN sector read failed: " << path
                  << " sec=" << (16+pregapSectors) << " skip=" << skip << "\n";
        return "";
    }
    if (sector[0] != 0x01 || memcmp(&sector[1], "CD001", 5) != 0) {
        std::cout << "[Serial] BIN PVD not found: " << path << " skip=" << skip
                  << " pregap=" << pregapSectors << " byte0=0x"
                  << std::hex << (int)sector[0] << std::dec << "\n";
        return "";
    }

    // Root directory LBA from PVD offset 156+2
    // LBA values in ISO9660 are logical (track-relative), so add pregap offset
    uint32_t rootLba = sector[156+2] | (sector[156+3] << 8) |
                       (sector[156+4] << 16) | (sector[156+5] << 24);
    if (!binReadSector(f, rootLba + pregapSectors, sectorSize, skip, sector)) {
        return "";
    }

    // Walk root directory entries for SYSTEM.CNF
    uint32_t cnfLba = 0;
    uint32_t offset = 0;
    while (offset + 33 < 2048) {
        uint8_t recLen   = sector[offset];
        if (recLen == 0) break;
        uint8_t fileFlags = sector[offset + 25];
        uint8_t nameLen   = sector[offset + 32];
        if (offset + 33 + nameLen > 2048) break;
        if (!(fileFlags & 0x02) && nameLen >= 10) {
            char name[32] = {};
            memcpy(name, &sector[offset + 33], std::min((int)nameLen, 31));
            if (strncmp(name, "SYSTEM.CNF", 10) == 0) {
                cnfLba = sector[offset+2] | (sector[offset+3] << 8) |
                         (sector[offset+4] << 16) | (sector[offset+5] << 24);
                break;
            }
        }
        offset += recLen;
    }
    if (cnfLba == 0) {
        std::cout << "[Serial] BIN SYSTEM.CNF not in root dir\n";
        return "";
    }
    if (!binReadSector(f, cnfLba + pregapSectors, sectorSize, skip, sector)) {
        std::cout << "[Serial] BIN CNF sector read failed\n";
        return "";
    }

    // Strip nulls and non-printable bytes
    std::string cleaned;
    cleaned.reserve(128);
    for (int i = 0; i < 2048; i++) {
        uint8_t b = sector[i];
        if (b >= 0x20 && b < 0x80) cleaned += (char)b;
        else if (b == '\r' || b == '\n' || b == '\t') cleaned += (char)b;
        if (cleaned.size() >= 512) break;
    }
    return parseSystemCnf(cleaned.c_str(), cleaned.size());
}

static std::string readSerialBin(const std::string& binPath) {
    // Try all combinations of:
    //   skip: 24 (MODE2_RAW) or 16 (MODE1_RAW)
    //   pregap: 0 or 150 sectors (some rips bake a 2-second pregap into the .bin)
    // readSerialFlat adds the pregapSectors offset when seeking to each logical sector.
    std::string s = readSerialFlat(binPath, 2352, 24, 0);
    if (s.empty()) s = readSerialFlat(binPath, 2352, 24, 150);
    if (s.empty()) s = readSerialFlat(binPath, 2352, 16, 0);
    if (s.empty()) s = readSerialFlat(binPath, 2352, 16, 150);
    return s;
}

// ISO: pure 2048-byte/sector image — no header skip, no pregap.
static std::string readSerialIso(const std::string& isoPath) {
    return readSerialFlat(isoPath, 2048, 0, 0);
}

#ifdef HAVE_LIBCHDR
// Read one logical sector from an open CHD into `out` (must be >= 2048 bytes).
// absSector is relative to the start of the CHD (track-relative offset added by caller).
static bool chdReadSector(chd_file* chd, uint32_t absSector,
                          uint32_t sectorsPerHunk, uint32_t unitBytes, uint32_t skip,
                          std::vector<uint8_t>& hunkBuf, uint32_t& lastHunk,
                          uint8_t* out) {
    uint32_t hunkNum = absSector / sectorsPerHunk;
    uint32_t hunkOff = absSector % sectorsPerHunk;
    if (hunkNum != lastHunk) {
        if (chd_read(chd, hunkNum, hunkBuf.data()) != CHDERR_NONE) return false;
        lastHunk = hunkNum;
    }
    if (skip + 2048u > unitBytes) return false;
    memcpy(out, hunkBuf.data() + hunkOff * unitBytes + skip, 2048);
    return true;
}

// CHD: open with libchdr, locate SYSTEM.CNF via ISO9660 root directory.
static std::string readSerialChd(const std::string& chdPath) {
    chd_file* chd = nullptr;
    chd_error openErr = chd_open(chdPath.c_str(), CHD_OPEN_READ, nullptr, &chd);
    if (openErr != CHDERR_NONE) {
        std::cout << "[Serial] CHD open failed (err=" << openErr << "): " << chdPath << "\n";
        return "";
    }

    const chd_header* hdr = chd_get_header(chd);
    if (!hdr) { chd_close(chd); return ""; }

    // Find first data track
    char meta[256] = {};
    uint32_t metaLen = 0, metaTag = 0;
    uint8_t  metaFlags = 0;
    uint32_t trackLba = 0, dataTrackLba = 0;
    uint32_t sectorSkip = 24;
    bool found = false;

    for (uint32_t idx = 0; ; idx++) {
        chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, idx,
                                         meta, sizeof(meta)-1, &metaLen, &metaTag, &metaFlags);
        if (err == CHDERR_METADATA_NOT_FOUND)
            err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, idx,
                                   meta, sizeof(meta)-1, &metaLen, &metaTag, &metaFlags);
        if (err != CHDERR_NONE) break;
        meta[metaLen] = '\0';

        int tnum = 0; char ttype[32] = {}; char tsub[32] = {}; int frames = 0;
        sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d", &tnum, ttype, tsub, &frames);

        if (!found && !strstr(ttype, "AUDIO")) {
            dataTrackLba = trackLba;
            if      (strstr(ttype, "MODE2_RAW"))   sectorSkip = 24;
            else if (strstr(ttype, "MODE1_RAW"))   sectorSkip = 16;
            else if (strstr(ttype, "MODE1"))       sectorSkip = 0;
            else if (strstr(ttype, "MODE2_FORM1")) sectorSkip = 24;
            else if (strstr(ttype, "MODE2"))       sectorSkip = 16;
            found = true;
        }
        trackLba += (uint32_t)frames;
    }

    if (!found) { chd_close(chd); return ""; }

    uint32_t unitBytes      = (hdr->unitbytes > 0) ? hdr->unitbytes : 2352;
    uint32_t sectorsPerHunk = hdr->hunkbytes / unitBytes;
    std::vector<uint8_t> hunkBuf(hdr->hunkbytes);
    uint32_t lastHunk = UINT32_MAX;
    uint8_t  sector[2048];

    // ── Step 1: Read ISO9660 Primary Volume Descriptor at sector 16 ──────────
    // PVD is always at logical sector 16 of the data track.
    if (!chdReadSector(chd, dataTrackLba + 16, sectorsPerHunk, unitBytes,
                       sectorSkip, hunkBuf, lastHunk, sector)) {
        chd_close(chd); return "";
    }
    // PVD starts with \x01CD001 — verify it's actually a PVD
    if (sector[0] != 0x01 || memcmp(&sector[1], "CD001", 5) != 0) {
        // Try skip+8 (duplicated sub-header variant)
        sectorSkip += 8;
        if (!chdReadSector(chd, dataTrackLba + 16, sectorsPerHunk, unitBytes,
                           sectorSkip, hunkBuf, lastHunk, sector)) {
            chd_close(chd); return "";
        }
        if (sector[0] != 0x01 || memcmp(&sector[1], "CD001", 5) != 0) {
            std::cout << "[Serial] CHD: PVD not found at sector 16\n";
            chd_close(chd); return "";
        }
    }

    // ── Step 2: Get root directory LBA from PVD ───────────────────────────────
    // Root directory record is at offset 156 in the PVD.
    // LBA of root directory extent is at offset 2 within the record (LE uint32).
    uint32_t rootLba = sector[156 + 2] | (sector[156 + 3] << 8) |
                       (sector[156 + 4] << 16) | (sector[156 + 5] << 24);

    // ── Step 3: Walk root directory to find SYSTEM.CNF ────────────────────────
    if (!chdReadSector(chd, dataTrackLba + rootLba, sectorsPerHunk, unitBytes,
                       sectorSkip, hunkBuf, lastHunk, sector)) {
        chd_close(chd); return "";
    }

    uint32_t cnfLba = 0;
    uint32_t offset = 0;
    while (offset + 33 < 2048) {
        uint8_t recLen = sector[offset];
        if (recLen == 0) break;

        uint8_t  fileFlags  = sector[offset + 25];
        uint8_t  nameLen    = sector[offset + 32];
        if (offset + 33 + nameLen > 2048) break;

        // Skip directories (bit 1 of flags set)
        if (!(fileFlags & 0x02) && nameLen >= 10) {
            char name[32] = {};
            memcpy(name, &sector[offset + 33], std::min((int)nameLen, 31));
            // ISO9660 names are uppercase; SYSTEM.CNF has version suffix ";1"
            if (strncmp(name, "SYSTEM.CNF", 10) == 0) {
                cnfLba = sector[offset + 2] | (sector[offset + 3] << 8) |
                         (sector[offset + 4] << 16) | (sector[offset + 5] << 24);
                break;
            }
        }
        offset += recLen;
    }

    if (cnfLba == 0) {
        std::cout << "[Serial] CHD: SYSTEM.CNF not found in root directory\n";
        chd_close(chd); return "";
    }

    // ── Step 4: Read SYSTEM.CNF and parse the serial ──────────────────────────
    if (!chdReadSector(chd, dataTrackLba + cnfLba, sectorsPerHunk, unitBytes,
                       sectorSkip, hunkBuf, lastHunk, sector)) {
        chd_close(chd); return "";
    }

    // Strip null bytes and non-printable chars
    std::string cleaned;
    cleaned.reserve(128);
    for (int i = 0; i < 2048; i++) {
        uint8_t b = sector[i];
        if (b >= 0x20 && b < 0x80) cleaned += (char)b;
        else if (b == '\r' || b == '\n' || b == '\t') cleaned += (char)b;
        if (cleaned.size() >= 512) break;
    }

    std::string serial = parseSystemCnf(cleaned.c_str(), cleaned.size());
    if (!serial.empty())
        std::cout << "[Serial] Found via ISO9660: " << serial << "\n";
    else
        std::cout << "[Serial] SYSTEM.CNF found but parse failed. Content: '"
                  << cleaned.substr(0, 80) << "'\n";

    chd_close(chd);
    return serial;
}
#endif // HAVE_LIBCHDR

std::string DiscFormats::readSerial(const std::string& path) {
    DiscFormat fmt = detectFormat(path);

    if (fmt == DiscFormat::M3U) {
        // Read serial from the FIRST disc line only — do not fall through to
        // subsequent discs, which could belong to a different game if disc 1 fails.
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            fs::path discPath = fs::path(path).parent_path() / line;
            return readSerial(discPath.string()); // first disc only, succeed or fail
        }
        return "";
    }

    if (fmt == DiscFormat::BIN_CUE) {
        // Parse the CUE to find the first .bin file, then read from it
        std::ifstream cue(path);
        if (!cue.is_open()) return "";
        fs::path cueDir = fs::path(path).parent_path();
        std::string line;
        while (std::getline(cue, line)) {
            std::string upper = line;
            for (auto& c : upper) c = (char)toupper((unsigned char)c);
            if (upper.find("FILE") == std::string::npos) continue;
            auto q1 = line.find('"');
            if (q1 == std::string::npos) continue;
            auto q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string binName = line.substr(q1 + 1, q2 - q1 - 1);
            if (!binName.empty())
                return readSerialBin((cueDir / binName).string());
        }
        return "";
    }

    if (fmt == DiscFormat::CHD) {
#ifdef HAVE_LIBCHDR
        return readSerialChd(path);
#else
        return "";  // CHD serial requires libchdr
#endif
    }

    if (fmt == DiscFormat::ISO) {
        return readSerialIso(path);
    }

    return "";
}
