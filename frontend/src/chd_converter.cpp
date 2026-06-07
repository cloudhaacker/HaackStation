#include "chd_converter.h"
#include <SDL2/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

// ─── Constructor: register SDL event type ─────────────────────────────────────
ChdConverter::ChdConverter() {
    m_sdlEventType = SDL_RegisterEvents(1);
}

ChdConverter::~ChdConverter() {
    cancelBatch();
    if (m_batchThread.joinable()) m_batchThread.join();
}

// ─── chdman availability ──────────────────────────────────────────────────────
bool ChdConverter::isChdmanAvailable() const {
    std::error_code ec;
    return fs::exists(m_chdmanPath, ec);
}

// ─── Space pre-flight ─────────────────────────────────────────────────────────
SpaceCheck ChdConverter::checkSpace(const std::vector<ConversionJob>& jobs,
                                     const std::string& romsDir) {
    SpaceCheck result;
    std::error_code ec;

    for (const auto& job : jobs) {
        auto addSize = [&](const std::string& p) {
            if (!p.empty() && fs::exists(p, ec))
                result.estimatedNeededBytes += fs::file_size(p, ec);
        };
        addSize(job.sourcePath);
        addSize(job.binPath);
    }

    std::string checkPath = romsDir.empty() ? "." : romsDir;
    auto spaceInfo = fs::space(checkPath, ec);
    if (!ec) result.availableBytes = spaceInfo.available;

    result.likelySafe = (result.availableBytes >= result.estimatedNeededBytes);
    result.needStr    = formatBytes(result.estimatedNeededBytes);
    result.availStr   = formatBytes(result.availableBytes);
    return result;
}

std::string ChdConverter::formatBytes(uint64_t bytes) {
    constexpr uint64_t GB = 1024ULL * 1024 * 1024;
    constexpr uint64_t MB = 1024ULL * 1024;
    constexpr uint64_t KB = 1024ULL;
    char buf[64];
    if      (bytes >= GB) snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / GB);
    else if (bytes >= MB) snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / MB);
    else if (bytes >= KB) snprintf(buf, sizeof(buf), "%.0f KB", (double)bytes / KB);
    else                  snprintf(buf, sizeof(buf), "%llu B",  (unsigned long long)bytes);
    return buf;
}

// ─── Single conversion (synchronous) ─────────────────────────────────────────
ConversionResult ChdConverter::convertOne(const ConversionJob& job,
                                            ConvertProgressFn progressFn) {
    ConversionResult res;
    res.title     = job.title;
    res.outputChd = job.outputChd;

    if (!isChdmanAvailable()) {
        res.status       = ConversionStatus::FAILED_CHDMAN;
        res.errorMessage = "chdman not found at: " + m_chdmanPath;
        return res;
    }

    std::string ext = fs::path(job.sourcePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".chd") {
        res.status = ConversionStatus::SKIPPED_ALREADY_CHD;
        return res;
    }

    auto [exitCode, errMsg] = runChdman(job, progressFn);
    if (exitCode != 0) {
        res.status       = ConversionStatus::FAILED_CHDMAN;
        res.errorMessage = errMsg.empty()
            ? "chdman exited with code " + std::to_string(exitCode) : errMsg;
        return res;
    }

    if (!moveToOriginals(job)) {
        res.status       = ConversionStatus::FAILED_MOVE;
        res.errorMessage = "Conversion succeeded but could not move originals to _originals/";
    }

    if (!job.m3uPath.empty())
        rewriteM3u(job.m3uPath, job.sourcePath, job.outputChd);

    res.status = ConversionStatus::OK;
    std::cout << "[ChdConverter] Done: " << job.title << " -> " << job.outputChd << "\n";
    return res;
}

