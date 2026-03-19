#include "libretro_bridge.h"
#include "libretro_types.h"
#include "audio_replacer.h"
#include "texture_replacer.h"
#include <iostream>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// ─── Platform dynamic library loading ────────────────────────────────────────
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #define DL_OPEN(path)       (void*)LoadLibraryA(path)
    #define DL_SYM(lib, sym)    (void*)GetProcAddress((HMODULE)(lib), sym)
    #define DL_CLOSE(lib)       FreeLibrary((HMODULE)(lib))
    #define DL_ERROR()          "LoadLibrary failed"
    #define CORE_EXT            ".dll"
#else
    #include <dlfcn.h>
    #define DL_OPEN(path)       dlopen(path, RTLD_LAZY)
    #define DL_SYM(lib, sym)    dlsym(lib, sym)
    #define DL_CLOSE(lib)       dlclose(lib)
    #define DL_ERROR()          dlerror()
    #define CORE_EXT            ".so"
#endif

// ─── Singleton instance (needed for static C callbacks) ───────────────────────
LibretroBridge* LibretroBridge::s_instance = nullptr;

// ─── Helper macro to load a function pointer from the core ───────────────────
#define LOAD_SYM(var, name) \
    var = (decltype(var))DL_SYM(m_libHandle, #name); \
    if (!var) { \
        std::cerr << "[Bridge] Missing symbol: " #name "\n"; \
        return false; \
    }

// ─── Constructor / Destructor ─────────────────────────────────────────────────
LibretroBridge::LibretroBridge() {
    s_instance = this;
}

LibretroBridge::~LibretroBridge() {
    unloadGame();
    unloadCore();
    if (s_instance == this) s_instance = nullptr;
}

// ─── Core loading ─────────────────────────────────────────────────────────────
bool LibretroBridge::loadCore(const std::string& corePath) {
    if (m_coreLoaded) unloadCore();

    std::cout << "[Bridge] Loading core: " << corePath << "\n";

    m_libHandle = DL_OPEN(corePath.c_str());
    if (!m_libHandle) {
        std::cerr << "[Bridge] Failed to load core library: " << DL_ERROR() << "\n";
        return false;
    }

    // Load all required function pointers
    LOAD_SYM(m_retro_init,                   retro_init);
    LOAD_SYM(m_retro_deinit,                 retro_deinit);
    LOAD_SYM(m_retro_load_game,              retro_load_game);
    LOAD_SYM(m_retro_unload_game,            retro_unload_game);
    LOAD_SYM(m_retro_run,                    retro_run);
    LOAD_SYM(m_retro_get_system_info,        retro_get_system_info);
    LOAD_SYM(m_retro_get_system_av_info,     retro_get_system_av_info);
    LOAD_SYM(m_retro_set_environment,        retro_set_environment);
    LOAD_SYM(m_retro_set_video_refresh,      retro_set_video_refresh);
    LOAD_SYM(m_retro_set_audio_sample,       retro_set_audio_sample);
    LOAD_SYM(m_retro_set_audio_sample_batch, retro_set_audio_sample_batch);
    LOAD_SYM(m_retro_set_input_poll,         retro_set_input_poll);
    LOAD_SYM(m_retro_set_input_state,        retro_set_input_state);
    LOAD_SYM(m_retro_set_controller_port_device, retro_set_controller_port_device);

    // Register our callbacks with the core
    m_retro_set_environment        ((void*)cb_environment);
    m_retro_set_video_refresh      ((void*)cb_videoRefresh);
    m_retro_set_audio_sample       ((void*)cb_audioSample);
    m_retro_set_audio_sample_batch ((void*)cb_audioSampleBatch);
    m_retro_set_input_poll         ((void*)cb_inputPoll);
    m_retro_set_input_state        ((void*)cb_inputState);

    // Initialize the core
    m_retro_init();

    // Read core info
    retro_system_info sysInfo = {};
    m_retro_get_system_info(&sysInfo);
    m_coreInfo.name       = sysInfo.library_name    ? sysInfo.library_name    : "Unknown";
    m_coreInfo.version    = sysInfo.library_version ? sysInfo.library_version : "Unknown";
    m_coreInfo.extensions = sysInfo.valid_extensions? sysInfo.valid_extensions: "";

    m_coreLoaded = true;
    std::cout << "[Bridge] Core loaded: " << m_coreInfo.name
              << " v" << m_coreInfo.version << "\n";
    return true;
}

void LibretroBridge::unloadCore() {
    if (!m_coreLoaded) return;
    if (m_gameLoaded) unloadGame();

    m_retro_deinit();
    DL_CLOSE(m_libHandle);
    m_libHandle  = nullptr;
    m_coreLoaded = false;

    // Clear all function pointers
    m_retro_init                   = nullptr;
    m_retro_deinit                 = nullptr;
    m_retro_load_game              = nullptr;
    m_retro_unload_game            = nullptr;
    m_retro_run                    = nullptr;
    m_retro_get_system_info        = nullptr;
    m_retro_get_system_av_info     = nullptr;
    m_retro_set_environment        = nullptr;
    m_retro_set_video_refresh      = nullptr;
    m_retro_set_audio_sample       = nullptr;
    m_retro_set_audio_sample_batch = nullptr;
    m_retro_set_input_poll         = nullptr;
    m_retro_set_input_state        = nullptr;
    m_retro_set_controller_port_device = nullptr;

    std::cout << "[Bridge] Core unloaded\n";
}

// ─── Game loading ─────────────────────────────────────────────────────────────
bool LibretroBridge::loadGame(const std::string& gamePath) {
    if (!m_coreLoaded) {
        // Auto-load the default core if none is loaded yet
        if (!loadCore(defaultCorePath())) {
            std::cerr << "[Bridge] No core loaded and default core not found\n";
            return false;
        }
    }

    if (m_gameLoaded) unloadGame();

    std::cout << "[Bridge] Loading game: " << gamePath << "\n";

    retro_game_info gameInfo = {};
    gameInfo.path = gamePath.c_str();
    gameInfo.data = nullptr;
    gameInfo.size = 0;
    gameInfo.meta = nullptr;

    if (!m_retro_load_game(&gameInfo)) {
        std::cerr << "[Bridge] retro_load_game() returned false\n";
        std::cerr << "[Bridge] Check: BIOS files in bios/ folder?\n";
        std::cerr << "[Bridge] Check: Game file not corrupted?\n";
        return false;
    }

    // Read AV info (resolution, framerate, sample rate)
    retro_system_av_info avInfo = {};
    m_retro_get_system_av_info(&avInfo);

    m_fbWidth  = avInfo.geometry.base_width;
    m_fbHeight = avInfo.geometry.base_height;
    m_timing   = avInfo.timing;

    std::cout << "[Bridge] Game loaded — "
              << m_fbWidth << "x" << m_fbHeight
              << " @ " << avInfo.timing.fps << "fps"
              << " audio: " << avInfo.timing.sample_rate << "Hz\n";

    // Set up controller ports
    m_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    m_retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);

    // Initialize audio with the sample rate the core told us about
    shutdownAudio();  // Close any previous device
    if (!initAudio()) {
        std::cerr << "[Bridge] Warning: Audio initialization failed\n";
    }

    m_gameLoaded = true;
    return true;
}

