#include "settings_screen.h"
#include "theme_engine.h"
#include <algorithm>
#include <iostream>

// ─── Helper builders ──────────────────────────────────────────────────────────
static SettingItem makeToggle(const std::string& id, const std::string& label,
                               const std::string& desc, bool* val) {
    SettingItem s;
    s.id = id; s.label = label; s.description = desc;
    s.type = SettingType::TOGGLE; s.toggleValue = val;
    return s;
}
static SettingItem makeChoice(const std::string& id, const std::string& label,
                               const std::string& desc,
                               std::vector<std::string> choices, int* idx) {
    SettingItem s;
    s.id = id; s.label = label; s.description = desc;
    s.type = SettingType::CHOICE; s.choices = choices; s.choiceIndex = idx;
    return s;
}
static SettingItem makeSlider(const std::string& id, const std::string& label,
                               const std::string& desc, int* val, int mn, int mx) {
    SettingItem s;
    s.id = id; s.label = label; s.description = desc;
    s.type = SettingType::SLIDER; s.sliderValue = val;
    s.sliderMin = mn; s.sliderMax = mx;
    return s;
}
static SettingItem makeAction(const std::string& id, const std::string& label,
                               const std::string& desc, std::function<void()> fn) {
    SettingItem s;
    s.id = id; s.label = label; s.description = desc;
    s.type = SettingType::ACTION; s.action = fn;
    return s;
}
static SettingItem makeLabel(const std::string& id, const std::string& label,
                              const std::string& value) {
    SettingItem s;
    s.id = id; s.label = label;
    s.type = SettingType::LABEL; s.labelValue = value;
    return s;
}
static SettingItem makeSep() {
    SettingItem s; s.type = SettingType::SEPARATOR; return s;
}

// ─── Constructor ──────────────────────────────────────────────────────────────
SettingsScreen::SettingsScreen(SDL_Renderer* renderer, ThemeEngine* theme,
                                ControllerNav* nav, HaackSettings* settings)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_settings(settings)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
    buildTabs();
}

void SettingsScreen::onWindowResize(int w, int h) {
    m_windowW = w;
    m_windowH = h;
}

