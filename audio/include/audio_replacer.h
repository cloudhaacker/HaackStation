#pragma once
// audio_replacer.h
// On-the-fly audio replacement system.
//
// How it works:
//   1. The Beetle PSX core outputs raw SPU audio chunks each frame
//   2. We hash each chunk (xxHash64, fast) and look it up in the replacement DB
//   3. If a replacement .ogg/.wav exists, we decode and mix it in instead
//   4. Timing is preserved — replaced audio follows the same presentation clock
//
// Replacement pack format (folder structure):
//   audio_replacements/
//     <game_serial>/        e.g. SCUS-94165
//       <hash_hex>.ogg      e.g. a3f1e9c2b7048d12.ogg
//       manifest.json       Optional: human-readable labels for each hash

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

struct ReplacementTrack {
    std::string path;
    int         sampleRate   = 44100;
    int         channels     = 2;
    float       volumeScale  = 1.0f;
    bool        loop         = false;

    // Decoded PCM cache (loaded on first use)
    std::vector<float> pcmData;
    bool loaded = false;
};

class AudioReplacer {
public:
    AudioReplacer();
    ~AudioReplacer();

    // Load replacement pack for a specific game serial
    bool loadPackForGame(const std::string& gameSerial);
    void unloadPack();

    // Called by the core bridge each audio frame
    // Input:  raw PS1 SPU output (16-bit stereo, 44100Hz)
    // Output: potentially replaced audio written back to the same buffer
    // Returns true if a replacement was applied
    bool processAudioFrame(int16_t* samples, int frameCount);

    bool isEnabled()   const { return m_enabled; }
    void setEnabled(bool e)  { m_enabled = e; }

    int replacementsLoaded() const { return static_cast<int>(m_tracks.size()); }

    // Replacement pack search paths
    void addSearchPath(const std::string& path);

private:
    uint64_t hashFrame(const int16_t* samples, int count) const;
    bool loadTrack(ReplacementTrack& track);
    void mixIntoBuffer(int16_t* dst, const std::vector<float>& src,
                       int frameCount, float vol);

    bool m_enabled = true;

    std::string m_currentSerial;
    std::unordered_map<uint64_t, ReplacementTrack> m_tracks;
    std::vector<std::string> m_searchPaths;
};
