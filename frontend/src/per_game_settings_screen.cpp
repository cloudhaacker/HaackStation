#include "per_game_settings_screen.h"
#include "per_game_settings.h"
#include <iostream>
#include <algorithm>

PerGameSettingsScreen::PerGameSettingsScreen(SDL_Renderer* renderer,
                                              ThemeEngine* theme,
                                              ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

void PerGameSettingsScreen::open(const std::string& gameTitle,
                                  const std::string& gamePath,
                                  const std::string& serial,
                                  GameOverrides currentOverrides) {
    m_gameTitle   = gameTitle;
    m_gamePath    = gamePath;
    m_serial      = serial.empty() ? [&]() {
        // Derive serial-like key from filename stem
        auto sl = gamePath.find_last_of("/\\");
        std::string stem = (sl != std::string::npos)
            ? gamePath.substr(sl + 1) : gamePath;
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        return stem;
    }() : serial;

    m_overrides   = currentOverrides;
    m_selectedRow = 0;
    m_wantsClose  = false;
    m_open        = true;
    buildRows();
    std::cout << "[PerGameScreen] Opened for: " << gameTitle << "\n";
}

void PerGameSettingsScreen::close() {
    m_open = false;
}

void PerGameSettingsScreen::buildRows() {
    m_rows.clear();

    // Each row: label, description, pointer to override-enable flag,
    //           pointer to int value (or nullptr), choices, pointer to bool value (or nullptr)

    m_rows.push_back({
        "Override Resolution",
        "Use a different internal resolution for this game",
        &m_overrides.overrideResolution,
        &m_overrides.internalRes,
        { "1x Native (320x240)", "2x (640x480)", "4x (1280x960)", "8x", "16x" },
        nullptr
    });

    m_rows.push_back({
        "Override Renderer",
        "Force software or hardware renderer for this game",
        &m_overrides.overrideRenderer,
        &m_overrides.rendererChoice,
        { "Software", "Hardware (OpenGL)" },
        nullptr
    });

    m_rows.push_back({
        "Override Shader",
        "Use a specific shader for this game",
        &m_overrides.overrideShader,
        &m_overrides.shaderChoice,
        { "None (sharp)", "CRT Lottes", "CRT Royale",
          "Scanlines", "Sharp Bilinear", "xBRZ Freescale" },
        nullptr
    });

    m_rows.push_back({
        "Override Texture Replacement",
        "Enable or disable HD textures for this game",
        &m_overrides.overrideTextures,
        nullptr,
        { "Off", "On" },
        &m_overrides.textureReplacement
    });

    m_rows.push_back({
        "Override Audio Replacement",
        "Enable or disable audio tracks for this game",
        &m_overrides.overrideAudioReplace,
        nullptr,
        { "Off", "On" },
        &m_overrides.audioReplacement
    });

    m_rows.push_back({
        "Reset All Overrides",
        "Remove all per-game settings and use global defaults",
        nullptr, nullptr, {}, nullptr // special action row
    });
}

void PerGameSettingsScreen::handleEvent(const SDL_Event& e) {
    if (!m_open) return;
    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) navigateAction(action);
}

void PerGameSettingsScreen::update(float /*deltaMs*/) {
    if (!m_open) return;
    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held != NavAction::NONE) navigateAction(held);
}

void PerGameSettingsScreen::navigateAction(NavAction action) {
    int numRows = (int)m_rows.size();

    switch (action) {
        case NavAction::UP:
            if (m_selectedRow > 0) m_selectedRow--;
            break;

        case NavAction::DOWN:
            if (m_selectedRow < numRows - 1) m_selectedRow++;
            break;

        case NavAction::LEFT:
        case NavAction::RIGHT: {
            if (m_selectedRow >= numRows) break;
            Row& row = m_rows[m_selectedRow];
            if (!row.enabled) break; // action row — no left/right

            // LEFT/RIGHT on a choice/bool setting changes the value
            if (row.value != nullptr && !row.choices.empty()) {
                if (!*row.enabled) { *row.enabled = true; } // auto-enable override
                int sz = (int)row.choices.size();
                if (action == NavAction::RIGHT)
                    *row.value = (*row.value + 1) % sz;
                else
                    *row.value = (*row.value - 1 + sz) % sz;
                m_nav->rumbleConfirm();
            } else if (row.boolValue != nullptr) {
                if (!*row.enabled) { *row.enabled = true; }
                *row.boolValue = !*row.boolValue;
                m_nav->rumbleConfirm();
            }
            break;
        }

        case NavAction::CONFIRM: {
            if (m_selectedRow >= numRows) break;
            Row& row = m_rows[m_selectedRow];

            if (!row.enabled) {
                // "Reset All Overrides" action row
                m_overrides = GameOverrides{};
                buildRows(); // rebuild since pointers need refresh
                m_nav->rumbleConfirm();
                std::cout << "[PerGameScreen] All overrides cleared\n";
                break;
            }

            // Toggle the override-enabled flag
            *row.enabled = !*row.enabled;
            m_nav->rumbleConfirm();
            break;
        }

        case NavAction::BACK:
        case NavAction::MENU: {
            // Save and close
            PerGameSettings pgs;
            pgs.save(m_serial, m_overrides);
            m_wantsClose = true;
            m_nav->rumbleConfirm();
            break;
        }

        default: break;
    }
}

