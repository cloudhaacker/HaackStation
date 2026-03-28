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
    freeScreenshotTextures();
    freeTrophyTextures();

    m_game          = game;
    m_saves         = saves;
    m_open          = true;
    m_slideAnim     = 0.f;
    m_selectedItem  = 0;
    m_pendingAction = DetailsPanelAction::NONE;
    m_coverTexture  = nullptr;
    m_description.clear();
    m_screenshotPaths.clear();
    m_trophyBadgePaths.clear();
    m_screenshotIndex  = 0;
    m_trophiesUnlocked = 0;
    m_trophiesTotal    = 0;

    buildMenuItems();
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
    m_items.push_back({ "\xF0\x9F\x92\xBE", "Save System",   DetailsPanelAction::OPEN_SAVES,             true, "Save states & memory card" });
    m_items.push_back({ "\xF0\x9F\x8E\xA8", "Shaders",       DetailsPanelAction::OPEN_SHADERS,           true, "Post-process effects" });
    m_items.push_back({ "\xE2\x9C\xA8",     "AI Upscaling",  DetailsPanelAction::OPEN_AI_UPSCALE,        true, "Enhance textures" });
    m_items.push_back({ "\xE7\xBF\xBB",     "Translation",   DetailsPanelAction::OPEN_TRANSLATION,       true, "On-the-fly translation" });
    m_items.push_back({ "\xE2\x9A\x99",     "Game Settings", DetailsPanelAction::OPEN_PER_GAME_SETTINGS, true, "Per-game overrides" });
}

// ─── Screenshot loading ───────────────────────────────────────────────────────
// Looks in: media/screenshots/[safe game title]/
// Also tries the ROM filename stem as a fallback folder name.
// Loads ALL .jpg and .png files found, sorted by filename, up to MAX_SCREENSHOTS.
// If no scraped screenshots are found, the strip is hidden entirely.
// Save state thumbnails are NOT shown here — they belong in the save/load grid.
void GameDetailsPanel::setScreenshots(const std::vector<std::string>& paths) {
    m_screenshotPaths = paths;
    if (m_open) {
        freeScreenshotTextures();
        loadScreenshotTextures();
    }
}

// Sanitize a name for use as a filename/folder name (mirrors GameScraper logic)
static std::string safeFilename(const std::string& name) {
    std::string safe = name;
    const std::string invalid = "\\/:*?\"<>|";
    for (auto& c : safe)
        if (invalid.find(c) != std::string::npos) c = '_';
    return safe;
}

void GameDetailsPanel::loadScreenshotTextures() {
    freeScreenshotTextures();

    // Build candidate folder list — try clean title first, then ROM stem
    std::string mediaDir = "media/";
    std::vector<std::string> folders;

    std::string safeTitle = safeFilename(m_game.title);
    folders.push_back(mediaDir + "screenshots/" + safeTitle + "/");

    fs::path romPath(m_game.path);
    std::string romStem     = romPath.stem().string();
    std::string safeRomStem = safeFilename(romStem);
    if (safeRomStem != safeTitle)
        folders.push_back(mediaDir + "screenshots/" + safeRomStem + "/");

    // Collect all image files from the first folder that exists and has images
    std::vector<std::string> imagePaths;
    for (const auto& folder : folders) {
        if (!fs::exists(folder)) continue;
        for (const auto& entry : fs::directory_iterator(folder)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".png")
                imagePaths.push_back(entry.path().string());
        }
        if (!imagePaths.empty()) {
            std::cout << "[Details] Screenshot folder: " << folder << "\n";
            break;
        }
    }

    // Also include any explicitly-set paths (from setScreenshots() calls)
    for (const auto& p : m_screenshotPaths)
        if (fs::exists(p)) imagePaths.push_back(p);

    // Sort so numbered files (01_, 02_, 03_) appear in the right order
    std::sort(imagePaths.begin(), imagePaths.end());

    // Load up to MAX_SCREENSHOTS textures
    for (const auto& path : imagePaths) {
        SDL_Texture* tex = IMG_LoadTexture(m_renderer, path.c_str());
        if (tex) {
            m_screenshotTextures.push_back(tex);
            std::cout << "[Details] Screenshot loaded: " << path << "\n";
            if ((int)m_screenshotTextures.size() >= MAX_SCREENSHOTS) break;
        }
    }

    if (m_screenshotTextures.empty())
        std::cout << "[Details] No screenshots found for: " << m_game.title << "\n";

    m_screenshotIndex = 0;
}

void GameDetailsPanel::freeScreenshotTextures() {
    for (auto* t : m_screenshotTextures)
        if (t) SDL_DestroyTexture(t);
    m_screenshotTextures.clear();
}

// ─── Trophy loading ───────────────────────────────────────────────────────────
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
            if ((int)m_trophyTextures.size() >= 5) break;
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
    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE)
        navigateMenu(action);
}