void LibretroBridge::unloadGame() {
    if (!m_gameLoaded) return;
    m_retro_unload_game();
    m_gameLoaded = false;

    if (m_framebufferTex) {
        SDL_DestroyTexture(m_framebufferTex);
        m_framebufferTex = nullptr;
    }

    std::cout << "[Bridge] Game unloaded\n";
}

// ─── Per-frame emulation ──────────────────────────────────────────────────────
void LibretroBridge::runFrame() {
    if (!m_gameLoaded) return;
    m_retro_run();
}

// ─── Blit framebuffer to SDL ──────────────────────────────────────────────────
void LibretroBridge::blitFramebuffer(SDL_Renderer* renderer) {
    if (!m_framebufferTex) return;

    int winW, winH;
    SDL_GetRendererOutputSize(renderer, &winW, &winH);

    // Letterbox: maintain PS1 aspect ratio (4:3)
    float aspect    = 4.f / 3.f;
    int   destW     = winW;
    int   destH     = (int)(winW / aspect);

    if (destH > winH) {
        destH = winH;
        destW = (int)(winH * aspect);
    }

    SDL_Rect dst = {
        (winW - destW) / 2,
        (winH - destH) / 2,
        destW, destH
    };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, m_framebufferTex, nullptr, &dst);
}

// ─── Input ────────────────────────────────────────────────────────────────────
void LibretroBridge::setButtonState(int port, int buttonMask) {
    if (port >= 0 && port < 2)
        m_inputState[port] = (int16_t)buttonMask;
}

