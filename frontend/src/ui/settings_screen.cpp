#include "ui/settings_screen.h"
#include <algorithm>
#include <iostream>

SettingsScreen::SettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                                ControllerNav* nav, HaackSettings* settings)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_settings(settings)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
    buildTabs();
}

void SettingsScreen::buildTabs() {
    m_tabs.clear();

    // ── General ───────────────────────────────────────────────────────────────
    SettingTab general;
    general.label = "General";
    general.items.push_back({ "roms_path", "ROMs Folder", "Folder containing your PS1 game files",
                               SettingType::PATH, nullptr, {}, nullptr, nullptr, &m_settings->romsPath });
    general.items.push_back({ "bios_path", "BIOS File", "PlayStation BIOS file (required to play games)",
                               SettingType::PATH, nullptr, {}, nullptr, nullptr, &m_settings->biosPath });
    general.items.push_back({ "sep1", "", "", SettingType::SEPARATOR });
    general.items.push_back({ "rescan", "Rescan Library", "Re-scan ROMs folder for new games",
                               SettingType::ACTION, nullptr, {}, nullptr, nullptr, nullptr,
                               [](){ std::cout << "[Settings] Rescan triggered\n"; } });
    general.items.push_back({ "fullscreen", "Fullscreen", "Launch in fullscreen mode",
                               SettingType::TOGGLE, &m_settings->fullscreen });
    general.items.push_back({ "show_fps", "Show FPS", "Display frames per second counter",
                               SettingType::TOGGLE, &m_settings->showFps });
    m_tabs.push_back(general);

    // ── Video ─────────────────────────────────────────────────────────────────
    SettingTab video;
    video.label = "Video";
    video.items.push_back({ "renderer", "Renderer", "GPU rendering backend",
                             SettingType::CHOICE, nullptr,
                             {"OpenGL (compatible)", "Vulkan (recommended)"},
                             &m_settings->rendererChoice });
    video.items.push_back({ "internal_res", "Internal Resolution", "PS1 renders at this resolution",
                             SettingType::CHOICE, nullptr,
                             {"1x Native (320x240)", "2x (640x480)", "4x (1280x960)",
                              "8x (2560x1920)", "16x (5120x3840)"},
                             &m_settings->internalRes });
    video.items.push_back({ "vsync", "V-Sync", "Sync to monitor refresh rate",
                             SettingType::TOGGLE, &m_settings->vsync });
    video.items.push_back({ "sep1", "", "", SettingType::SEPARATOR });
    video.items.push_back({ "shader", "Shader Pack", "Post-processing shader",
                             SettingType::CHOICE, nullptr,
                             {"None (sharp)", "CRT Lottes", "CRT Royale",
                              "Scanlines", "Sharp Bilinear", "xBRZ Freescale"},
                             &m_settings->shaderChoice });
    m_tabs.push_back(video);

    // ── Audio ─────────────────────────────────────────────────────────────────
    SettingTab audio;
    audio.label = "Audio";
    audio.items.push_back({ "volume", "Master Volume", "Overall audio volume",
                             SettingType::SLIDER, nullptr, {}, nullptr,
                             &m_settings->audioVolume, nullptr, nullptr, nullptr,
                             "volume", 0, 100 });
    audio.items.push_back({ "sep1", "", "", SettingType::SEPARATOR });
    audio.items.push_back({ "audio_replace", "Audio Replacement", "Replace SPU audio with high-quality tracks",
                             SettingType::TOGGLE, &m_settings->audioReplacement });
    audio.items.push_back({ "audio_log", "Log Replacements", "Log audio hash matches to console (debug)",
                             SettingType::TOGGLE, &m_settings->audioReplacementLog });
    m_tabs.push_back(audio);

    // ── Textures ──────────────────────────────────────────────────────────────
    SettingTab textures;
    textures.label = "Textures";
    textures.items.push_back({ "tex_replace", "Texture Replacement", "Use HD texture packs when available",
                                SettingType::TOGGLE, &m_settings->textureReplacement });
    textures.items.push_back({ "sep1", "", "", SettingType::SEPARATOR });
    textures.items.push_back({ "ai_upscale", "AI Upscaling (NCNN)", "Upscale PS1 textures using AI (experimental)",
                                SettingType::TOGGLE, &m_settings->aiUpscaling });
    textures.items.push_back({ "ai_scale", "AI Upscale Factor", "How much to upscale textures",
                                SettingType::CHOICE, nullptr,
                                {"2x", "4x"},
                                &m_settings->aiUpscaleScale });
    m_tabs.push_back(textures);

    // ── About ─────────────────────────────────────────────────────────────────
    SettingTab about;
    about.label = "About";
    about.items.push_back({ "ver",     "Version",         SettingType::LABEL, nullptr, {}, nullptr, nullptr, nullptr, nullptr, nullptr, "0.1.0-dev" });
    about.items.push_back({ "core",    "Emulation Core",  SettingType::LABEL, nullptr, {}, nullptr, nullptr, nullptr, nullptr, nullptr, "Beetle PSX HW (libretro)" });
    about.items.push_back({ "license", "License",         SettingType::LABEL, nullptr, {}, nullptr, nullptr, nullptr, nullptr, nullptr, "GNU General Public License v2" });
    about.items.push_back({ "sep1", "", "", SettingType::SEPARATOR });
    about.items.push_back({ "credits", "Credits",         SettingType::LABEL, nullptr, {}, nullptr, nullptr, nullptr, nullptr, nullptr,
                             "Core: libretro team  |  Frontend: HaackStation contributors  |  AI tooling: Claude (Anthropic)" });
    about.items.push_back({ "sep2", "", "", SettingType::SEPARATOR });
    about.items.push_back({ "github", "Source Code",      SettingType::LABEL, nullptr, {}, nullptr, nullptr, nullptr, nullptr, nullptr,
                             "github.com/haackstation/haackstation" });
    m_tabs.push_back(about);
}