// ─── Batch conversion (async) ─────────────────────────────────────────────────
void ChdConverter::startBatch(std::vector<ConversionJob> jobs,
                                ConvertProgressFn progressFn,
                                ConvertDoneFn     doneFn) {
    if (m_batchRunning.load()) return;

    m_progressFn = progressFn;
    m_doneFn     = doneFn;
    m_cancelRequested.store(false);
    m_batchRunning.store(true);

    m_batchThread = std::thread([this, jobs = std::move(jobs)]() {
        std::vector<ConversionResult> results;
        int total = (int)jobs.size();

        for (int i = 0; i < total; i++) {
            if (m_cancelRequested.load()) {
                for (int j = i; j < total; j++) {
                    ConversionResult cr;
                    cr.title  = jobs[j].title;
                    cr.status = ConversionStatus::CANCELLED;
                    results.push_back(cr);
                }
                break;
            }

            // Post progress event
            if (m_sdlEventType != (Uint32)-1) {
                auto* pe      = new PendingEvent();
                pe->kind      = PendingEvent::Kind::PROGRESS;
                pe->jobIndex  = i;
                pe->totalJobs = total;
                pe->pct       = 0;
                pe->title     = jobs[i].title;
                SDL_Event ev{};
                ev.type          = SDL_USEREVENT;
                ev.user.type     = m_sdlEventType;
                ev.user.data1    = pe;
                SDL_PushEvent(&ev);
            }

            auto progressCb = [&, i, total](int, int, int pct,
                                             const std::string& t) {
                if (m_sdlEventType == (Uint32)-1) return;
                auto* p2      = new PendingEvent();
                p2->kind      = PendingEvent::Kind::PROGRESS;
                p2->jobIndex  = i;
                p2->totalJobs = total;
                p2->pct       = pct;
                p2->title     = t;
                SDL_Event ev2{};
                ev2.type          = SDL_USEREVENT;
                ev2.user.type     = m_sdlEventType;
                ev2.user.data1    = p2;
                SDL_PushEvent(&ev2);
            };

            results.push_back(convertOne(jobs[i], progressCb));
        }

        // Post done event
        if (m_sdlEventType != (Uint32)-1) {
            auto* de    = new PendingEvent();
            de->kind    = PendingEvent::Kind::DONE;
            de->results = results;
            SDL_Event ev{};
            ev.type          = SDL_USEREVENT;
            ev.user.type     = m_sdlEventType;
            ev.user.data1    = de;
            SDL_PushEvent(&ev);
        }

        m_batchRunning.store(false);
    });
}

void ChdConverter::cancelBatch() {
    m_cancelRequested.store(true);
}

// ─── update() — drain SDL events on main thread ───────────────────────────────
void ChdConverter::update() {
    if (m_sdlEventType == (Uint32)-1) return;

    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, m_sdlEventType, m_sdlEventType) > 0) {
        auto* pe = static_cast<PendingEvent*>(ev.user.data1);
        if (!pe) continue;

        if (pe->kind == PendingEvent::Kind::PROGRESS) {
            if (m_progressFn)
                m_progressFn(pe->jobIndex, pe->totalJobs, pe->pct, pe->title);
        } else if (pe->kind == PendingEvent::Kind::DONE) {
            if (m_batchThread.joinable()) m_batchThread.join();
            if (m_doneFn) m_doneFn(pe->results);
        }
        delete pe;
    }
}

// ─── runChdman ────────────────────────────────────────────────────────────────
std::pair<int, std::string> ChdConverter::runChdman(const ConversionJob& job,
                                                      ConvertProgressFn progressFn) {
    std::string ext = fs::path(job.sourcePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isIso  = (ext == ".iso");
    std::string subcmd = isIso ? "createhd" : "createcd";

    auto q = [](const std::string& p) { return "\"" + p + "\""; };
    std::string cmd = q(m_chdmanPath)
                    + " " + subcmd
                    + " -i " + q(job.sourcePath)
                    + " -o " + q(job.outputChd);

    std::cout << "[ChdConverter] Running: " << cmd << "\n";

#if defined(_WIN32)
    FILE* pipe = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif

    if (!pipe) return { -1, "Failed to start chdman process" };

    std::string fullOutput;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        fullOutput += line;
        auto pctPos = line.find('%');
        if (pctPos != std::string::npos && progressFn) {
            size_t numEnd   = pctPos;
            size_t numStart = numEnd;
            while (numStart > 0 &&
                   (isdigit((unsigned char)line[numStart-1]) || line[numStart-1] == '.'))
                numStart--;
            if (numStart < numEnd) {
                try {
                    float pct  = std::stof(line.substr(numStart, numEnd - numStart));
                    int   ipct = (int)pct;
                    ipct = ipct < 0 ? 0 : (ipct > 99 ? 99 : ipct);
                    progressFn(0, 1, ipct, job.title);
                } catch (...) {}
            }
        }
    }

#if defined(_WIN32)
    int exitCode = _pclose(pipe);
#else
    int exitCode = pclose(pipe);
#endif

    if (exitCode != 0) {
        std::string errMsg;
        std::istringstream ss(fullOutput);
        std::string ln;
        std::vector<std::string> lines;
        while (std::getline(ss, ln)) lines.push_back(ln);
        int start = (int)lines.size() - 3;
        if (start < 0) start = 0;
        for (int i = start; i < (int)lines.size(); i++)
            errMsg += lines[i] + "\n";
        return { exitCode, errMsg };
    }

    if (progressFn) progressFn(0, 1, 100, job.title);
    return { 0, "" };
}

