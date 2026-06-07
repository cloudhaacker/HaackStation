#include "ambient_music.h"
#include <filesystem>
#include <algorithm>
#include <random>
#include <iostream>

namespace fs = std::filesystem;

// ─── Static instance pointer for SDL_mixer callback ──────────────────────────
AmbientMusicPlayer* AmbientMusicPlayer::s_instance = nullptr;

// ─── Destructor ───────────────────────────────────────────────────────────────
AmbientMusicPlayer::~AmbientMusicPlayer() {
    stop();
    if (m_music) {
        Mix_FreeMusic(m_music);
        m_music = nullptr;
    }
    if (m_initialized) {
        Mix_CloseAudio();
        Mix_Quit();
    }
    if (s_instance == this) s_instance = nullptr;
}

// ─── Init ─────────────────────────────────────────────────────────────────────
bool AmbientMusicPlayer::init() {
    if (m_initialized) return true;

    // Request MP3, OGG, FLAC support from SDL_mixer
    int flags = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC;
    int inited = Mix_Init(flags);
    if ((inited & MIX_INIT_MP3) == 0)
        std::cout << "[AmbientMusic] MP3 support unavailable: " << Mix_GetError() << "\n";
    if ((inited & MIX_INIT_OGG) == 0)
        std::cout << "[AmbientMusic] OGG support unavailable: " << Mix_GetError() << "\n";
    if ((inited & MIX_INIT_FLAC) == 0)
        std::cout << "[AmbientMusic] FLAC support unavailable: " << Mix_GetError() << "\n";

    // Open audio — 44100 Hz stereo, 2048-sample buffer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "[AmbientMusic] Mix_OpenAudio failed: " << Mix_GetError() << "\n";
        return false;
    }

    Mix_VolumeMusic(m_volume);
    s_instance = this;
    Mix_HookMusicFinished(onMusicFinished);

    m_initialized = true;
    std::cout << "[AmbientMusic] Initialized. Formats: MP3/OGG/WAV/FLAC\n";
    return true;
}

// ─── Folder management ────────────────────────────────────────────────────────
void AmbientMusicPlayer::setFolder(const std::string& folderPath) {
    if (m_folderPath == folderPath) return;
    m_folderPath = folderPath;
    scanFolder();

    // If we were playing, restart with the new playlist
    if (isPlaying() || m_paused) {
        stop();
        if (!m_playlist.empty() && m_enabled) play();
    }
}

void AmbientMusicPlayer::scanFolder() {
    m_playlist.clear();
    m_trackIndex = 0;

    if (m_folderPath.empty()) return;

    std::error_code ec;
    if (!fs::exists(m_folderPath, ec) || !fs::is_directory(m_folderPath, ec)) {
        std::cout << "[AmbientMusic] Music folder not found: " << m_folderPath << "\n";
        return;
    }

    static const std::vector<std::string> kExts = {
        ".mp3", ".ogg", ".wav", ".flac", ".MP3", ".OGG", ".WAV", ".FLAC"
    };

    for (const auto& entry : fs::directory_iterator(m_folderPath, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        for (const auto& e : kExts) {
            if (ext == e) {
                m_playlist.push_back(entry.path().string());
                break;
            }
        }
    }

    if (m_playlist.empty()) {
        std::cout << "[AmbientMusic] No audio files found in: " << m_folderPath << "\n";
        return;
    }

    // Shuffle playlist so it doesn't always start with the same track
    std::mt19937 rng(std::random_device{}());
    std::shuffle(m_playlist.begin(), m_playlist.end(), rng);

    std::cout << "[AmbientMusic] Found " << m_playlist.size()
              << " track(s) in: " << m_folderPath << "\n";
}

// ─── Volume ───────────────────────────────────────────────────────────────────
void AmbientMusicPlayer::setVolume(int vol) {
    m_volume = std::max(0, std::min(128, vol));
    if (m_initialized) Mix_VolumeMusic(m_volume);
}

// ─── Playback controls ────────────────────────────────────────────────────────
void AmbientMusicPlayer::play() {
    if (!m_initialized || !m_enabled || m_playlist.empty()) return;
    if (isPlaying()) return;

    m_paused = false;
    loadAndPlayCurrent();
}

void AmbientMusicPlayer::pause() {
    if (!m_initialized || !isPlaying()) return;
    Mix_PauseMusic();
    m_paused = true;
}

void AmbientMusicPlayer::resume() {
    if (!m_initialized || !m_paused) return;
    Mix_ResumeMusic();
    m_paused = false;
}

void AmbientMusicPlayer::fadeOut(int fadeMs) {
    if (!m_initialized) return;
    Mix_FadeOutMusic(fadeMs);
    m_paused = false;
}

void AmbientMusicPlayer::stop() {
    if (!m_initialized) return;
    Mix_HaltMusic();
    m_paused = false;
    if (m_music) {
        Mix_FreeMusic(m_music);
        m_music = nullptr;
    }
    m_currentTrackName.clear();
}

// ─── State queries ────────────────────────────────────────────────────────────
bool AmbientMusicPlayer::isPlaying() const {
    if (!m_initialized) return false;
    return Mix_PlayingMusic() && !Mix_PausedMusic();
}

// ─── Enable / disable ─────────────────────────────────────────────────────────
void AmbientMusicPlayer::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        fadeOut(400);
    }
    // If re-enabling: caller is responsible for calling play() at the right moment
}

