#pragma once
// chd_converter.h
// ChdConverter — wraps chdman to convert BIN/CUE and ISO files to CHD format.
//
// chdman is downloaded to tools/chdman/chdman.exe — hardcoded path, no user config.
//
// After a successful conversion:
//   - Original files are moved to <romsDir>/_originals/
//   - GameScanner::rescan() is triggered so the library updates
//   - M3U files referencing the originals are rewritten to point to the CHD
//
// Undo (single-game only):
//   If _originals/ still contains the source files, delete CHD + restore them.

#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <fstream>

// ── Describes one game to convert ────────────────────────────────────────────
struct ConversionJob {
    std::string title;        // Display name (from GameEntry)
    std::string sourcePath;   // .cue or .iso path (the "lead" file)
    std::string binPath;      // .bin companion (empty for ISO jobs)
    std::string outputChd;    // Destination .chd path (same dir as source)
    std::string romsDir;      // Root ROMs directory (for _originals/ placement)
    bool        isMultiDisc = false;
    std::string m3uPath;      // Non-empty if this game has an M3U to update
};

// ── Result of a single conversion ─────────────────────────────────────────────
enum class ConversionStatus {
    OK,
    SKIPPED_ALREADY_CHD,
    FAILED_CHDMAN,
    FAILED_MOVE,
    CANCELLED
};

struct ConversionResult {
    std::string      title;
    std::string      outputChd;
    ConversionStatus status = ConversionStatus::OK;
    std::string      errorMessage;
};

// ── Space pre-flight result ───────────────────────────────────────────────────
struct SpaceCheck {
    uint64_t estimatedNeededBytes  = 0;
    uint64_t availableBytes        = 0;
    bool     likelySafe            = true;
    std::string needStr;
    std::string availStr;
};

// ── Progress callback — called on main thread via SDL event queue ─────────────
using ConvertProgressFn = std::function<void(int jobIndex, int totalJobs,
                                              int pct,
                                              const std::string& currentTitle)>;
using ConvertDoneFn = std::function<void(std::vector<ConversionResult> results)>;

class ChdConverter {
public:
    ChdConverter();
    ~ChdConverter();

    // ── Setup ────────────────────────────────────────────────────────────────
    void setChdmanPath(const std::string& path) { m_chdmanPath = path; }
    bool isChdmanAvailable() const;
    const std::string& chdmanPath() const { return m_chdmanPath; }

    // ── Space pre-flight ─────────────────────────────────────────────────────
    static SpaceCheck checkSpace(const std::vector<ConversionJob>& jobs,
                                  const std::string& romsDir);
    static std::string formatBytes(uint64_t bytes);

    // ── Single-game conversion (synchronous) ──────────────────────────────────
    ConversionResult convertOne(const ConversionJob& job,
                                 ConvertProgressFn progressFn = nullptr);

    // ── Batch conversion (async) ─────────────────────────────────────────────
    void startBatch(std::vector<ConversionJob> jobs,
                     ConvertProgressFn progressFn,
                     ConvertDoneFn     doneFn);
    void cancelBatch();
    bool isBatchRunning() const { return m_batchRunning.load(); }

    // ── Call every frame from app.cpp update() ────────────────────────────────
    void update();

    // ── Undo ─────────────────────────────────────────────────────────────────
    static bool canUndo(const ConversionJob& job);
    static bool undoConversion(const ConversionJob& job);

    // ── File operations ───────────────────────────────────────────────────────
    static bool moveToOriginals(const ConversionJob& job);
    static bool rewriteM3u(const std::string& m3uPath,
                             const std::string& oldDiscPath,
                             const std::string& newChdPath);

private:
    std::pair<int, std::string> runChdman(const ConversionJob& job,
                                           ConvertProgressFn progressFn);

    // SDL user event type for thread→main callbacks
    Uint32 m_sdlEventType;

    std::string m_chdmanPath = "tools/chdman/chdman.exe";

    std::atomic<bool>    m_batchRunning    { false };
    std::atomic<bool>    m_cancelRequested { false };
    std::thread          m_batchThread;

    ConvertProgressFn    m_progressFn;
    ConvertDoneFn        m_doneFn;

    struct PendingEvent {
        enum class Kind { PROGRESS, DONE } kind;
        int jobIndex  = 0;
        int totalJobs = 0;
        int pct       = 0;
        std::string title;
        std::vector<ConversionResult> results;
    };
};