void GameDetailsPanel::navigateMenu(NavAction action) {
    int cols = 2;

    switch (action) {
        // ── Menu grid (arrows) ─────────────────────────────────────────────
        case NavAction::UP:
            if (m_selectedItem >= cols) m_selectedItem -= cols;
            break;
        case NavAction::DOWN:
            if (m_selectedItem + cols < (int)m_items.size())
                m_selectedItem += cols;
            break;
        case NavAction::LEFT:
            if (m_selectedItem % cols != 0) m_selectedItem--;
            break;
        case NavAction::RIGHT:
            if (m_selectedItem % cols != cols - 1 &&
                m_selectedItem + 1 < (int)m_items.size())
                m_selectedItem++;
            break;

        // ── Screenshot cycling (L1/R1 or Page Up/Down) ────────────────────
        case NavAction::SHOULDER_L:
            if ((int)m_screenshotTextures.size() > 1)
                m_screenshotIndex = (m_screenshotIndex - 1 +
                    (int)m_screenshotTextures.size()) %
                    (int)m_screenshotTextures.size();
            break;
        case NavAction::SHOULDER_R:
            if ((int)m_screenshotTextures.size() > 1)
                m_screenshotIndex = (m_screenshotIndex + 1) %
                    (int)m_screenshotTextures.size();
            break;

        case NavAction::CONFIRM:
            activateSelected();
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_pendingAction = DetailsPanelAction::CLOSE;
            m_open = false;
            freeScreenshotTextures();
            freeTrophyTextures();
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
    float speed = 8.f;
    m_slideAnim += (1.f - m_slideAnim) * speed * (deltaMs / 1000.f);
    if (m_slideAnim > 0.99f) m_slideAnim = 1.f;
}

// ─── Render ───────────────────────────────────────────────────────────────────
void GameDetailsPanel::render() {
    if (!m_open) return;
    renderDimLayer();
    renderCoverHero();
    renderPanel();
}

void GameDetailsPanel::renderDimLayer() {
    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)(180 * m_slideAnim));
    SDL_Rect dimRect = { 0, 0, panelX, m_h };
    SDL_RenderFillRect(m_renderer, &dimRect);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
}

void GameDetailsPanel::renderCoverHero() {
    if (!m_coverTexture) return;

    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;
    int shelfW      = panelX;

    int coverW = (int)(shelfW * 0.55f);
    int coverH = (int)(coverW * 1.25f);
    int coverX = (shelfW - coverW) / 2;
    int coverY = (m_h - coverH) / 2;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 120);
    SDL_Rect shadow = { coverX + 8, coverY + 8, coverW, coverH };
    SDL_RenderFillRect(m_renderer, &shadow);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_Rect coverRect = { coverX, coverY, coverW, coverH };
    SDL_RenderCopy(m_renderer, m_coverTexture, nullptr, &coverRect);

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

// ─── renderPanel ─────────────────────────────────────────────────────────────
// Top-to-bottom layout, right panel:
//   [Screenshot strip — 40% panel height, only if screenshots exist]
//   [Divider]
//   [Trophy row]
//   [Divider]
//   [Game description]
//   [Menu grid — anchored to bottom, always visible]
//   [Footer]
void GameDetailsPanel::renderPanel() {
    const auto& pal = m_theme->palette();
    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;

    SDL_Rect panel = { panelX, 0, panelW, m_h };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer,
        pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 245);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_Rect border = { panelX, 0, 3, m_h };
    m_theme->drawRect(border, pal.accent);

    int contentX = panelX + 16;
    int contentW = panelW - 32;
    int y        = 16;

    bool hasScreenshots = !m_screenshotTextures.empty();
    if (hasScreenshots) {
        renderScreenshotStrip(contentX, contentW, y);
        y += (int)(m_h * 0.40f) + 8;
        m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
        y += 8;
    }

    renderTrophyRow(contentX, contentW, y);
    y += (m_trophiesTotal > 0) ? (20 + 40 + 8) : 28;

    m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
    y += 8;

    renderDescription(contentX, contentW, y);

    // Menu grid always anchored to bottom
    renderMenuGrid(contentX, contentW);

    // Footer — include screenshot hint if multiple shots exist
    m_theme->drawFooterHints(m_w, m_h, "Select", "Back");
    if (hasScreenshots && (int)m_screenshotTextures.size() > 1) {
        m_theme->drawText(
            "PgUp/PgDn or L1/R1: cycle screenshots",
            contentX,
            m_h - m_theme->layout().footerH + 8,
            pal.textDisable, FontSize::TINY);
    }
}