void SettingsScreen::buildTabs() {
    m_tabs.clear();

    // ── General ───────────────────────────────────────────────────────────────
    {
        SettingTab tab;
        tab.label = "General";
        tab.items.push_back(makeToggle("fullscreen", "Fullscreen",
            "Launch in fullscreen mode", &m_settings->fullscreen));
        tab.items.push_back(makeToggle("show_fps", "Show FPS",
            "Display frames per second counter", &m_settings->showFps));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeChoice("top_row", "Default Shelf",
            "Which shelf opens when you launch HaackStation",
            { "All Games", "Recently Played", "Favorites" },
            &m_settings->topRowMode));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeAction("rescan", "Rescan Library",
            "Re-scan ROMs folder for new games",
            []() { std::cout << "[Settings] Rescan triggered\n"; }));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeLabel("ss_header", "ScreenScraper Account",
            "Register free at screenscraper.fr"));
        tab.items.push_back(makeAction("scrape", "Scrape Game Art",
            "Download cover art, screenshots and info",
            [this]() { m_wantsScrape = true; }));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeAction("quit", "Quit HaackStation",
            "Exit the application",
            [this]() { m_wantsQuit = true; }));
        m_tabs.push_back(tab);
    }

    // ── Emulation ─────────────────────────────────────────────────────────────
    // Fast Boot and Fast Forward live here. These are the settings most likely
    // to be overridden per-game, which is why they're grouped separately from
    // general display/audio options.
    {
        SettingTab tab;
        tab.label = "Emulation";
        tab.items.push_back(makeToggle("fast_boot", "Fast Boot",
            "Skip PS1 BIOS logo on startup (disable for some PAL/Saga Frontier)",
            &m_settings->fastBoot));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeLabel("ff_header", "Fast Forward",
            "Hold R2 (controller) or F key (keyboard) to speed up"));
        tab.items.push_back(makeChoice("ff_speed", "Fast Forward Speed",
            "How many times faster than normal when held",
            { "2x", "4x", "6x", "8x" },
            &m_settings->fastForwardSpeed));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeLabel("ff_note", "Fast Forward toggle",
            "Per-game toggle option coming in per-game settings"));
        m_tabs.push_back(tab);
    }

    // ── Video ─────────────────────────────────────────────────────────────────
    {
        SettingTab tab;
        tab.label = "Video";
        tab.items.push_back(makeChoice("renderer", "Renderer",
            "GPU rendering backend",
            { "OpenGL (compatible)", "Vulkan (recommended)" },
            &m_settings->rendererChoice));
        tab.items.push_back(makeChoice("internal_res", "Internal Resolution",
            "PS1 renders at this resolution",
            { "1x Native (320x240)", "2x (640x480)", "4x (1280x960)",
              "8x (2560x1920)", "16x (5120x3840)" },
            &m_settings->internalRes));
        tab.items.push_back(makeToggle("vsync", "V-Sync",
            "Sync to monitor refresh rate", &m_settings->vsync));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeChoice("shader", "Shader Pack",
            "Post-processing shader",
            { "None (sharp)", "CRT Lottes", "CRT Royale",
              "Scanlines", "Sharp Bilinear", "xBRZ Freescale" },
            &m_settings->shaderChoice));
        m_tabs.push_back(tab);
    }

    // ── Audio ─────────────────────────────────────────────────────────────────
    {
        SettingTab tab;
        tab.label = "Audio";
        tab.items.push_back(makeSlider("volume", "Master Volume",
            "Overall audio volume", &m_settings->audioVolume, 0, 100));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeToggle("audio_replace", "Audio Replacement",
            "Replace SPU audio with high-quality tracks",
            &m_settings->audioReplacement));
        m_tabs.push_back(tab);
    }

    // ── Textures ──────────────────────────────────────────────────────────────
    {
        SettingTab tab;
        tab.label = "Textures";
        tab.items.push_back(makeToggle("tex_replace", "Texture Replacement",
            "Use HD texture packs when available",
            &m_settings->textureReplacement));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeToggle("ai_upscale", "AI Upscaling (NCNN)",
            "Upscale PS1 textures using AI (experimental)",
            &m_settings->aiUpscaling));
        tab.items.push_back(makeChoice("ai_scale", "AI Upscale Factor",
            "How much to upscale textures", { "2x", "4x" },
            &m_settings->aiUpscaleScale));
        m_tabs.push_back(tab);
    }

    // ── About ─────────────────────────────────────────────────────────────────
    {
        SettingTab tab;
        tab.label = "About";
        tab.items.push_back(makeLabel("ver",     "Version",        "0.1.0-dev"));
        tab.items.push_back(makeLabel("core",    "Emulation Core", "Beetle PSX HW / Mednafen (libretro)"));
        tab.items.push_back(makeLabel("license", "License",        "GNU General Public License v2"));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeLabel("core_credit", "Core Credit",
            "libretro team and Mednafen contributors"));
        tab.items.push_back(makeLabel("code_credit", "Frontend Code",
            "Claude (Anthropic AI) - directed by project author"));
        tab.items.push_back(makeLabel("logo_credit", "Project Logo",
            "Generated with Google Gemini"));
        tab.items.push_back(makeLabel("font_credit", "UI Font",
            "Zrnic by Apostrophic Labs - dafont.com/zrnic.font"));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeLabel("github", "Source Code",
            "github.com/cloudhaacker/HaackStation"));
        tab.items.push_back(makeSep());
        tab.items.push_back(makeAction("quit", "Quit HaackStation",
            "Exit the application",
            [this]() { m_wantsQuit = true; }));
        m_tabs.push_back(tab);
    }
}

void SettingsScreen::handleEvent(const SDL_Event& e) {
    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) navigateAction(action, false);
}