// ─── Private: load and play current track ────────────────────────────────────
void AmbientMusicPlayer::loadAndPlayCurrent() {
    if (m_playlist.empty()) return;

    if (m_music) {
        Mix_FreeMusic(m_music);
        m_music = nullptr;
    }

    const std::string& path = m_playlist[m_trackIndex];
    m_music = Mix_LoadMUS(path.c_str());
    if (!m_music) {
        std::cerr << "[AmbientMusic] Failed to load: " << path
                  << " — " << Mix_GetError() << "\n";
        advanceTrack();
        return;
    }

    // Extract display name: filename without path or extension
    fs::path p(path);
    m_currentTrackName = p.stem().string();

    // Reset strip scroll
    m_scrollOffset  = 0.f;
    m_scrollPause   = SCROLL_PAUSE_SEC;
    m_scrollForward = true;

    // Fade in over 600ms for the first track; crossfade feel
    if (Mix_FadeInMusic(m_music, 1, 600) < 0) {
        std::cerr << "[AmbientMusic] Mix_FadeInMusic failed: " << Mix_GetError() << "\n";
    }

    Mix_VolumeMusic(m_volume);
    std::cout << "[AmbientMusic] Playing: " << m_currentTrackName << "\n";
}

// ─── Private: advance track ───────────────────────────────────────────────────
void AmbientMusicPlayer::advanceTrack() {
    if (m_playlist.empty()) return;
    m_trackIndex = (m_trackIndex + 1) % (int)m_playlist.size();

    // Reshuffle when we wrap around so the order changes each cycle
    if (m_trackIndex == 0) {
        std::mt19937 rng(std::random_device{}());
        std::shuffle(m_playlist.begin(), m_playlist.end(), rng);
    }

    loadAndPlayCurrent();
}

// ─── Static callback (audio thread) ──────────────────────────────────────────
void AmbientMusicPlayer::onMusicFinished() {
    // Called by SDL_mixer on the audio thread — only set the atomic flag.
    // The actual track advance happens in update() on the main thread.
    if (s_instance) s_instance->m_trackFinished.store(true);
}

// ─── Per-frame update ─────────────────────────────────────────────────────────
void AmbientMusicPlayer::update(float deltaMs) {
    if (!m_initialized || !m_enabled) return;

    // Consume track-finished flag (set by audio thread callback)
    if (m_trackFinished.exchange(false)) {
        advanceTrack();
    }

    // Animate strip scroll for long track names
    // (scroll state is reset in loadAndPlayCurrent)
    if (m_scrollPause > 0.f) {
        m_scrollPause -= deltaMs / 1000.f;
    } else {
        if (m_scrollForward) {
            m_scrollOffset += SCROLL_SPEED_PX_SEC * (deltaMs / 1000.f);
        } else {
            m_scrollOffset -= SCROLL_SPEED_PX_SEC * (deltaMs / 1000.f);
            if (m_scrollOffset <= 0.f) {
                m_scrollOffset  = 0.f;
                m_scrollPause   = SCROLL_PAUSE_SEC;
                m_scrollForward = true;
            }
        }
    }
}

// ─── Strip renderer ───────────────────────────────────────────────────────────
void AmbientMusicPlayer::renderStrip(SDL_Renderer* renderer, ThemeEngine* theme,
                                      int stripX, int stripY, int stripW) {
    if (!m_enabled || !m_initialized) return;
    if (m_playlist.empty()) return;

    const auto& pal = theme->palette();

    // Strip background — subtle, semi-transparent
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,
        pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 180);
    SDL_Rect stripRect = { stripX, stripY, stripW, STRIP_H };
    SDL_RenderFillRect(renderer, &stripRect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Music note icon (♪ rendered as text — theme font)
    SDL_Color iconCol = m_paused ? pal.textDisable : pal.accent;
    theme->drawText(m_paused ? "II" : "♪", stripX + 6, stripY + 5, iconCol, FontSize::TINY);

    // Track name — clip to strip width, scroll if too long
    if (!m_currentTrackName.empty()) {
        int maxTextW = stripW - STRIP_ICON_W - 8;

        // Measure full track name
        int tw, th;
        theme->measureText(m_currentTrackName, FontSize::TINY, tw, th);

        // Set clip rect so text doesn't overflow the strip
        SDL_Rect clip = { stripX + STRIP_ICON_W, stripY,
                          maxTextW, STRIP_H };
        SDL_RenderSetClipRect(renderer, &clip);

        // If text fits, no scroll needed; if wider, apply scroll offset
        int drawX = stripX + STRIP_ICON_W - (int)m_scrollOffset;

        // If we've scrolled to the end, start scrolling back
        if (m_scrollForward && m_scrollOffset >= (float)(tw - maxTextW + 8)) {
            // reached the end — start pause before reversing
            const_cast<AmbientMusicPlayer*>(this)->m_scrollForward = false;
            const_cast<AmbientMusicPlayer*>(this)->m_scrollPause   = SCROLL_PAUSE_SEC;
        }

        SDL_Color textCol = m_paused ? pal.textDisable : pal.textPrimary;
        theme->drawText(m_currentTrackName, drawX,
                        stripY + (STRIP_H - th) / 2,
                        textCol, FontSize::TINY);

        SDL_RenderSetClipRect(renderer, nullptr);
    } else {
        // No track loaded yet
        SDL_Color dim = pal.textDisable;
        theme->drawText("No tracks found", stripX + STRIP_ICON_W,
                        stripY + 6, dim, FontSize::TINY);
    }
}
