#pragma once
// libretro_types.h
// Minimal self-contained libretro API definitions.
// This lets us compile the bridge without needing the full beetle submodule
// present yet. When beetle is added as a submodule, this file stays — it's
// a clean subset of the official libretro.h that covers everything we use.
//
// Source: https://github.com/libretro/libretro-common/blob/master/include/libretro.h
// License: MIT

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// ─── Core environment commands ─────────────────────────────────────────────────
#define RETRO_ENVIRONMENT_SET_ROTATION                1
#define RETRO_ENVIRONMENT_GET_OVERSCAN                2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE                3
#define RETRO_ENVIRONMENT_SET_MESSAGE                 6
#define RETRO_ENVIRONMENT_SHUTDOWN                    7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL       8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY        9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT            10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS       11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK       12
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE  13
#define RETRO_ENVIRONMENT_SET_HW_RENDER               14
#define RETRO_ENVIRONMENT_GET_VARIABLE                15
#define RETRO_ENVIRONMENT_SET_VARIABLES               16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE         17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME         18
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH           19
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK          22
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK     21
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE        23
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES 24
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE           27
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE          28
#define RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE      29
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY          31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO          32
#define RETRO_ENVIRONMENT_GET_USERNAME                38
#define RETRO_ENVIRONMENT_GET_LANGUAGE                39
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER 40
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT       44
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE           45
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION    52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS            53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL       54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY    55

// ─── Pixel formats ────────────────────────────────────────────────────────────
enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,  // Native PS1 format
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565   = 2,
    RETRO_PIXEL_FORMAT_UNKNOWN  = INT32_MAX
};

// ─── Log levels ───────────────────────────────────────────────────────────────
enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = INT32_MAX
};

// ─── Device types ────────────────────────────────────────────────────────────
#define RETRO_DEVICE_NONE       0
#define RETRO_DEVICE_JOYPAD     1
#define RETRO_DEVICE_MOUSE      2
#define RETRO_DEVICE_KEYBOARD   3
#define RETRO_DEVICE_LIGHTGUN   4
#define RETRO_DEVICE_ANALOG     5
#define RETRO_DEVICE_POINTER    6

// ─── Joypad button IDs (PS1 mapping) ─────────────────────────────────────────
#define RETRO_DEVICE_ID_JOYPAD_B        0   // Cross
#define RETRO_DEVICE_ID_JOYPAD_Y        1   // Square
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8   // Circle
#define RETRO_DEVICE_ID_JOYPAD_X        9   // Triangle
#define RETRO_DEVICE_ID_JOYPAD_L        10  // L1
#define RETRO_DEVICE_ID_JOYPAD_R        11  // R1
#define RETRO_DEVICE_ID_JOYPAD_L2       12
#define RETRO_DEVICE_ID_JOYPAD_R2       13
#define RETRO_DEVICE_ID_JOYPAD_L3       14
#define RETRO_DEVICE_ID_JOYPAD_R3       15

// ─── Core structs ────────────────────────────────────────────────────────────
struct retro_system_info {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;
    bool        need_fullpath;
    bool        block_extract;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float    aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    retro_game_geometry geometry;
    retro_system_timing timing;
};

struct retro_game_info {
    const char* path;
    const void* data;
    size_t      size;
    const char* meta;
};

struct retro_message {
    const char* msg;
    unsigned    frames;
};

struct retro_variable {
    const char* key;
    const char* value;
};

struct retro_core_option_value {
    const char* value;
    const char* label;
};

struct retro_core_option_definition {
    const char*                  key;
    const char*                  desc;
    const char*                  info;
    retro_core_option_value      values[128];
    const char*                  default_value;
};

// ─── Callback typedefs ────────────────────────────────────────────────────────
typedef void  (*retro_video_refresh_t)(const void* data, unsigned width,
                                        unsigned height, size_t pitch);
typedef void  (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t(*retro_audio_sample_batch_t)(const int16_t* data, size_t frames);
typedef void  (*retro_input_poll_t)(void);
typedef int16_t(*retro_input_state_t)(unsigned port, unsigned device,
                                       unsigned index, unsigned id);
typedef bool  (*retro_environment_t)(unsigned cmd, void* data);
typedef void  (*retro_log_printf_t)(enum retro_log_level level,
                                    const char* fmt, ...);

struct retro_log_callback {
    retro_log_printf_t log;
};

// ─── Hardware rendering (for Beetle HW renderer) ──────────────────────────────
#define RETRO_HW_FRAME_BUFFER_VALID ((uintptr_t)-1)

enum retro_hw_context_type {
    RETRO_HW_CONTEXT_NONE        = 0,
    RETRO_HW_CONTEXT_OPENGL      = 1,
    RETRO_HW_CONTEXT_OPENGLES2   = 2,
    RETRO_HW_CONTEXT_OPENGL_CORE = 3,
    RETRO_HW_CONTEXT_OPENGLES3   = 4,
    RETRO_HW_CONTEXT_VULKAN      = 7,
    RETRO_HW_CONTEXT_DUMMY       = INT32_MAX
};

typedef uintptr_t (*retro_hw_get_current_framebuffer_t)(void);
typedef void (*retro_hw_context_reset_t)(void);
typedef void* (*retro_hw_get_proc_address_t)(const char* sym);

struct retro_hw_render_callback {
    enum retro_hw_context_type context_type;
    retro_hw_context_reset_t   context_reset;
    retro_hw_get_current_framebuffer_t get_current_framebuffer;
    retro_hw_get_proc_address_t get_proc_address;
    bool depth;
    bool stencil;
    bool bottom_left_origin;
    unsigned version_major;
    unsigned version_minor;
    bool cache_context;
    retro_hw_context_reset_t context_destroy;
    bool debug_context;
};