void SettingsScreen::navigateAction(NavAction action, bool isRepeat) {
    auto& tab     = m_tabs[m_activeTab];
    int   numTabs = (int)m_tabs.size();
    int   numItems= (int)tab.items.size();

    // Helper: skip separators going in a direction, return new index or -1 if stuck
    auto skipSeps = [&](int start, int dir) -> int {
        int next = start;
        for (int i = 0; i < numItems; i++) {
            next = next + dir;
            if (next < 0 || next >= numItems) return -1; // hit an edge
            if (tab.items[next].type != SettingType::SEPARATOR) return next;
        }
        return -1;
    };

    switch (action) {
        case NavAction::LEFT:
            if (m_editingChoice) {
                // Adjust selected choice / slider backwards
                auto& item = tab.items[m_activeItem];
                if (item.type == SettingType::CHOICE &&
                    item.choiceIndex && !item.choices.empty()) {
                    int sz = (int)item.choices.size();
                    int next = *item.choiceIndex - 1;
                    if (next >= 0) { *item.choiceIndex = next; m_nav->rumbleConfirm(); }
                } else if (item.type == SettingType::SLIDER && item.sliderValue) {
                    if (*item.sliderValue > item.sliderMin) {
                        (*item.sliderValue)--;
                        m_nav->rumbleConfirm();
                    }
                }
            } else {
                // Switch tab left (wrap only on fresh press)
                if (!isRepeat || m_activeTab > 0) {
                    m_activeTab  = (m_activeTab - 1 + numTabs) % numTabs;
                    m_activeItem = 0; m_scrollOffset = 0;
                }
            }
            break;

        case NavAction::RIGHT:
            if (m_editingChoice) {
                // Adjust selected choice / slider forwards
                auto& item = tab.items[m_activeItem];
                if (item.type == SettingType::CHOICE &&
                    item.choiceIndex && !item.choices.empty()) {
                    int sz = (int)item.choices.size();
                    int next = *item.choiceIndex + 1;
                    if (next < sz) { *item.choiceIndex = next; m_nav->rumbleConfirm(); }
                } else if (item.type == SettingType::SLIDER && item.sliderValue) {
                    if (*item.sliderValue < item.sliderMax) {
                        (*item.sliderValue)++;
                        m_nav->rumbleConfirm();
                    }
                }
            } else {
                // Switch tab right (wrap only on fresh press)
                if (!isRepeat || m_activeTab < numTabs - 1) {
                    m_activeTab  = (m_activeTab + 1) % numTabs;
                    m_activeItem = 0; m_scrollOffset = 0;
                }
            }
            break;

        case NavAction::UP: {
            if (m_editingChoice) { m_editingChoice = false; break; } // exit edit mode
            int next = skipSeps(m_activeItem, -1);
            if (next >= 0) {
                m_activeItem = next;
            } else if (!isRepeat) {
                // Fresh press at top: wrap to bottom
                for (int i = numItems - 1; i >= 0; i--) {
                    if (tab.items[i].type != SettingType::SEPARATOR) {
                        m_activeItem = i; break;
                    }
                }
            }
            // isRepeat at top edge: do nothing (clamp)
            break;
        }

        case NavAction::DOWN: {
            if (m_editingChoice) { m_editingChoice = false; break; } // exit edit mode
            int next = skipSeps(m_activeItem, +1);
            if (next >= 0) {
                m_activeItem = next;
            } else if (!isRepeat) {
                // Fresh press at bottom: wrap to top
                for (int i = 0; i < numItems; i++) {
                    if (tab.items[i].type != SettingType::SEPARATOR) {
                        m_activeItem = i; break;
                    }
                }
            }
            // isRepeat at bottom edge: do nothing (clamp)
            break;
        }

        case NavAction::CONFIRM: {
            auto& item = tab.items[m_activeItem];
            if (item.type == SettingType::CHOICE || item.type == SettingType::SLIDER) {
                // Toggle into/out of edit mode so LEFT/RIGHT adjusts the value
                m_editingChoice = !m_editingChoice;
                m_nav->rumbleConfirm();
            } else {
                m_editingChoice = false;
                activateCurrentItem();
            }
            break;
        }

        case NavAction::BACK:
            if (m_editingChoice) {
                m_editingChoice = false; // exit edit mode first
            } else {
                m_wantsClose = true;
            }
            break;

        case NavAction::MENU:
            m_wantsClose = true;
            break;

        default: break;
    }
}

