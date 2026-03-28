#include "game_details_panel.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

GameDetailsPanel::GameDetailsPanel(SDL_Renderer* renderer,
                                    ThemeEngine* theme,
                                    ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

GameDetailsPanel::~GameDetailsPanel() {
    freeScreenshotTextures();
    freeTrophyTextures();
}

void GameDetailsPanel::open(const GameEntry& game, SaveStateManager* saves) {
    // Always free old textures before loading new game's data
    freeScreenshotTextures();
    freeTrophyTextures();

    m_game          = game;
    m_saves         = saves;
    m_open          = true;
    m_slideAnim     = 0.f;
    m_selectedItem  = 0;
    m_pendingAction = DetailsPanelAction::NONE;
    m_coverTexture  = nullptr; // Will be set by caller
    m_description.clear();
    m_screenshotPaths.clear();
    m_trophyBadgePaths.clear();
    m_screenshotIndex  = 0;
    m_trophiesUnlocked = 0;
    m_trophiesTotal    = 0;

    buildMenuItems();

    // Load screenshots - don't change save manager game context
    // The save manager already tracks the last played game
    loadScreenshotTextures();
    loadTrophyTextures();
    std::cout << "[Details] Opened for: " << game.title << "\n";
}

void GameDetailsPanel::close() {
    m_open = false;
    freeScreenshotTextures();
    freeTrophyTextures();
}

void GameDetailsPanel::onWindowResize(int w, int h) {
    m_w = w;
    m_h = h;
}

// ─── Menu items ───────────────────────────────────────────────────────────────
void GameDetailsPanel::buildMenuItems() {
    m_items.clear();
    m_items.push_back({ "💾", "Save System",    DetailsPanelAction::OPEN_SAVES,            true, "Save states & memory card" });
    m_items.push_back({ "🎨", "Shaders",        DetailsPanelAction::OPEN_SHADERS,          true, "Post-process effects" });
    m_items.push_back({ "✨", "AI Upscaling",   DetailsPanelAction::OPEN_AI_UPSCALE,       true, "Enhance textures" });
    m_items.push_back({ "翻", "Translation",    DetailsPanelAction::OPEN_TRANSLATION,      true, "On-the-fly translation" });
    m_items.push_back({ "⚙", "Game Settings",  DetailsPanelAction::OPEN_PER_GAME_SETTINGS,true, "Per-game overrides" });
}

// ─── Screenshots ──────────────────────────────────────────────────────────────
void GameDetailsPanel::setScreenshots(const std::vector<std::string>& paths) {
    m_screenshotPaths = paths;
    if (m_open) {
        freeScreenshotTextures();
        loadScreenshotTextures();
    }
}

void GameDetailsPanel::loadScreenshotTextures() {
    freeScreenshotTextures();

    // Look for save state thumbnails in the game's save directory
    // Save states use the ROM filename stem, not the clean title
    // Try both the clean title and the full path stem
    std::vector<std::string> titleVariants;
    titleVariants.push_back(m_game.title);

    // Also try the ROM filename stem (e.g. "Crash Bandicoot (USA)")
    fs::path romPath(m_game.path);
    std::string romStem = romPath.stem().string();
    if (romStem != m_game.title) titleVariants.push_back(romStem);

    for (const auto& titleVar : titleVariants) {
        std::string saveDir = "saves/states/" + titleVar + "/";
        if (!fs::exists(saveDir)) continue;

        for (const auto& entry : fs::directory_iterator(saveDir)) {
            if (entry.path().extension().string() != ".png") continue;
            std::string name = entry.path().stem().string();
            if (name == "auto" || name.rfind("slot_", 0) == 0) {
                SDL_Texture* tex = IMG_LoadTexture(m_renderer,
                    entry.path().string().c_str());
                if (tex) {
                    m_screenshotTextures.push_back(tex);
                    if (m_screenshotTextures.size() >= 5) break;
                }
            }
        }
        if (!m_screenshotTextures.empty()) break;
    }

    // Also load from explicit screenshot paths (from scraper)
    for (const auto& p : m_screenshotPaths) {
        if (fs::exists(p)) {
            SDL_Texture* tex = IMG_LoadTexture(m_renderer, p.c_str());
            if (tex) m_screenshotTextures.push_back(tex);
            if (m_screenshotTextures.size() >= 5) break;
        }
    }

    std::cout << "[Details] Loaded " << m_screenshotTextures.size()
              << " screenshots for: " << m_game.title << "";
}

void GameDetailsPanel::freeScreenshotTextures() {
    for (auto* t : m_screenshotTextures)
        if (t) SDL_DestroyTexture(t);
    m_screenshotTextures.clear();
}

// ─── Trophies ─────────────────────────────────────────────────────────────────
void GameDetailsPanel::setTrophyInfo(int unlocked, int total,
                                      const std::vector<std::string>& badgePaths) {
    m_trophiesUnlocked = unlocked;
    m_trophiesTotal    = total;
    m_trophyBadgePaths = badgePaths;
    if (m_open) {
        freeTrophyTextures();
        loadTrophyTextures();
    }
}

void GameDetailsPanel::loadTrophyTextures() {
    freeTrophyTextures();
    for (const auto& p : m_trophyBadgePaths) {
        if (fs::exists(p)) {
            SDL_Texture* tex = IMG_LoadTexture(m_renderer, p.c_str());
            if (tex) m_trophyTextures.push_back(tex);
            if (m_trophyTextures.size() >= 5) break;
        }
    }
}

void GameDetailsPanel::freeTrophyTextures() {
    for (auto* t : m_trophyTextures)
        if (t) SDL_DestroyTexture(t);
    m_trophyTextures.clear();
}

// ─── Events ───────────────────────────────────────────────────────────────────
void GameDetailsPanel::handleEvent(const SDL_Event& e) {
    if (!m_open) return;
    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE)
        navigateMenu(action);
}