void SettingsScreen::handleEvent(const SDL_Event& e) {
    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) navigateAction(action);
    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE) navigateAction(action);
}

void SettingsScreen::navigateAction(NavAction action) {
    auto& tab = m_tabs[m_activeTab];

    switch (action) {
        case NavAction::LEFT:
            m_activeTab = std::max(0, m_activeTab - 1);
            m_activeItem = 0;
            m_scrollOffset = 0;
            break;
        case NavAction::RIGHT:
            m_activeTab = std::min((int)m_tabs.size() - 1, m_activeTab + 1);
            m_activeItem = 0;
            m_scrollOffset = 0;
            break;
        case NavAction::UP:
            do {
                m_activeItem = std::max(0, m_activeItem - 1);
            } while (m_activeItem > 0 &&
                     tab.items[m_activeItem].type == SettingType::SEPARATOR);
            break;
        case NavAction::DOWN:
            do {
                m_activeItem = std::min((int)tab.items.size() - 1, m_activeItem + 1);
            } while (m_activeItem < (int)tab.items.size() - 1 &&
                     tab.items[m_activeItem].type == SettingType::SEPARATOR);
            break;
        case NavAction::CONFIRM:
            activateCurrentItem();
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_wantsClose = true;
            break;
        default:
            break;
    }
}

void SettingsScreen::activateCurrentItem() {
    auto& tab  = m_tabs[m_activeTab];
    auto& item = tab.items[m_activeItem];

    switch (item.type) {
        case SettingType::TOGGLE:
            if (item.toggleValue) {
                *item.toggleValue = !*item.toggleValue;
                m_nav->rumbleConfirm();
            }
            break;
        case SettingType::CHOICE:
            if (item.choiceIndex && !item.choices.empty()) {
                *item.choiceIndex = (*item.choiceIndex + 1) % (int)item.choices.size();
                m_nav->rumbleConfirm();
            }
            break;
        case SettingType::ACTION:
            if (item.action) {
                item.action();
                m_nav->rumbleConfirm();
            }
            break;
        default:
            break;
    }
}

void SettingsScreen::update(float /*deltaMs*/) {}

