#include "rewind_manager.h"
#include "libretro_bridge.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// ── Constructor ───────────────────────────────────────────────────────────────
RewindManager::RewindManager(int maxSeconds, int captureEvery)
    : m_captureEvery(std::max(1, captureEvery))
{
    // We'll size the ring buffer in init() once we know the state size.
    // Store maxSeconds so we can compute capacity then.
    // Approximate: targetFps / captureEvery * maxSeconds slots.
    // We use 60 fps as the worst-case upper bound so buffer is always deep enough.
    m_capacity = static_cast<int>(
        (60.0 / std::max(1, captureEvery)) * std::max(1, maxSeconds));
}

// ── init ──────────────────────────────────────────────────────────────────────
bool RewindManager::init(LibretroBridge* bridge) {
    reset();
    m_bridge = bridge;
    if (!bridge) return false;

    m_stateSize = bridge->getSerializeSize();
    if (m_stateSize == 0) {
        std::cerr << "[Rewind] Core reports 0 serialise size — rewind unavailable\n";
        return false;
    }

    // Allocate flat buffer: capacity * stateSize bytes
    try {
        m_buffer.resize(static_cast<std::size_t>(m_capacity) * m_stateSize, 0);
    } catch (const std::bad_alloc&) {
        std::cerr << "[Rewind] Failed to allocate rewind buffer ("
                  << m_capacity << " slots × " << m_stateSize << " bytes)\n";
        return false;
    }

    m_head        = 0;
    m_count       = 0;
    m_frameCount  = 0;
    m_ready       = true;

    std::cout << "[Rewind] Ready — " << m_capacity << " slots × "
              << m_stateSize << " bytes = "
              << (m_capacity * m_stateSize) / (1024 * 1024) << " MB\n";
    return true;
}

// ── reset ─────────────────────────────────────────────────────────────────────
void RewindManager::reset() {
    m_buffer.clear();
    m_head       = 0;
    m_count      = 0;
    m_frameCount = 0;
    m_stateSize  = 0;
    m_ready      = false;
    m_bridge     = nullptr;
}

// ── captureFrame ──────────────────────────────────────────────────────────────
bool RewindManager::captureFrame() {
    if (!m_ready) return false;

    ++m_frameCount;
    if (m_frameCount < m_captureEvery) return false;
    m_frameCount = 0;

    // Serialise directly into the head slot
    if (!m_bridge->serialize(slotPtr(m_head), m_stateSize)) {
        // Serialise failed — not fatal, just skip this frame
        return false;
    }

    // Advance head
    m_head = nextIndex(m_head);
    if (m_count < m_capacity) ++m_count;

    return true;
}

// ── stepBack ──────────────────────────────────────────────────────────────────
bool RewindManager::stepBack() {
    if (!m_ready || m_count == 0) return false;

    // The slot *before* head is the most recently written state.
    // We read that state back, then retract head so the next stepBack reads
    // the one before it (consuming the buffer backwards).
    int readIdx = prevIndex(m_head);

    if (!m_bridge->unserialize(slotPtr(readIdx), m_stateSize)) {
        std::cerr << "[Rewind] unserialize failed\n";
        return false;
    }

    // Retract
    m_head = readIdx;
    --m_count;

    return true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
uint8_t* RewindManager::slotPtr(int index) {
    return m_buffer.data() + static_cast<std::size_t>(index) * m_stateSize;
}

int RewindManager::prevIndex(int idx) const {
    return (idx - 1 + m_capacity) % m_capacity;
}

int RewindManager::nextIndex(int idx) const {
    return (idx + 1) % m_capacity;
}