// ─── moveToOriginals ──────────────────────────────────────────────────────────
bool ChdConverter::moveToOriginals(const ConversionJob& job) {
    fs::path originalsDir = fs::path(job.romsDir) / "_originals";
    std::error_code ec;
    fs::create_directories(originalsDir, ec);
    if (ec) {
        std::cerr << "[ChdConverter] Cannot create _originals/: " << ec.message() << "\n";
        return false;
    }

    bool ok = true;
    auto moveFile = [&](const std::string& src) {
        if (src.empty()) return;
        fs::path srcP(src);
        if (!fs::exists(srcP, ec)) return;
        fs::path dst = originalsDir / srcP.filename();
        fs::rename(srcP, dst, ec);
        if (ec) {
            std::cerr << "[ChdConverter] Move failed: " << src << ": " << ec.message() << "\n";
            ok = false;
        } else {
            std::cout << "[ChdConverter] Moved to _originals/: " << srcP.filename() << "\n";
        }
    };

    moveFile(job.sourcePath);
    moveFile(job.binPath);
    return ok;
}

// ─── rewriteM3u ───────────────────────────────────────────────────────────────
bool ChdConverter::rewriteM3u(const std::string& m3uPath,
                                const std::string& oldDiscPath,
                                const std::string& newChdPath) {
    std::error_code ec;
    if (!fs::exists(m3uPath, ec)) return false;

    std::ifstream in(m3uPath);
    if (!in.is_open()) return false;

    std::string oldFilename = fs::path(oldDiscPath).filename().string();
    std::string newFilename = fs::path(newChdPath).filename().string();

    std::vector<std::string> lines;
    std::string line;
    bool changed = false;
    while (std::getline(in, line)) {
        auto pos = line.find(oldFilename);
        if (pos != std::string::npos) {
            line.replace(pos, oldFilename.size(), newFilename);
            changed = true;
        }
        lines.push_back(line);
    }
    in.close();
    if (!changed) return true;

    std::ofstream out(m3uPath);
    if (!out.is_open()) return false;
    for (const auto& l : lines) out << l << "\n";

    std::cout << "[ChdConverter] Rewrote M3U: " << m3uPath << "\n";
    return true;
}

// ─── Undo ─────────────────────────────────────────────────────────────────────
bool ChdConverter::canUndo(const ConversionJob& job) {
    if (job.sourcePath.empty()) return false;
    fs::path originalsDir = fs::path(job.romsDir) / "_originals";
    std::error_code ec;
    fs::path origSrc = originalsDir / fs::path(job.sourcePath).filename();
    return fs::exists(origSrc, ec);
}

bool ChdConverter::undoConversion(const ConversionJob& job) {
    std::error_code ec;
    if (!job.outputChd.empty() && fs::exists(job.outputChd, ec)) {
        fs::remove(job.outputChd, ec);
        if (ec) {
            std::cerr << "[ChdConverter] Undo: could not delete CHD: " << ec.message() << "\n";
            return false;
        }
        std::cout << "[ChdConverter] Undo: deleted CHD: " << job.outputChd << "\n";
    }

    fs::path originalsDir = fs::path(job.romsDir) / "_originals";
    bool ok = true;
    auto restoreFile = [&](const std::string& origPath) {
        if (origPath.empty()) return;
        fs::path src = originalsDir / fs::path(origPath).filename();
        if (!fs::exists(src, ec)) return;
        fs::rename(src, fs::path(origPath), ec);
        if (ec) {
            std::cerr << "[ChdConverter] Undo: restore failed: " << ec.message() << "\n";
            ok = false;
        } else {
            std::cout << "[ChdConverter] Undo: restored: "
                      << fs::path(origPath).filename() << "\n";
        }
    };

    restoreFile(job.sourcePath);
    restoreFile(job.binPath);
    if (!job.m3uPath.empty())
        rewriteM3u(job.m3uPath, job.outputChd, job.sourcePath);
    return ok;
}
