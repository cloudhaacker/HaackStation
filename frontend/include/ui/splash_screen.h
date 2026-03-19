#pragma once
// splash_screen.h
// The HaackStation splash screen.
// Shown on startup while the game scanner runs in the background.
//
// Layout:
//   - Full dark background
//   - HaackStation logo (pixel art PS1) centered, fades in
//   - "HaackStation" title in Zrnic font below logo
//   - Subtle version string
//   - Animated loading bar at bottom
//   - "Built on Beetle PSX HW" credit line

#include "theme_engine.h"
#include <SDL2/SDL.h>
#include <string>

class SplashScreen {
public:
    SplashScreen(SDL_Renderer* renderer, ThemeEngine* theme);
    ~SplashScreen();

    void update(float deltaMs);
    void render();

    // Returns true once the splash has fully played and is ready to hand off
    bool isDone() const { return m_done; }

    // Call this when scanning finishes so the splash knows it can proceed
    void onScanComplete() { m_scanComplete = true; }

    // Minimum display time in ms (so it doesn't flash past instantly)
    static constexpr float MIN_DISPLAY_MS = 2000.f;

private:
    void loadLogo();

    SDL_Renderer* m_renderer = nullptr;
    ThemeEngine*  m_theme    = nullptr;

    // Logo texture loaded from assets/icons/HaackStation_Logo.png
    SDL_Texture*  m_logoTex    = nullptr;
    int           m_logoW      = 0;
    int           m_logoH      = 0;

    // Animation state
    float m_elapsed      = 0.f;   // Total ms since splash started
    float m_fadeIn       = 0.f;   // 0..1 logo fade in
    float m_barProgress  = 0.f;   // 0..1 loading bar
    float m_fadeOut      = 0.f;   // 0..1 fade to black when done

    bool  m_scanComplete = false;
    bool  m_fadingOut    = false;
    bool  m_done         = false;
};