void GameDetailsPanel::navigateMenu(NavAction action) {
    int cols = 2;
    int rows = ((int)m_items.size() + cols - 1) / cols;

    switch (action) {
        case NavAction::UP:
            if (m_selectedItem >= cols)
                m_selectedItem -= cols;
            break;
        case NavAction::DOWN:
            if (m_selectedItem + cols < (int)m_items.size())
                m_selectedItem += cols;
            break;
        case NavAction::LEFT:
            // Cycle screenshots
            if (!m_screenshotTextures.empty() && m_screenshotTextures.size() > 1)
                m_screenshotIndex = (m_screenshotIndex - 1 +
                    (int)m_screenshotTextures.size()) %
                    (int)m_screenshotTextures.size();
            else if (m_selectedItem % cols != 0)
                m_selectedItem--;
            break;
        case NavAction::RIGHT:
            // Cycle screenshots
            if (!m_screenshotTextures.empty() && m_screenshotTextures.size() > 1)
                m_screenshotIndex = (m_screenshotIndex + 1) %
                    (int)m_screenshotTextures.size();
            else if (m_selectedItem % cols != cols - 1 &&
                m_selectedItem + 1 < (int)m_items.size())
                m_selectedItem++;
            break;
        case NavAction::CONFIRM:
            activateSelected();
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_pendingAction = DetailsPanelAction::CLOSE;
            m_open = false;
            freeTrophyTextures();
            freeScreenshotTextures();
            break;
        default: break;
    }
}

void GameDetailsPanel::activateSelected() {
    if (m_selectedItem < (int)m_items.size()) {
        m_pendingAction = m_items[m_selectedItem].action;
        m_nav->rumbleConfirm();
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void GameDetailsPanel::update(float deltaMs) {
    if (!m_open) return;
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);

    // Slide in animation
    float target = 1.f;
    float speed  = deltaMs * 0.008f;
    m_slideAnim  = std::min(1.f, m_slideAnim + speed);
}

// ─── Render ───────────────────────────────────────────────────────────────────
void GameDetailsPanel::render() {
    if (!m_open) return;

    renderDimLayer();
    renderCoverHero();
    renderPanel();
}

void GameDetailsPanel::renderDimLayer() {
    // Dim the left portion (the shelf area)
    int panelW = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX = m_w - panelW + slideOffset;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)(180 * m_slideAnim));
    SDL_Rect dimRect = { 0, 0, panelX, m_h };
    SDL_RenderFillRect(m_renderer, &dimRect);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
}

void GameDetailsPanel::renderCoverHero() {
    if (!m_coverTexture) return;

    int panelW    = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX    = m_w - panelW + slideOffset;
    int shelfW    = panelX;

    // Cover art centered in the shelf area, large
    int coverW = (int)(shelfW * 0.55f);
    int coverH = (int)(coverW * 1.25f); // PS1 box art aspect ratio
    int coverX = (shelfW - coverW) / 2;
    int coverY = (m_h - coverH) / 2;

    // Drop shadow
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 120);
    SDL_Rect shadow = { coverX + 8, coverY + 8, coverW, coverH };
    SDL_RenderFillRect(m_renderer, &shadow);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Cover art
    SDL_Rect coverRect = { coverX, coverY, coverW, coverH };
    SDL_RenderCopy(m_renderer, m_coverTexture, nullptr, &coverRect);

    // Game title below cover
    m_theme->drawTextCentered(m_game.title,
        shelfW / 2, coverY + coverH + 16,
        m_theme->palette().textPrimary, FontSize::BODY);

    if (m_game.isMultiDisc) {
        std::string discStr = std::to_string(m_game.discCount) + " discs";
        m_theme->drawTextCentered(discStr,
            shelfW / 2, coverY + coverH + 40,
            m_theme->palette().multiDisc, FontSize::SMALL);
    }
}

