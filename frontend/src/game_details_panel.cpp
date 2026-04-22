#include "game_details_panel.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// ─── Sanitize filename (mirrors GameScraper logic) ────────────────────────────
static std::string safeFilename(const std::string& name) {
    std::string safe = name;
    const std::string invalid = "\\/:*?\"<>|";
    for (auto& c : safe)
        if (invalid.find(c) != std::string::npos) c = '_';
    return safe;
}

// ─── Minimal JSON string field extractor ─────────────────────────────────────
// Extracts the value of a simple "key": "value" pair from a JSON string.
// Handles basic escape sequences. Not a full JSON parser — only used for
// our own controlled sidecar format so this is sufficient.
static std::string jsonExtract(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos]==' '||json[pos]=='\t'||
                                  json[pos]=='\r'||json[pos]=='\n')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos+1 < json.size()) {
            pos++;
            if      (json[pos] == 'n') val += '\n';
            else if (json[pos] == 'r') val += '\r';   // \r from ScreenScraper
            else if (json[pos] == 't') val += '\t';
            else if (json[pos] == '"') val += '"';
            else if (json[pos] == '\\') val += '\\';
            else                        val += json[pos];
        } else {
            val += json[pos];
        }
        pos++;
    }
    return val;
}

// ─── Load metadata sidecar ────────────────────────────────────────────────────
// Looks for media/info/[safe title].json and extracts the description.
// Also tries media/info/[rom stem].json as fallback.
// If found, populates m_description (only if not already set by setDescription()).
void GameDetailsPanel::loadMetadataSidecar() {
    if (!m_description.empty()) return; // already set externally, don't overwrite

    // Try scraped title first, then ROM stem as fallback
    std::vector<std::string> candidates = {
        m_mediaDir + "info/" + safeFilename(m_game.title) + ".json",
    };
    fs::path romPath(m_game.path);
    std::string romStem = safeFilename(romPath.stem().string());
    if (romStem != safeFilename(m_game.title))
        candidates.push_back(m_mediaDir + "info/" + romStem + ".json");

    for (const auto& infoPath : candidates) {
        if (!fs::exists(infoPath)) continue;

        std::ifstream f(infoPath);
        if (!f.is_open()) continue;

        std::stringstream ss;
        ss << f.rdbuf();
        std::string json = ss.str();

        std::string desc = jsonExtract(json, "description");
        if (!desc.empty()) {
            m_description = desc;
            // Also load other fields if the game entry didn't have them
            std::string dev = jsonExtract(json, "developer");
            std::string pub = jsonExtract(json, "publisher");
            std::string dat = jsonExtract(json, "releaseDate");
            std::string gen = jsonExtract(json, "genre");
            // Store for potential display (future expansion)
            if (m_developer.empty())   m_developer   = dev;
            if (m_publisher.empty())   m_publisher   = pub;
            if (m_releaseDate.empty()) m_releaseDate = dat;
            if (m_genre.empty())       m_genre       = gen;

            std::cout << "[Details] Loaded metadata from: " << infoPath << "\n";
            return;
        }
    }

    std::cout << "[Details] No metadata sidecar found for: " << m_game.title << "\n";
}

void GameDetailsPanel::open(const GameEntry& game, SaveStateManager* saves) {
    freeScreenshotTextures();
    freeTrophyTextures();

    m_game           = game;
    m_saves          = saves;
    m_open           = true;
    m_slideAnim      = 0.f;
    m_selectedItem   = 0;
    m_descHighlighted  = false;
    m_descFocused      = false;
    m_descScrollOffset = 0;
    m_pendingAction  = DetailsPanelAction::NONE;
    m_coverTexture   = nullptr;
    m_description.clear();
    m_developer.clear();
    m_publisher.clear();
    m_releaseDate.clear();
    m_genre.clear();
    m_screenshotPaths.clear();
    m_trophyBadgePaths.clear();
    m_screenshotIndex  = 0;
    m_trophiesUnlocked = 0;
    m_trophiesTotal    = 0;

    buildMenuItems();
    loadMetadataSidecar();    // ← Load description from disk
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
    // 4 buttons in a 2x2 grid — condensed from the original 5.
    // Shaders + AI Upscaling merged into Enhancements (opens OPEN_SHADERS
    // for now; a dedicated Enhancements screen can be wired later).
    m_items.push_back({ "\xF0\x9F\x92\xBE", "Save System",   DetailsPanelAction::OPEN_SAVES,             true, "Save states & memory card" });
    m_items.push_back({ "\xE2\x9A\x99",     "Game Settings", DetailsPanelAction::OPEN_PER_GAME_SETTINGS, true, "Per-game overrides" });
    m_items.push_back({ "\xF0\x9F\x8E\xA8", "Enhancements",  DetailsPanelAction::OPEN_SHADERS,           true, "Shaders, upscaling & more" });
    m_items.push_back({ "\xE7\xBF\xBB",     "Translation",   DetailsPanelAction::OPEN_TRANSLATION,       true, "On-the-fly translation" });
}