// ─── Screenshot strip ─────────────────────────────────────────────────────────
// 40% of panel height. Image is letterboxed/pillarboxed to fill the area
// correctly regardless of whether it's 4:3 native, 16:9, or anything else.
// Black bars fill any unused space — no stretching, no cropping.
void GameDetailsPanel::renderScreenshotStrip(int contentX, int contentW, int topY) {
    const auto& pal = m_theme->palette();

    // Header row
    std::string header = "Screenshots";
    if ((int)m_screenshotTextures.size() > 1)
        header += "  " + std::to_string(m_screenshotIndex + 1)
                + "/" + std::to_string(m_screenshotTextures.size());
    m_theme->drawText(header, contentX, topY, pal.textSecond, FontSize::SMALL);
    topY += 20;

    int areaW = contentW;
    int areaH = (int)(m_h * 0.40f) - 20; // subtract header row

    SDL_Rect area = { contentX, topY, areaW, areaH };

    // Black background always — covers letterbox bars cleanly
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(m_renderer, &area);

    if (m_screenshotIndex < (int)m_screenshotTextures.size()) {
        SDL_Texture* tex = m_screenshotTextures[m_screenshotIndex];

        int texW = 0, texH = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

        if (texW > 0 && texH > 0) {
            float texAspect  = (float)texW / (float)texH;
            float areaAspect = (float)areaW / (float)areaH;

            int drawW, drawH;
            if (texAspect > areaAspect) {
                // Image wider than area — fit width, letterbox top/bottom
                drawW = areaW;
                drawH = (int)(areaW / texAspect);
            } else {
                // Image taller than area — fit height, pillarbox left/right
                drawH = areaH;
                drawW = (int)(areaH * texAspect);
            }

            int drawX = area.x + (areaW - drawW) / 2;
            int drawY = area.y + (areaH - drawH) / 2;

            SDL_Rect dst = { drawX, drawY, drawW, drawH };
            SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        }
    }
}

// ─── Trophy row ───────────────────────────────────────────────────────────────
void GameDetailsPanel::renderTrophyRow(int contentX, int contentW, int y) {
    const auto& pal = m_theme->palette();

    if (m_trophiesTotal > 0) {
        std::string str = std::to_string(m_trophiesUnlocked) +
                          "/" + std::to_string(m_trophiesTotal) + " achievements";
        m_theme->drawText(str, contentX, y, pal.textSecond, FontSize::SMALL);
        y += 20;

        int badgeSize = 40;
        int bx = contentX;
        for (auto* tex : m_trophyTextures) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            SDL_RenderCopy(m_renderer, tex, nullptr, &br);
            bx += badgeSize + 4;
        }
        SDL_Color dimBadge = { 50, 50, 70, 255 };
        for (int i = (int)m_trophyTextures.size(); i < 5; i++) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            m_theme->drawRoundRect(br, dimBadge, 4);
            bx += badgeSize + 4;
        }
    } else {
        m_theme->drawText("No achievements loaded",
            contentX, y, pal.textDisable, FontSize::SMALL);
    }
}

// ─── Description ──────────────────────────────────────────────────────────────
void GameDetailsPanel::renderDescription(int contentX, int contentW, int y) {
    const auto& pal = m_theme->palette();
    if (!m_description.empty()) {
        std::string desc = m_description;
        if (desc.size() > 180) desc = desc.substr(0, 177) + "...";
        m_theme->drawTextWrapped(desc, contentX, y, contentW,
            pal.textSecond, FontSize::TINY);
    } else {
        m_theme->drawText("No description available",
            contentX, y, pal.textDisable, FontSize::TINY);
    }
}

// ─── Menu grid ────────────────────────────────────────────────────────────────
// Anchored to the bottom of the panel so it's always reachable regardless
// of how much content is stacked above it.
void GameDetailsPanel::renderMenuGrid(int contentX, int contentW) {
    const auto& pal = m_theme->palette();

    int footerH = m_theme->layout().footerH;
    int cols    = 2;
    int itemH   = 72;
    int rows    = ((int)m_items.size() + cols - 1) / cols;
    int gridH   = rows * itemH + (rows - 1) * 8;
    int gridY   = m_h - footerH - gridH - 12;

    m_theme->drawLine(contentX, gridY - 8,
                       contentX + contentW, gridY - 8, pal.gridLine);

    int itemW = (contentW - 8) / cols;

    for (int i = 0; i < (int)m_items.size(); i++) {
        int col = i % cols;
        int row = i / cols;
        int x   = contentX + col * (itemW + 8);
        int y   = gridY + row * (itemH + 8);
        bool sel = (i == m_selectedItem);

        SDL_Rect itemRect = { x, y, itemW, itemH };

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

        m_theme->drawTextCentered(m_items[i].icon,
            x + itemW/2, y + 12,
            sel ? pal.accent : pal.textSecond, FontSize::TITLE);
        m_theme->drawTextCentered(m_items[i].label,
            x + itemW/2, y + 46,
            sel ? pal.textPrimary : pal.textSecond, FontSize::TINY);
    }
}