// ─── Default core path ────────────────────────────────────────────────────────
std::string LibretroBridge::defaultCorePath() {
#ifdef _WIN32
    // Beetle PSX HW is distributed as mednafen_psx_hw on the libretro buildbot
    // Both names refer to the exact same core — same code, same accuracy
    return "core/mednafen_psx_hw_libretro.dll";
#elif defined(__ANDROID__)
    return "core/mednafen_psx_hw_libretro_android.so";
#else
    return "core/mednafen_psx_hw_libretro.so";
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
// STATIC CALLBACKS
// These are called BY the core. s_instance lets them reach the bridge object.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Video callback ───────────────────────────────────────────────────────────
// Called every frame with the PS1's rendered output.
// We upload it to an SDL texture so blitFramebuffer() can display it.
void LibretroBridge::cb_videoRefresh(const void* data, unsigned width,
                                      unsigned height, size_t pitch)
{
    if (!s_instance || !data) return;
    LibretroBridge& b = *s_instance;

    // Create or resize the framebuffer texture if needed
    if (!b.m_framebufferTex ||
        (int)width  != b.m_fbWidth ||
        (int)height != b.m_fbHeight)
    {
        if (b.m_framebufferTex) {
            SDL_DestroyTexture(b.m_framebufferTex);
            b.m_framebufferTex = nullptr;
        }

        // Use the pixel format the core told us about via environment callback
        SDL_PixelFormatEnum fmt = SDL_PIXELFORMAT_RGB565;
        if (b.m_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888)
            fmt = SDL_PIXELFORMAT_ARGB8888;
        else if (b.m_pixelFormat == RETRO_PIXEL_FORMAT_0RGB1555)
            fmt = SDL_PIXELFORMAT_ARGB1555;

        b.m_framebufferTex = SDL_CreateTexture(
            b.m_sdlRenderer,
            fmt,
            SDL_TEXTUREACCESS_STREAMING,
            width, height
        );
        b.m_fbWidth  = width;
        b.m_fbHeight = height;

        if (!b.m_framebufferTex) {
            std::cerr << "[Bridge] Failed to create framebuffer texture: "
                      << SDL_GetError() << "\n";
            return;
        }
    }

    // Upload this frame's pixel data
    SDL_UpdateTexture(b.m_framebufferTex, nullptr, data, (int)pitch);
}

// ─── Audio callbacks ──────────────────────────────────────────────────────────
// cb_audioSample: called for single stereo samples (rare)
void LibretroBridge::cb_audioSample(int16_t left, int16_t right) {
    if (!s_instance) return;
    int16_t buf[2] = { left, right };
    cb_audioSampleBatch(buf, 1);
}

// cb_audioSampleBatch: called with a batch of stereo samples each frame
// This is the main audio path. We pass through the audio replacer if active.
size_t LibretroBridge::cb_audioSampleBatch(const int16_t* data, size_t frames) {
    if (!s_instance) return frames;

    // Audio replacement hook: if a pack is loaded and a replacement
    // exists for this audio hash, it will swap the samples in-place
    if (s_instance->m_audioReplacer && s_instance->m_audioReplacer->isEnabled()) {
        // Work on a mutable copy so the replacer can modify it
        std::vector<int16_t> buf(data, data + frames * 2);
        s_instance->m_audioReplacer->processAudioFrame(buf.data(), (int)frames);
        SDL_QueueAudio(s_instance->m_audioDevice, buf.data(), (Uint32)(frames * 2 * sizeof(int16_t)));
    } else {
        SDL_QueueAudio(s_instance->m_audioDevice, data, (Uint32)(frames * 2 * sizeof(int16_t)));
    }

    return frames;
}

// ─── Input callbacks ──────────────────────────────────────────────────────────
// cb_inputPoll: called once per frame before input is read
void LibretroBridge::cb_inputPoll() {
    // Input state is already updated by the frontend each frame
    // via setButtonState() — nothing to do here
}

// cb_inputState: called for each button the core wants to query
int16_t LibretroBridge::cb_inputState(unsigned port, unsigned device,
                                       unsigned /*index*/, unsigned id)
{
    if (!s_instance) return 0;
    if (port >= 2)   return 0;
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    if (id >= 16)    return 0;

    // Return 1 if the button at position 'id' is pressed
    return (s_instance->m_inputState[port] >> id) & 1;
}

// ─── Environment callback ─────────────────────────────────────────────────────
// This is the core's way of asking the frontend for things it needs:
// log interface, pixel format, system directory, BIOS path, variables, etc.
bool LibretroBridge::cb_environment(unsigned cmd, void* data) {
    if (!s_instance) return false;
    LibretroBridge& b = *s_instance;

    switch (cmd) {

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            auto* cb = (retro_log_callback*)data;
            cb->log = cb_log;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            auto fmt = *(const retro_pixel_format*)data;
            b.m_pixelFormat = fmt;
            std::cout << "[Bridge] Pixel format: " << (int)fmt << "\n";
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            // This is where the core looks for BIOS files
            *(const char**)data = b.m_biosPath.c_str();
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            *(const char**)data = b.m_savePath.c_str();
            return true;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
            *(bool*)data = true;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            // Core is registering its option keys — store them
            const auto* vars = (const retro_variable*)data;
            while (vars && vars->key) {
                // Set sensible defaults for Beetle PSX HW options
                b.setCoreOptionDefault(vars->key, vars->value);
                vars++;
            }
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* var = (retro_variable*)data;
            if (!var || !var->key) return false;
            auto it = b.m_coreOptions.find(var->key);
            if (it != b.m_coreOptions.end()) {
                var->value = it->second.c_str();
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            *(bool*)data = b.m_coreOptionsUpdated;
            b.m_coreOptionsUpdated = false;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            // Hardware renderer request — return false to decline
            // This tells the core to fall back to software rendering
            // Full HW renderer (OpenGL context setup) is Phase 3
            std::cout << "[Bridge] HW render requested — using software fallback\n";
            return false;
        }

        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
            // Multi-disc swapping — will be wired in Phase 3
            return true;

        case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: {
            *(uint64_t*)data = (1 << RETRO_DEVICE_JOYPAD);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_MESSAGE: {
            const auto* msg = (const retro_message*)data;
            if (msg && msg->msg)
                std::cout << "[Core] " << msg->msg << "\n";
            return true;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN:
            std::cout << "[Bridge] Core requested shutdown\n";
            return true;

        default:
            return false;
    }
}

// ─── Log callback ─────────────────────────────────────────────────────────────
void LibretroBridge::cb_log(enum retro_log_level level, const char* fmt, ...) {
    const char* prefix = "[Core] ";
    switch (level) {
        case RETRO_LOG_DEBUG: prefix = "[Core:D] "; break;
        case RETRO_LOG_INFO:  prefix = "[Core:I] "; break;
        case RETRO_LOG_WARN:  prefix = "[Core:W] "; break;
        case RETRO_LOG_ERROR: prefix = "[Core:E] "; break;
        default: break;
    }

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Errors and warnings always print, debug only in debug builds
#ifdef NDEBUG
    if (level >= RETRO_LOG_WARN)
#endif
    std::cout << prefix << buf;
}

// ─── Core option helpers ──────────────────────────────────────────────────────
void LibretroBridge::setCoreOptionDefault(const std::string& key,
                                           const std::string& valuesStr)
{
    // The values string format is "label; val1|val2|val3"
    // We want the first actual value after the semicolon
    auto semi = valuesStr.find(';');
    if (semi == std::string::npos) {
        m_coreOptions[key] = valuesStr;
        return;
    }
    std::string vals = valuesStr.substr(semi + 2); // skip "; "
    auto pipe = vals.find('|');
    m_coreOptions[key] = (pipe != std::string::npos) ? vals.substr(0, pipe) : vals;
}

void LibretroBridge::setCoreOption(const std::string& key, const std::string& value) {
    m_coreOptions[key]  = value;
    m_coreOptionsUpdated = true;
}

// ─── SDL audio device setup ───────────────────────────────────────────────────
bool LibretroBridge::initAudio() {
    SDL_AudioSpec want = {}, got = {};
    want.freq     = (int)m_timing.sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = nullptr;  // We use SDL_QueueAudio

    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (m_audioDevice == 0) {
        std::cerr << "[Bridge] SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_PauseAudioDevice(m_audioDevice, 0);  // Start playing
    std::cout << "[Bridge] Audio: " << got.freq << "Hz stereo\n";
    return true;
}

void LibretroBridge::shutdownAudio() {
    if (m_audioDevice) {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }
}