void GameDetailsPanel::renderPanel() {
    const auto& pal = m_theme->palette();
    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;

    // Panel background
    SDL_Rect panel = { panelX, 0, panelW, m_h };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer,
        pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 245);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Accent left border
    SDL_Rect border = { panelX, 0, 3, m_h };
    m_theme->drawRect(border, pal.accent);

    int contentX = panelX + 16;
    int contentW = panelW - 32;
    int y = 16;

    // ── Screenshots strip ─────────────────────────────────────────────────────
    // Screenshot display - show current screenshot, L/R to cycle
    int ssH = (int)(contentW * 9.f / 16.f); // 16:9 aspect ratio
    ssH = std::min(ssH, 130);               // Cap height

    // Header with index indicator
    std::string ssHeader = "Screenshots";
    if (m_screenshotTextures.size() > 1) {
        ssHeader += "  " + std::to_string(m_screenshotIndex + 1) +
                    "/" + std::to_string(m_screenshotTextures.size());
    }
    m_theme->drawText(ssHeader, contentX, y, pal.textSecond, FontSize::SMALL);
    if (m_screenshotTextures.size() > 1)
        m_theme->drawText("< L    R >", contentX + contentW - 60, y,
            pal.textDisable, FontSize::TINY);
    y += 20;

    SDL_Rect ssRect = { contentX, y, contentW, ssH };
    if (!m_screenshotTextures.empty() &&
        m_screenshotIndex < (int)m_screenshotTextures.size()) {
        SDL_RenderCopy(m_renderer,
            m_screenshotTextures[m_screenshotIndex], nullptr, &ssRect);
    } else {
        m_theme->drawRect(ssRect, pal.bgCard);
        m_theme->drawTextCentered("No screenshots yet",
            contentX + contentW/2, y + ssH/2 - 8,
            pal.textDisable, FontSize::SMALL);
    }
    y += ssH + 8;

    // ── Trophy row ────────────────────────────────────────────────────────────
    m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
    y += 8;

    if (m_trophiesTotal > 0) {
        std::string trophyStr = std::to_string(m_trophiesUnlocked) +
                                "/" + std::to_string(m_trophiesTotal) +
                                " trophies";
        m_theme->drawText(trophyStr, contentX, y,
            pal.textSecond, FontSize::SMALL);
        y += 20;

        // Recent trophy badges
        int badgeSize = 40;
        int bx = contentX;
        for (auto* tex : m_trophyTextures) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            SDL_RenderCopy(m_renderer, tex, nullptr, &br);
            bx += badgeSize + 4;
        }

        // Empty badge slots
        int shown = (int)m_trophyTextures.size();
        SDL_Color dimBadge = { 50, 50, 70, 255 };
        for (int i = shown; i < 5; i++) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            m_theme->drawRoundRect(br, dimBadge, 4);
            bx += badgeSize + 4;
        }
        y += badgeSize + 8;
    } else {
        m_theme->drawText("No RA achievements", contentX, y,
            pal.textDisable, FontSize::SMALL);
        y += 24;
    }

    // ── Game description ──────────────────────────────────────────────────────
    m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
    y += 8;

    if (!m_description.empty()) {
        // Show first ~120 chars of description
        std::string desc = m_description;
        if (desc.size() > 120) desc = desc.substr(0, 117) + "...";
        m_theme->drawTextWrapped(desc, contentX, y, contentW,
            pal.textSecond, FontSize::TINY);
        y += 52;
    } else {
        m_theme->drawText("No description available",
            contentX, y, pal.textDisable, FontSize::TINY);
        y += 24;
    }

    // ── Menu grid ─────────────────────────────────────────────────────────────
    m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
    y += 12;
    renderMenuGrid();

    // ── Footer ────────────────────────────────────────────────────────────────
    m_theme->drawFooterHints(m_w, m_h, "Select", "Back");
}

void GameDetailsPanel::renderMenuGrid() {
    const auto& pal = m_theme->palette();
    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;
    int contentX    = panelX + 16;
    int contentW    = panelW - 32;

    // Calculate grid start Y (bottom section of panel)
    int gridY   = m_h - m_theme->layout().footerH - 180;
    int cols    = 2;
    int itemW   = (contentW - 8) / cols;
    int itemH   = 72;

    for (int i = 0; i < (int)m_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        int x   = contentX + col * (itemW + 8);
        int y   = gridY + row * (itemH + 8);
        bool sel = (i == m_selectedItem);

        SDL_Rect itemRect = { x, y, itemW, itemH };

        // Background
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = sel ? pal.bgCardHover : pal.bgCard;
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 220);
        SDL_RenderFillRect(m_renderer, &itemRect);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        if (sel) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderDrawRect(m_renderer, &itemRect);
        }

        // Icon
        m_theme->drawTextCentered(m_items[i].icon,
            x + itemW/2, y + 12,
            sel ? pal.accent : pal.textSecond, FontSize::TITLE);

        // Label
        m_theme->drawTextCentered(m_items[i].label,
            x + itemW/2, y + 46,
            sel ? pal.textPrimary : pal.textSecond, FontSize::TINY);
    }
}
