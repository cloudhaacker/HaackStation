#pragma once
// ─── RewindManager ────────────────────────────────────────────────────────────
// Ring-buffer rewind for HaackStation.
//
// How it works:
//   • Every N frames (captureInterval, default 2) we serialise the libretro
//     core state into a fixed-size slot in a ring buffer.
//   • On rewind, we deserialise the most-recent captured state and step the
//     ring-buffer pointer backward one slot per captureInterval frames.
//   • Buffer depth is configurable (default 10 seconds @ ~30 captures/sec).
//
// Usage:
//   manager.init(bridge);          // call once after core is loaded
//   manager.captureFrame();        // call every game frame
//   manager.stepBack();            // call every game frame while rewinding
//   manager.reset();               // call on game stop / core unload
//
#include <vector>
#include <cstddef>
#include <cstdint>

class LibretroBridge;

class RewindManager {
public:
    // maxSeconds    — ring-buffer depth in seconds of gameplay
    // captureEvery  — capture one state every N frames (2 = ~30 caps/sec @ 60fps)
    explicit RewindManager(int maxSeconds = 10, int captureEvery = 2);
    ~RewindManager() = default;

    // Call after a game is loaded. Allocates the ring buffer using the
    // core's serialisation size. Returns false if the core can't serialise.
    bool init(LibretroBridge* bridge);

    // Call on game stop / before unloading the core.
    void reset();

    // Returns true if rewind has been successfully initialised for this game.
    bool isReady() const { return m_ready; }

    // ── Per-frame API ─────────────────────────────────────────────────────────

    // Call every game frame (not during fast-forward). Captures a state every
    // m_captureEvery frames; returns true when a capture actually happened.
    bool captureFrame();

    // Restore the previous captured state. Returns true if a state was restored.
    // Call this every game frame while the rewind button is held, instead of
    // calling runFrame() on the bridge.
    bool stepBack();

    // How many captured states are currently in the buffer (0..capacity).
    int depth() const { return m_count; }

    // Capacity in number of states.
    int capacity() const { return m_capacity; }

private:
    LibretroBridge* m_bridge       = nullptr;
    int             m_captureEvery = 2;       // frames between captures
    int             m_frameCount   = 0;       // frames since last capture
    bool            m_ready        = false;

    // Ring buffer
    int             m_capacity     = 0;       // total slots
    int             m_head         = 0;       // next write index
    int             m_count        = 0;       // how many valid slots
    std::size_t     m_stateSize    = 0;       // bytes per serialised state

    // Flat storage: m_capacity * m_stateSize bytes
    std::vector<uint8_t> m_buffer;

    // Helpers
    uint8_t* slotPtr(int index);
    int prevIndex(int idx) const;
    int nextIndex(int idx) const;
};
