#pragma once
// libretro_bridge.h
// Bridges HaackStation's frontend to any libretro core.
// Primary target: Beetle PSX HW — but this layer is core-agnostic.
//
// Usage:
//   LibretroBridge bridge;
//   bridge.setBiosPath("bios/");
//   bridge.setSavePath("saves/");
//   bridge.setRenderer(sdlRenderer);
//   bridge.loadCore("core/beetle_psx_hw_libretro.dll");
//   bridge.loadGame("games/CoolGame.chd");
//   // In main loop:
//   bridge.runFrame();
//   bridge.blitFramebuffer(renderer);

#include "core_bridge/libretro_types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>

class AudioReplacer;
class TextureReplacer;

struct CoreInfo {
    std::string name;
    std::string version;
    std::string extensions;
};

class LibretroBridge {
public:
    LibretroBridge();
    ~LibretroBridge();

    void setRenderer(SDL_Renderer* r)      { m_sdlRenderer = r; }
    void setBiosPath(const std::string& p) { m_biosPath = p; }
    void setSavePath(const std::string& p) { m_savePath = p; }

    bool loadCore(const std::string& corePath);
    void unloadCore();
    bool loadGame(const std::string& gamePath);
    void unloadGame();

    void runFrame();
    void blitFramebuffer(SDL_Renderer* renderer);
    void setButtonState(int port, int buttonMask);
    void setCoreOption(const std::string& key, const std::string& value);

    void setAudioReplacer(AudioReplacer* a)    { m_audioReplacer   = a; }
    void setTextureReplacer(TextureReplacer* t) { m_textureReplacer = t; }

    bool     isCoreLoaded() const { return m_coreLoaded; }
    bool     isGameLoaded() const { return m_gameLoaded; }
    CoreInfo getCoreInfo()  const { return m_coreInfo; }

    static std::string defaultCorePath();

private:
    bool initAudio();
    void shutdownAudio();
    void setCoreOptionDefault(const std::string& key, const std::string& valuesStr);

    static void    cb_videoRefresh(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    cb_audioSample(int16_t left, int16_t right);
    static size_t  cb_audioSampleBatch(const int16_t* data, size_t frames);
    static void    cb_inputPoll();
    static int16_t cb_inputState(unsigned port, unsigned device, unsigned index, unsigned id);
    static bool    cb_environment(unsigned cmd, void* data);
    static void    cb_log(enum retro_log_level level, const char* fmt, ...);

    void* m_libHandle = nullptr;

    void   (*m_retro_init)()                          = nullptr;
    void   (*m_retro_deinit)()                        = nullptr;
    bool   (*m_retro_load_game)(const void*)          = nullptr;
    void   (*m_retro_unload_game)()                   = nullptr;
    void   (*m_retro_run)()                           = nullptr;
    void   (*m_retro_get_system_info)(void*)          = nullptr;
    void   (*m_retro_get_system_av_info)(void*)       = nullptr;
    void   (*m_retro_set_environment)(void*)          = nullptr;
    void   (*m_retro_set_video_refresh)(void*)        = nullptr;
    void   (*m_retro_set_audio_sample)(void*)         = nullptr;
    void   (*m_retro_set_audio_sample_batch)(void*)   = nullptr;
    void   (*m_retro_set_input_poll)(void*)           = nullptr;
    void   (*m_retro_set_input_state)(void*)          = nullptr;
    void   (*m_retro_set_controller_port_device)(unsigned, unsigned) = nullptr;

    bool     m_coreLoaded = false;
    bool     m_gameLoaded = false;
    CoreInfo m_coreInfo;

    SDL_Renderer*      m_sdlRenderer    = nullptr;
    SDL_Texture*       m_framebufferTex = nullptr;
    int                m_fbWidth        = 640;
    int                m_fbHeight       = 480;
    retro_pixel_format m_pixelFormat    = RETRO_PIXEL_FORMAT_RGB565;

    SDL_AudioDeviceID   m_audioDevice = 0;
    retro_system_timing m_timing      = {};

    int16_t m_inputState[2] = {0, 0};

    std::string m_biosPath = "bios/";
    std::string m_savePath = "saves/";

    std::unordered_map<std::string, std::string> m_coreOptions;
    bool m_coreOptionsUpdated = false;

    AudioReplacer*   m_audioReplacer   = nullptr;
    TextureReplacer* m_textureReplacer = nullptr;

    static LibretroBridge* s_instance;
};