// ─── Screenshot loading ───────────────────────────────────────────────────────
void GameDetailsPanel::setScreenshots(const std::vector<std::string>& paths) {
    m_screenshotPaths = paths;
    if (m_open) {
        freeScreenshotTextures();
        loadScreenshotTextures();
    }
}

void GameDetailsPanel::loadScreenshotTextures() {
    freeScreenshotTextures();

    std::vector<std::string> folders;

    std::string safeTitle = safeFilename(m_game.title);
    folders.push_back(m_mediaDir + "screenshots/" + safeTitle + "/");

    fs::path romPath(m_game.path);
    std::string romStem     = romPath.stem().string();
    std::string safeRomStem = safeFilename(romStem);
    if (safeRomStem != safeTitle)
        folders.push_back(m_mediaDir + "screenshots/" + safeRomStem + "/");

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

    for (const auto& p : m_screenshotPaths)
        if (fs::exists(p)) imagePaths.push_back(p);

    std::sort(imagePaths.begin(), imagePaths.end());

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
    if (m_open) { freeTrophyTextures(); loadTrophyTextures(); }
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
    if (action != NavAction::NONE) navigateMenu(action);

    // Only forward held-input to navigateMenu when NOT in desc interaction —
    // desc scrolling uses its own 4px step via navigateMenu, and we don't
    // want the normal grid repeat-rate fighting with it.
    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE) navigateMenu(action);
}

