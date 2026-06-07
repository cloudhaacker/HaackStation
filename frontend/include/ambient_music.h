#pragma once
// ambient_music.h
// AmbientMusicPlayer — background music for the HaackStation frontend UI.
//
// Plays a shuffled, looping playlist from a user-configured folder.
// Supports MP3, OGG, WAV, and FLAC.
//
// Lifecycle:
//   - call init() once after SDL_mixer is initialized in app.cpp
//   - call setFolder() when the music path setting changes
//   - call play()     when entering browser/hub screens
//   - call pause()    when an overlay opens (OmniSave, Settings, etc.)
//   - call resume()   when the overlay closes
//   - call fadeOut()  when a game launches
//   - call stop()     on shutdown
//
// The now-playing strip is rendered by renderStrip() — call it from each
// screen's render pass, after the main content and before the footer.
// It auto-hides if music is disabled or no tracks are found.
//
// Thread safety: all methods must be called from the main thread.
// SDL_mixer callbacks run on the audio thread — we use an atomic flag
// to signal track-end safely across threads.

#include <SDL2/SDL.h>
#include <SDL2_mixer/SDL_mixer.h>
#include <string>
#include <vector>
#include <atomic>
#include "theme_engine.h"

// Forward-declared so we don't pull in full SDL_mixer in every TU
// that includes this header.

class AmbientMusicPlayer {
public:
    AmbientMusicPlayer() = default;
    ~AmbientMusicPlayer();

    // ── Lifecycle ────────────────────────────────────────────────────────────

    // Initialize SDL_mixer. Call once after SDL_Init.
    // Returns false if SDL_mixer could not open the audio device.
    bool init();

    // Set (or change) the music folder. Re-scans immediately.
    // Accepts MP3, OGG, WAV, FLAC files. Subdirectories are ignored.
    void setFolder(const std::string& folderPath);

    // Set master volume for ambient music (0–128, SDL_mixer scale).
    // Independent from game audio volume.
    void setVolume(int vol);   // 0–128
    int  getVolume() const { return m_volume; }

    // ── Playback control ─────────────────────────────────────────────────────

    // Begin playback from the start of the shuffled playlist.
    // No-op if already playing, not initialized, or no tracks found.
    void play();

    // Pause immediately (no fade). Resume with resume().
    void pause();
    void resume();

    // Fade out over fadeMs milliseconds, then stop.
    // Default 800ms — long enough to feel intentional, short enough not to linger.
    void fadeOut(int fadeMs = 800);

    // Stop immediately. Does not reset playlist position.
    void stop();

    // ── State queries ────────────────────────────────────────────────────────

    bool isPlaying()     const;
    bool isPaused()      const { return m_paused; }
    bool isInitialized() const { return m_initialized; }
    bool hasTrack()      const { return !m_playlist.empty(); }
    int  trackCount()    const { return (int)m_playlist.size(); }

    // Display name of the current track (filename, no extension, no path).
    const std::string& currentTrackName() const { return m_currentTrackName; }

    // ── Per-frame update ─────────────────────────────────────────────────────

    // Must be called every frame from app.cpp update().
    // Handles: auto-advance to next track when current finishes,
    //          scroll animation for long track names in the strip.
    void update(float deltaMs);

    // ── UI rendering ─────────────────────────────────────────────────────────

    // Render the now-playing strip in the footer area.
    // stripX/Y: top-left corner of the strip area.
    // stripW: available width for the strip.
    // Call after main screen content, before drawFooterHints().
    void renderStrip(SDL_Renderer* renderer, ThemeEngine* theme,
                     int stripX, int stripY, int stripW);

    // ── Settings helpers ─────────────────────────────────────────────────────

    // Enable or disable ambient music globally.
    // When disabled, stop() is called immediately and renderStrip() is a no-op.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

private:
    // Scan m_folderPath for supported audio files. Builds and shuffles m_playlist.
    void scanFolder();

    // Advance to the next track in the playlist (wraps).
    void advanceTrack();

    // Load and begin playing m_playlist[m_trackIndex].
    // Frees the previous Mix_Music* if one is loaded.
    void loadAndPlayCurrent();

    // SDL_mixer music-finished callback — sets m_trackFinished atomically.
    static void onMusicFinished();

    // Pointer to the active instance for the static callback.
    // Only one AmbientMusicPlayer should exist at a time.
    static AmbientMusicPlayer* s_instance;

    bool        m_initialized  = false;
    bool        m_enabled      = true;
    bool        m_paused       = false;
    int         m_volume       = 64;   // Default ~50% (SDL_mixer max = 128)

    std::string              m_folderPath;
    std::vector<std::string> m_playlist;       // Full paths, shuffled
    int                      m_trackIndex = 0;
    Mix_Music*               m_music      = nullptr;

    std::string  m_currentTrackName;   // Display name, no path/ext

    // Track-finished flag set by the SDL_mixer audio thread callback.
    // Consumed in update() on the main thread.
    std::atomic<bool> m_trackFinished { false };

    // Strip scroll animation state (for long track names)
    float m_scrollOffset  = 0.f;   // pixels scrolled
    float m_scrollPause   = 0.f;   // pause timer before/after scroll
    bool  m_scrollForward = true;

    static constexpr float SCROLL_SPEED_PX_SEC = 40.f;
    static constexpr float SCROLL_PAUSE_SEC    = 2.0f;
    static constexpr int   STRIP_H             = 28;
    static constexpr int   STRIP_ICON_W        = 24;
};