void SettingsScreen::render() {
    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    // Header
    m_theme->drawHeader(m_windowW, m_windowH, "Settings", "", 0);

    // ── Tab bar ────────────────────────────────────────────────────────────────
    int tabBarY = 80;
    SDL_Rect tabBar = { 0, tabBarY, m_windowW, TAB_BAR_H };
    m_theme->drawRect(tabBar, pal.bgPanel);

    int tabX = PANEL_X;
    for (int i = 0; i < (int)m_tabs.size(); i++) {
        bool active = (i == m_activeTab);
        int tw, th;
        m_theme->measureText(m_tabs[i].label, FontSize::BODY, tw, th);
        int tabW = tw + 32;

        SDL_Rect tabRect = { tabX, tabBarY, tabW, TAB_BAR_H };

        if (active) {
            m_theme->drawRect(tabRect, pal.bgCard);
            // Active tab accent underline
            SDL_Rect underline = { tabX, tabBarY + TAB_BAR_H - 3, tabW, 3 };
            m_theme->drawRect(underline, pal.accent);
        }

        m_theme->drawTextCentered(m_tabs[i].label,
                                   tabX + tabW / 2,
                                   tabBarY + (TAB_BAR_H - th) / 2,
                                   active ? pal.textPrimary : pal.textSecond,
                                   FontSize::BODY);
        tabX += tabW + 4;
    }

    // ── Settings items ────────────────────────────────────────────────────────
    renderTab(m_tabs[m_activeTab]);

    // Footer
    m_theme->drawFooterHints(m_windowW, m_windowH, "Select / Toggle", "");
}

void SettingsScreen::renderTab(const SettingTab& tab) {
    int y = PANEL_Y + 12;
    int panelW = m_windowW - PANEL_X * 2;

    for (int i = 0; i < (int)tab.items.size(); i++) {
        if (y > m_windowH - m_theme->layout().footerH - ITEM_H) break;
        renderItem(tab.items[i], PANEL_X, y, panelW, i == m_activeItem);
        y += (tab.items[i].type == SettingType::SEPARATOR) ? 16 : ITEM_H;
    }
}

void SettingsScreen::renderItem(const SettingItem& item, int x, int y, int w, bool selected) {
    const auto& pal = m_theme->palette();

    if (item.type == SettingType::SEPARATOR) {
        m_theme->drawLine(x, y + 8, x + w, y + 8, pal.gridLine);
        return;
    }

    // Row background
    SDL_Rect row = { x, y, w, ITEM_H - 4 };
    if (selected) m_theme->drawRoundRect(row, pal.bgCardHover, 6);

    // Label
    m_theme->drawText(item.label, x + 16, y + 10,
                      selected ? pal.textPrimary : pal.textSecond,
                      FontSize::BODY);

    // Description (smaller, below label)
    if (!item.description.empty()) {
        m_theme->drawText(item.description, x + 16, y + 30,
                          pal.textDisable, FontSize::TINY);
    }

    // Value (right-aligned)
    std::string valueStr;
    switch (item.type) {
        case SettingType::TOGGLE:
            if (item.toggleValue) {
                valueStr = *item.toggleValue ? "ON" : "OFF";
                SDL_Color vc = *item.toggleValue ? pal.multiDisc : pal.textDisable;
                int vw, vh;
                m_theme->measureText(valueStr, FontSize::BODY, vw, vh);
                m_theme->drawText(valueStr, x + w - vw - 20, y + 14, vc, FontSize::BODY);
            }
            break;
        case SettingType::CHOICE:
            if (item.choiceIndex && !item.choices.empty()) {
                valueStr = item.choices[*item.choiceIndex];
                int vw, vh;
                m_theme->measureText(valueStr, FontSize::BODY, vw, vh);
                m_theme->drawText(valueStr, x + w - vw - 20, y + 14,
                                  selected ? pal.accent : pal.textSecond, FontSize::BODY);
            }
            break;
        case SettingType::SLIDER:
            if (item.sliderValue) {
                valueStr = std::to_string(*item.sliderValue) + "%";
                int vw, vh;
                m_theme->measureText(valueStr, FontSize::BODY, vw, vh);
                m_theme->drawText(valueStr, x + w - vw - 20, y + 14, pal.accent, FontSize::BODY);
            }
            break;
        case SettingType::LABEL:
            {
                int vw, vh;
                m_theme->measureText(item.labelValue, FontSize::SMALL, vw, vh);
                m_theme->drawText(item.labelValue, x + w - vw - 20, y + 16,
                                  pal.textSecond, FontSize::SMALL);
            }
            break;
        case SettingType::ACTION:
            {
                std::string arrow = ">";
                int vw, vh;
                m_theme->measureText(arrow, FontSize::TITLE, vw, vh);
                m_theme->drawText(arrow, x + w - vw - 20, y + 10,
                                  selected ? pal.accent : pal.textDisable, FontSize::TITLE);
            }
            break;
        default:
            break;
    }
}