void GameDetailsPanel::navigateMenu(NavAction action) {
    int cols = 2;

    // Description interaction has two sub-states:
    //   m_descHighlighted = true  → box has border, UP/DOWN move cursor; A enters scroll
    //   m_descFocused     = true  → actively scrolling; UP/DOWN scroll text; B exits to highlighted
    //
    // Flow: normal nav → UP from top row → highlighted → A → scrolling → B → highlighted → B → normal

    if (m_descFocused) {
        // Actively scrolling the description — 4px step feels smooth with hold-repeat
        switch (action) {
            case NavAction::UP:
                m_descScrollOffset -= 4;
                if (m_descScrollOffset < 0) m_descScrollOffset = 0;
                break;
            case NavAction::DOWN:
                // Upper clamp applied in renderDescription where totalTextH is known
                m_descScrollOffset += 4;
                break;
            case NavAction::BACK:
                // Exit scroll mode, return to highlighted
                m_descFocused = false;
                break;
            default: break;
        }
        return;
    }

    if (m_descHighlighted) {
        // Description box is selected/highlighted but not yet scrolling
        switch (action) {
            case NavAction::CONFIRM:
                // A button: enter scroll mode, start from top
                m_descFocused     = true;
                m_descScrollOffset = 0;
                break;
            case NavAction::DOWN:
                // Move down into the button grid (top row)
                m_descHighlighted = false;
                m_selectedItem    = 0;
                break;
            case NavAction::BACK:
                // B from highlighted: exit panel
                m_pendingAction = DetailsPanelAction::CLOSE;
                m_open = false;
                freeScreenshotTextures();
                freeTrophyTextures();
                break;
            default: break;
        }
        return;
    }

    switch (action) {
        case NavAction::UP:
            // If at top row of menu, highlight the description box
            if (m_selectedItem < cols) {
                m_descHighlighted = true;
                m_selectedItem    = -1;  // no grid item selected while desc is highlighted
            } else {
                m_selectedItem -= cols;
            }
            break;
        case NavAction::DOWN:
            if (m_selectedItem + cols < (int)m_items.size())
                m_selectedItem += cols;
            break;
        case NavAction::LEFT:
            if (m_selectedItem % cols != 0) m_selectedItem--; break;
        case NavAction::RIGHT:
            if (m_selectedItem % cols != cols - 1 &&
                m_selectedItem + 1 < (int)m_items.size())
                m_selectedItem++;
            break;
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
            activateSelected(); break;
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
    if (m_selectedItem >= 0 && m_selectedItem < (int)m_items.size()) {
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

    int maxW = (int)(shelfW * 0.55f);
    int maxH = (int)(m_h * 0.70f);
    int areaX = (shelfW - maxW) / 2;
    int areaY = (m_h - maxH) / 2;

    SDL_Rect img = { areaX, areaY, maxW, maxH };
    {
        int texW = 0, texH = 0;
        SDL_QueryTexture(m_coverTexture, nullptr, nullptr, &texW, &texH);
        if (texW > 0 && texH > 0) {
            float scale = std::min((float)maxW / texW, (float)maxH / texH);
            int dw = (int)(texW * scale);
            int dh = (int)(texH * scale);
            img = { areaX + (maxW - dw) / 2, areaY + (maxH - dh) / 2, dw, dh };
        }
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 130);
    SDL_Rect shadow = { img.x + 10, img.y + 10, img.w, img.h };
    SDL_RenderFillRect(m_renderer, &shadow);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    SDL_RenderCopy(m_renderer, m_coverTexture, nullptr, &img);

    m_theme->drawTextCentered(m_game.title,
        shelfW / 2, img.y + img.h + 16,
        m_theme->palette().textPrimary, FontSize::BODY);

    if (m_game.isMultiDisc) {
        std::string discStr = std::to_string(m_game.discCount) + " discs";
        m_theme->drawTextCentered(discStr,
            shelfW / 2, img.y + img.h + 40,
            m_theme->palette().multiDisc, FontSize::SMALL);
    }
}

// ─── renderPanel ─────────────────────────────────────────────────────────────
void GameDetailsPanel::renderPanel() {
    const auto& pal = m_theme->palette();
    int panelW      = (int)(m_w * PANEL_FRACTION);
    int slideOffset = (int)((1.f - m_slideAnim) * panelW);
    int panelX      = m_w - panelW + slideOffset;

    SDL_Rect panel = { panelX, 0, panelW, m_h };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 245);
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
    // Fixed height: 20px count label + 48px badges + 4px gap = 72px.
    // Constant regardless of whether achievements are loaded so that the
    // description box height (and scroll state) never jumps.
    y += 72;

    m_theme->drawLine(contentX, y, contentX + contentW, y, pal.gridLine);
    y += 8;

    renderDescription(contentX, contentW, y);

    renderMenuGrid(contentX, contentW);

    m_theme->drawFooterHints(m_w, m_h, "Select", "Back");
    if (hasScreenshots && (int)m_screenshotTextures.size() > 1) {
        m_theme->drawText(
            "PgUp/PgDn or L1/R1: cycle screenshots",
            contentX, m_h - m_theme->layout().footerH + 8,
            pal.textDisable, FontSize::TINY);
    }
}

// ─── Screenshot strip ─────────────────────────────────────────────────────────
void GameDetailsPanel::renderScreenshotStrip(int contentX, int contentW, int topY) {
    const auto& pal = m_theme->palette();

    std::string header = "Screenshots";
    if ((int)m_screenshotTextures.size() > 1)
        header += "  " + std::to_string(m_screenshotIndex + 1)
                + "/" + std::to_string(m_screenshotTextures.size());
    m_theme->drawText(header, contentX, topY, pal.textSecond, FontSize::SMALL);
    topY += 20;

    int areaW = contentW;
    int areaH = (int)(m_h * 0.40f) - 20;

    SDL_Rect area = { contentX, topY, areaW, areaH };
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
            if (texAspect > areaAspect) { drawW = areaW; drawH = (int)(areaW / texAspect); }
            else                        { drawH = areaH; drawW = (int)(areaH * texAspect); }
            SDL_Rect dst = { area.x + (areaW - drawW) / 2,
                             area.y + (areaH - drawH) / 2, drawW, drawH };
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
        // RetroAchievements badge icons are 64×64 — display at 48×48.
        int badgeSize = 48, bx = contentX;
        for (auto* tex : m_trophyTextures) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            SDL_RenderCopy(m_renderer, tex, nullptr, &br);
            bx += badgeSize + 6;
        }
        // Dim placeholder slots for any badges not yet loaded (always show 5)
        SDL_Color dimBadge = { 50, 50, 70, 255 };
        for (int i = (int)m_trophyTextures.size(); i < 5; i++) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            m_theme->drawRoundRect(br, dimBadge, 6);
            bx += badgeSize + 6;
        }
    } else {
        // No data yet — draw 5 dim placeholder slots so the layout is stable
        // and the description box height doesn't jump when achievements load.
        int badgeSize = 48, bx = contentX;
        SDL_Color dimBadge = { 40, 40, 58, 255 };
        for (int i = 0; i < 5; i++) {
            SDL_Rect br = { bx, y, badgeSize, badgeSize };
            m_theme->drawRoundRect(br, dimBadge, 6);
            bx += badgeSize + 6;
        }
        m_theme->drawText("Achievements not loaded",
            contentX, y + badgeSize + 4, pal.textDisable, FontSize::TINY);
    }
}