void SettingsScreen::activateCurrentItem() {
    auto& item = m_tabs[m_activeTab].items[m_activeItem];
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
            if (item.action) { item.action(); m_nav->rumbleConfirm(); }
            break;
        case SettingType::PATH:
            m_nav->rumbleConfirm();
            break;
        default: break;
    }
}

void SettingsScreen::update(float /*deltaMs*/) {
    // updateHeld fires every frame so d-pad hold works even between SDL events
    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held != NavAction::NONE) navigateAction(held, true);
}

void SettingsScreen::render() {
    SDL_GetRendererOutputSize(m_renderer, &m_windowW, &m_windowH);

    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    m_theme->drawHeader(m_windowW, m_windowH, "Settings", "", 0);

    // Tab bar
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
            SDL_Rect ul = { tabX, tabBarY + TAB_BAR_H - 3, tabW, 3 };
            m_theme->drawRect(ul, pal.accent);
        }
        m_theme->drawTextCentered(m_tabs[i].label,
            tabX + tabW/2, tabBarY + (TAB_BAR_H - th)/2,
            active ? pal.textPrimary : pal.textSecond, FontSize::BODY);
        tabX += tabW + 4;
    }

    renderTab(m_tabs[m_activeTab]);
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

void SettingsScreen::renderItem(const SettingItem& item, int x, int y,
                                 int w, bool selected) {
    const auto& pal = m_theme->palette();
    if (item.type == SettingType::SEPARATOR) {
        m_theme->drawLine(x, y+8, x+w, y+8, pal.gridLine);
        return;
    }
    SDL_Rect row = { x, y, w, ITEM_H - 4 };
    if (selected) m_theme->drawRoundRect(row, pal.bgCardHover, 6);

    m_theme->drawText(item.label, x+16, y+10,
        selected ? pal.textPrimary : pal.textSecond, FontSize::BODY);
    if (!item.description.empty())
        m_theme->drawText(item.description, x+16, y+30, pal.textDisable, FontSize::TINY);

    switch (item.type) {
        case SettingType::TOGGLE:
            if (item.toggleValue) {
                std::string v = *item.toggleValue ? "ON" : "OFF";
                SDL_Color vc = *item.toggleValue ? pal.multiDisc : pal.textDisable;
                int vw, vh; m_theme->measureText(v, FontSize::BODY, vw, vh);
                m_theme->drawText(v, x+w-vw-20, y+14, vc, FontSize::BODY);
            }
            break;
        case SettingType::CHOICE:
            if (item.choiceIndex && !item.choices.empty()) {
                const std::string& v = item.choices[*item.choiceIndex];
                int vw, vh; m_theme->measureText(v, FontSize::BODY, vw, vh);
                m_theme->drawText(v, x+w-vw-20, y+14,
                    selected ? pal.accent : pal.textSecond, FontSize::BODY);
            }
            break;
        case SettingType::SLIDER:
            if (item.sliderValue) {
                std::string v = std::to_string(*item.sliderValue) + "%";
                int vw, vh; m_theme->measureText(v, FontSize::BODY, vw, vh);
                m_theme->drawText(v, x+w-vw-20, y+14, pal.accent, FontSize::BODY);
            }
            break;
        case SettingType::LABEL: {
            int vw, vh;
            m_theme->measureText(item.labelValue, FontSize::SMALL, vw, vh);
            m_theme->drawText(item.labelValue, x+w-vw-20, y+16,
                pal.textSecond, FontSize::SMALL);
            break;
        }
        case SettingType::ACTION: {
            int vw, vh;
            m_theme->measureText(">", FontSize::TITLE, vw, vh);
            m_theme->drawText(">", x+w-vw-20, y+10,
                selected ? pal.accent : pal.textDisable, FontSize::TITLE);
            break;
        }
        default: break;
    }
}