void PerGameSettingsScreen::render() {
    if (!m_open) return;
    const auto& pal = m_theme->palette();

    // Full-screen dim
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
    SDL_Rect full = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &full);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Panel
    int panelW = std::min(PANEL_W, m_w - PANEL_X_MARGIN * 2);
    int numRows = (int)m_rows.size();
    int panelH = 80 + numRows * ITEM_H + 60;
    int panelX = (m_w - panelW) / 2;
    int panelY = (m_h - panelH) / 2;
    panelY = std::max(20, panelY);

    SDL_Rect panel = { panelX, panelY, panelW, panelH };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 245);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Accent left border
    SDL_Rect border = { panelX, panelY, 4, panelH };
    m_theme->drawRect(border, pal.accent);

    // Header
    m_theme->drawText("Game Settings", panelX + 16, panelY + 12,
                       pal.accent, FontSize::TITLE);
    m_theme->drawText(m_gameTitle, panelX + 16, panelY + 46,
                       pal.textSecond, FontSize::SMALL);
    m_theme->drawLine(panelX + 16, panelY + 70,
                       panelX + panelW - 16, panelY + 70, pal.gridLine);

    // Rows
    int contentW = panelW - 32;
    int y = panelY + 78;
    for (int i = 0; i < numRows; i++) {
        renderRow(m_rows[i], panelX + 16, y, contentW, i == m_selectedRow);
        y += ITEM_H;
    }

    // Footer
    m_theme->drawFooterHints(m_w, m_h, "Toggle Override", "Save & Back", "< / > Value", "");
}

void PerGameSettingsScreen::renderRow(const Row& row, int x, int y,
                                       int w, bool selected) {
    const auto& pal = m_theme->palette();

    if (selected) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 40);
        SDL_Rect hi = { x - 4, y, w + 8, ITEM_H - 4 };
        SDL_RenderFillRect(m_renderer, &hi);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        SDL_Rect ind = { x - 4, y, 4, ITEM_H - 4 };
        m_theme->drawRect(ind, pal.accent);
    }

    // Action row (Reset All)
    if (!row.enabled) {
        SDL_Color col = selected ? pal.accent : pal.textSecond;
        m_theme->drawText(row.label, x + 8, y + 8, col, FontSize::BODY);
        m_theme->drawText(row.description, x + 8, y + 34, pal.textDisable, FontSize::TINY);
        return;
    }

    // Override enabled/disabled indicator
    bool active = *row.enabled;
    SDL_Color labelCol  = active ? pal.textPrimary : pal.textDisable;
    SDL_Color valueCol  = active ? pal.accent      : pal.textDisable;

    // Checkbox style toggle indicator
    SDL_Rect checkBox = { x + 8, y + 10, 18, 18 };
    m_theme->drawRect(checkBox, active ? pal.accent : pal.bgCard);
    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_RenderDrawRect(m_renderer, &checkBox);
    if (active) {
        // Checkmark lines
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(m_renderer, x+11, y+19, x+14, y+23);
        SDL_RenderDrawLine(m_renderer, x+14, y+23, x+22, y+13);
    }

    m_theme->drawText(row.label, x + 32, y + 8,
                       selected ? pal.textPrimary : labelCol, FontSize::BODY);
    m_theme->drawText(row.description, x + 32, y + 34, pal.textDisable, FontSize::TINY);

    // Current value (right-aligned)
    std::string valStr;
    if (row.value != nullptr && !row.choices.empty()) {
        int idx = std::max(0, std::min(*row.value, (int)row.choices.size() - 1));
        valStr = active ? row.choices[idx] : "(global default)";
    } else if (row.boolValue != nullptr) {
        valStr = active ? (*row.boolValue ? "On" : "Off") : "(global default)";
    }

    if (!valStr.empty()) {
        int vw, vh;
        m_theme->measureText(valStr, FontSize::SMALL, vw, vh);
        m_theme->drawText(valStr, x + w - vw - 8, y + 18,
                           active ? valueCol : pal.textDisable, FontSize::SMALL);
    }
}