// ─── cleanDescription ────────────────────────────────────────────────────────
// Fixes all known ScreenScraper text encoding issues:
//   1. HTML entities: &quot; -> "   &amp; -> &   &lt; -> <   &gt; -> >
//   2. Stale 'r' artifacts from old sidecars (literal 'r' where \r\n was)
//   3. Tab indentation characters
//   4. Excess newline runs collapsed to 2
static std::string cleanDescription(const std::string& raw) {
    std::string s = raw;

    // HTML entity decode
    struct HtmlEnt { const char* entity; char ch; };
    static const HtmlEnt ents[] = {
        { "&quot;",  '"'  }, { "&amp;",  '&' }, { "&lt;",  '<' },
        { "&gt;",   '>'   }, { "&apos;", '\'' }, { nullptr, 0  }
    };
    for (int ei = 0; ents[ei].entity; ei++) {
        std::string from = ents[ei].entity;
        std::string to(1, ents[ei].ch);
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    // Fix stale 'r' artifacts and tabs
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (i + 1 < s.size() && s[i] == 'r' && s[i + 1] == '\n') {
                bool prevPunct   = i > 0 && (s[i-1]=='.'||s[i-1]=='!'||
                                             s[i-1]=='?'||s[i-1]==';'||s[i-1]==',');
                bool prevNewline = i > 0 && s[i-1] == '\n';
                if (prevPunct || prevNewline) {
                    out += '\n';
                    i++;
                    while (i + 1 < s.size() && (s[i+1]=='\t'||s[i+1]==' ')) i++;
                    continue;
                }
            }
            out += (s[i] == '\t') ? ' ' : s[i];
        }
        s = out;
    }

    // Collapse 3+ newlines to 2 and trim
    {
        std::string out; out.reserve(s.size()); int nl = 0;
        for (char c : s) { if (c=='\n'){nl++;if(nl<=2)out+=c;}else{nl=0;out+=c;} }
        s = out;
    }
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

