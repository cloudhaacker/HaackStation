#include "splash_screen.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <cmath>
#include <algorithm>

SplashScreen::SplashScreen(SDL_Renderer* renderer, ThemeEngine* theme)
    : m_renderer(renderer), m_theme(theme)
{
    loadLogo();
}

SplashScreen::~SplashScreen() {
    if (m_logoTex) SDL_DestroyTexture(m_logoTex);
}

void SplashScreen::loadLogo() {
    // Try PNG first (requires SDL2_image), fall back to BMP
    const char* logoPaths[] = {
        "assets/icons/HaackStation_Logo.png",
        "assets/icons/haackstation_logo.png",
        "assets/icons/logo.png",
        nullptr
    };

    for (int i = 0; logoPaths[i]; ++i) {
        SDL_Surface* surf = nullptr;

        // Try SDL_image first
        surf = IMG_Load(logoPaths[i]);
        if (!surf) {
            // Fall back to BMP (always available in SDL2)
            surf = SDL_LoadBMP(logoPaths[i]);
        }

        if (surf) {
            m_logoTex = SDL_CreateTextureFromSurface(m_renderer, surf);
            m_logoW   = surf->w;
            m_logoH   = surf->h;
            SDL_FreeSurface(surf);
            std::cout << "[SplashScreen] Logo loaded: " << logoPaths[i]
                      << " (" << m_logoW << "x" << m_logoH << ")\n";
            return;
        }
    }
    std::cout << "[SplashScreen] No logo found — text-only splash\n";
}

void SplashScreen::update(float deltaMs) {
    if (m_done) return;
    m_elapsed += deltaMs;

    // Phase 1: Fade in logo (0 - 800ms)
    m_fadeIn = std::min(1.f, m_elapsed / 800.f);
    m_fadeIn = Ease::outCubic(m_fadeIn);

    // Phase 2: Loading bar grows while scanning
    // It fills up to 90% on its own, then jumps to 100% when scan completes
    float naturalProgress = std::min(0.9f, m_elapsed / 1800.f);
    if (m_scanComplete) {
        m_barProgress = std::min(1.f, m_barProgress + deltaMs / 200.f);
    } else {
        m_barProgress = naturalProgress;
    }

    // Phase 3: Fade out once scan is done and minimum time has passed
    bool canExit = m_scanComplete && m_elapsed >= MIN_DISPLAY_MS && m_barProgress >= 1.f;
    if (canExit && !m_fadingOut) {
        m_fadingOut = true;
    }

    if (m_fadingOut) {
        m_fadeOut += deltaMs / 400.f;
        if (m_fadeOut >= 1.f) {
            m_fadeOut = 1.f;
            m_done    = true;
        }
    }
}

void SplashScreen::render() {
    int winW, winH;
    SDL_GetRendererOutputSize(m_renderer, &winW, &winH);

    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    int cx = winW / 2;
    int cy = winH / 2;

    Uint8 contentAlpha = (Uint8)(m_fadeIn * 255.f);

    // ── Logo ─────────────────────────────────────────────────────────────────
    if (m_logoTex) {
        // Scale logo to fit nicely — max 360px wide, maintain aspect ratio
        int maxLogoW = std::min(360, winW - 80);
        float scale  = std::min(1.f, (float)maxLogoW / m_logoW);
        int drawW    = (int)(m_logoW * scale);
        int drawH    = (int)(m_logoH * scale);

        SDL_Rect logoRect = {
            cx - drawW / 2,
            cy - drawH / 2 - 60,   // Slightly above center
            drawW, drawH
        };

        SDL_SetTextureAlphaMod(m_logoTex, contentAlpha);
        SDL_RenderCopy(m_renderer, m_logoTex, nullptr, &logoRect);
    }

    // ── Title — "HaackStation" in Zrnic display font ─────────────────────────
    {
        SDL_Color titleColor = pal.textPrimary;
        titleColor.a = contentAlpha;

        // Draw with display font (Zrnic) if available, system font otherwise
        int titleY = cy + (m_logoTex ? 60 : -20);
        m_theme->drawTextCentered("HaackStation", cx, titleY,
                                   titleColor, FontSize::HERO,
                                   true); // <-- true = use Zrnic display font
    }

    // ── Version & core credit ────────────────────────────────────────────────
    {
        SDL_Color dimColor = pal.textDisable;
        dimColor.a = contentAlpha;

        int creditY = winH - 80;
        m_theme->drawTextCentered("Built on Beetle PSX HW  |  libretro team",
                                   cx, creditY, dimColor, FontSize::SMALL);
        m_theme->drawTextCentered("github.com/cloudhaacker/HaackStation",
                                   cx, creditY + 22, dimColor, FontSize::TINY);
    }

    // ── Loading bar ──────────────────────────────────────────────────────────
    {
        int barW    = std::min(400, winW - 80);
        int barH    = 3;
        int barX    = cx - barW / 2;
        int barY    = winH - 40;

        // Track
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer,
                               pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, contentAlpha);
        SDL_Rect track = { barX, barY, barW, barH };
        SDL_RenderFillRect(m_renderer, &track);

        // Fill
        int fillW = (int)(barW * m_barProgress);
        if (fillW > 0) {
            SDL_SetRenderDrawColor(m_renderer,
                                   pal.accent.r, pal.accent.g, pal.accent.b, contentAlpha);
            SDL_Rect fill = { barX, barY, fillW, barH };
            SDL_RenderFillRect(m_renderer, &fill);
        }
    }

    // ── Fade-out overlay ─────────────────────────────────────────────────────
    if (m_fadingOut && m_fadeOut > 0.f) {
        Uint8 blackAlpha = (Uint8)(m_fadeOut * 255.f);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, blackAlpha);
        SDL_Rect full = { 0, 0, winW, winH };
        SDL_RenderFillRect(m_renderer, &full);
    }
}