// ─── Description (scrollable) ─────────────────────────────────────────────────
// Three visual states:
//   Normal      — plain text, hint "press up to read" if text is long
//   Highlighted — accent BORDER ONLY (no fill), hint "A = scroll  B = back"
//   Scrolling   — accent border, text scrolls with UP/DOWN, hint "B = done"
//
// The hint text is drawn OUTSIDE the clip rect so it is never covered.
void GameDetailsPanel::renderDescription(int contentX, int contentW, int y) {
    const auto& pal  = m_theme->palette();
    int footerH      = m_theme->layout().footerH;
    int cols         = 2;
    int itemH        = 72;
    int rows         = ((int)m_items.size() + cols - 1) / cols;
    int gridH        = rows * itemH + (rows - 1) * 8;

    // Reserve space at the bottom for the hint line.
    // clipBottom is where scrolled text must stop.
    // boxRect border only covers up to clipBottom — NOT the hint row below it.
    int hintH        = 22;
    int boxBottom    = m_h - footerH - gridH - 24;  // 24px gap above grid
    int clipBottom   = boxBottom - hintH;            // text clip stops before hint
    int boxH         = clipBottom - y;
    if (boxH < 40) return;

    // Issue A: border height = clipBottom - y so it never overlaps the hint text
    SDL_Rect boxRect = { contentX - 4, y - 4, contentW + 8, clipBottom - y + 4 };

    if (m_descHighlighted || m_descFocused) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(m_renderer,
            pal.accent.r, pal.accent.g, pal.accent.b, 220);
        SDL_RenderDrawRect(m_renderer, &boxRect);
    }

    std::string text = m_description.empty()
        ? "No description available."
        : cleanDescription(m_description);

    // Issue B: clamp scroll so you can't scroll past the last line.
    // Estimate total text height — ~18px per line, ~7 chars per pixel of width.
    if (m_descFocused) {
        int charsPerLine = std::max(1, contentW / 7);
        int lineCount    = 1;
        int lineLen      = 0;
        for (char c : text) {
            if (c == '\n') { lineCount++; lineLen = 0; }
            else if (++lineLen > charsPerLine) { lineCount++; lineLen = 0; }
        }
        int totalTextH = lineCount * 18;
        int maxScroll  = std::max(0, totalTextH - boxH);
        if (m_descScrollOffset > maxScroll) m_descScrollOffset = maxScroll;
    }

    // Clip to boxH so scrolled text never bleeds into the hint line
    SDL_Rect clip = { contentX, y, contentW, boxH };
    SDL_RenderSetClipRect(m_renderer, &clip);

    int drawY = y - m_descScrollOffset;
    m_theme->drawTextWrapped(text, contentX, drawY, contentW,
        m_description.empty() ? pal.textDisable : pal.textSecond,
        FontSize::SMALL);

    SDL_RenderSetClipRect(m_renderer, nullptr);  // clear BEFORE drawing hint

    // Hint text — outside clip rect, always visible
    int hintY = clipBottom + 2;
    if (m_descFocused) {
        m_theme->drawText("B = stop scrolling",
            contentX, hintY, pal.accent, FontSize::TINY);
    } else if (m_descHighlighted) {
        m_theme->drawText("A = scroll   B = back",
            contentX, hintY, pal.accent, FontSize::TINY);
    } else if ((int)text.size() > 180) {
        m_theme->drawText("Press Up to read description",
            contentX, hintY, pal.textDisable, FontSize::TINY);
    }
}

// ─── Menu grid ────────────────────────────────────────────────────────────────
void GameDetailsPanel::renderMenuGrid(int contentX, int contentW) {
    const auto& pal = m_theme->palette();
    int footerH = m_theme->layout().footerH;
    int cols    = 2;
    int itemH   = 68;
    int rows    = ((int)m_items.size() + cols - 1) / cols;
    int gridH   = rows * itemH + (rows - 1) * 8;
    int gridY   = m_h - footerH - gridH - 12;

    m_theme->drawLine(contentX, gridY - 8,
                       contentX + contentW, gridY - 8, pal.gridLine);

    int itemW = (contentW - 8) / cols;
    for (int i = 0; i < (int)m_items.size(); i++) {
        int col = i % cols, row = i / cols;
        int x   = contentX + col * (itemW + 8);
        int y   = gridY + row * (itemH + 8);
        bool sel = (m_selectedItem >= 0 && !m_descHighlighted && !m_descFocused && i == m_selectedItem);

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
            x + itemW/2, y + 10, sel ? pal.accent : pal.textSecond, FontSize::TITLE);
        m_theme->drawTextCentered(m_items[i].label,
            x + itemW/2, y + 44, sel ? pal.textPrimary : pal.textSecond, FontSize::TINY);
    }
}
