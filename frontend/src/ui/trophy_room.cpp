#include "trophy_room.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace fs = std::filesystem;

// ─── Construction / destruction ───────────────────────────────────────────────
TrophyRoom::TrophyRoom(SDL_Renderer* renderer, ThemeEngine* theme,
                        ControllerNav* nav, RAManager* ra)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_ra(ra)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

TrophyRoom::~TrophyRoom() {
    freeTextures();
}

// ─── refresh ─────────────────────────────────────────────────────────────────
// Pulls the full achievement list from RAManager, builds TrophyEntry list,
// then loads any badge textures that are already cached on disk.
void TrophyRoom::refresh() {
    freeTextures();
    m_entries.clear();
    m_visible.clear();
    m_selectedIdx = 0;
    m_scrollRow   = 0;

    // Use cached achievements if game not currently loaded — this lets the
    // trophy room work when opened from the shelf/hub between sessions.
    std::vector<AchievementInfo> achievements;
    if (m_ra && m_ra->isGameLoaded()) {
        achievements = m_ra->getAchievementsWithBadgePaths();
    } else if (m_ra && !m_ra->getCachedAchievements().empty()) {
        achievements = m_ra->getCachedAchievements();
    }

    m_entries.reserve(achievements.size());
    for (auto& a : achievements) {
        TrophyEntry e;
        e.info = std::move(a);
        m_entries.push_back(std::move(e));
    }

    std::cout << "[TrophyRoom] Loaded " << m_entries.size() << " achievements"
              << (m_ra && !m_ra->isGameLoaded() ? " (from cache)" : "") << "\n";

    loadTextures();
    m_dirty = true;
}

// ─── refreshWithData ──────────────────────────────────────────────────────────
// Populates from an explicit achievement list — use when no game is running.
// Called by app.cpp when opening from hub (per-game, specific game's cache).
void TrophyRoom::refreshWithData(const std::vector<AchievementInfo>& achievements) {
    freeTextures();
    m_entries.clear();
    m_visible.clear();
    m_selectedIdx = 0;
    m_scrollRow   = 0;

    m_entries.reserve(achievements.size());
    for (const auto& a : achievements) {
        TrophyEntry e;
        e.info = a;
        m_entries.push_back(std::move(e));
    }

    std::cout << "[TrophyRoom] Loaded " << m_entries.size()
              << " achievements (injected)\n";

    loadTextures();
    m_dirty = true;
}

// ─── Texture loading ──────────────────────────────────────────────────────────
void TrophyRoom::freeTextures() {
    for (auto& e : m_entries) {
        if (e.texture)     { SDL_DestroyTexture(e.texture);     e.texture     = nullptr; }
        if (e.textureLock) { SDL_DestroyTexture(e.textureLock); e.textureLock = nullptr; }
        e.textureLoaded = false;
    }
}

SDL_Texture* TrophyRoom::loadBadge(const std::string& path) {
    if (path.empty() || !fs::exists(path)) return nullptr;
    SDL_Texture* t = IMG_LoadTexture(m_renderer, path.c_str());
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

// Software greyscale — converts a colour texture to a grey version.
// Used when a _lock.png isn't cached yet: we greyscale the colour badge instead.
SDL_Texture* TrophyRoom::makeGreyscale(SDL_Texture* src) {
    if (!src) return nullptr;

    int w = 0, h = 0;
    SDL_QueryTexture(src, nullptr, nullptr, &w, &h);
    if (w <= 0 || h <= 0) return nullptr;

    // Render src to a surface so we can read pixels
    SDL_Texture* target = SDL_CreateTexture(m_renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!target) return nullptr;

    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, src, nullptr, nullptr);
    SDL_SetRenderTarget(m_renderer, nullptr);

    // Read pixels back
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) { SDL_DestroyTexture(target); return nullptr; }

    SDL_SetRenderTarget(m_renderer, target);
    SDL_RenderReadPixels(m_renderer, nullptr, SDL_PIXELFORMAT_RGBA8888,
                          surf->pixels, surf->pitch);
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_DestroyTexture(target);

    // Desaturate in place
    Uint8* px = (Uint8*)surf->pixels;
    for (int i = 0; i < w * h * 4; i += 4) {
        // RGBA8888: R=byte0, G=byte1, B=byte2, A=byte3
        Uint8 r = px[i], g = px[i+1], b = px[i+2];
        Uint8 grey = (Uint8)(0.299f * r + 0.587f * g + 0.114f * b);
        // Dim it a bit so locked badges look noticeably different
        grey = (Uint8)(grey * 0.55f);
        px[i] = grey; px[i+1] = grey; px[i+2] = grey;
        // alpha unchanged
    }

    SDL_Texture* grey = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    if (grey) SDL_SetTextureBlendMode(grey, SDL_BLENDMODE_BLEND);
    return grey;
}

void TrophyRoom::loadTextures() {
    for (auto& e : m_entries) {
        if (e.textureLoaded) continue;

        // Colour badge path (from RAManager)
        if (!e.info.badgeLocalPath.empty()) {
            e.texture = loadBadge(e.info.badgeLocalPath);
        }

        // Lock badge — _lock.png variant
        if (!e.info.badgeLockLocalPath.empty()) {
            e.textureLock = loadBadge(e.info.badgeLockLocalPath);
        }

        // If we have the colour badge but no lock badge yet, generate greyscale
        if (!e.textureLock && e.texture) {
            e.textureLock = makeGreyscale(e.texture);
        }

        e.textureLoaded = true;
    }
}

// ─── Visible list ─────────────────────────────────────────────────────────────
void TrophyRoom::rebuildVisible() {
    m_visible.clear();
    for (int i = 0; i < (int)m_entries.size(); i++) {
        const auto& e = m_entries[i];
        bool show = false;
        switch (m_filter) {
            case TrophyFilter::ALL:      show = true;             break;
            case TrophyFilter::UNLOCKED: show = e.info.unlocked;  break;
            case TrophyFilter::LOCKED:   show = !e.info.unlocked; break;
        }
        if (show) m_visible.push_back(i);
    }
    m_dirty = false;
    clampSelection();
}

void TrophyRoom::clampSelection() {
    if (m_visible.empty()) { m_selectedIdx = 0; m_scrollRow = 0; return; }
    if (m_selectedIdx >= (int)m_visible.size())
        m_selectedIdx = (int)m_visible.size() - 1;
    if (m_selectedIdx < 0) m_selectedIdx = 0;

    // Ensure selected row is visible
    int selRow = m_selectedIdx / COLS;
    if (selRow < m_scrollRow) m_scrollRow = selRow;

    // Compute how many rows fit in the grid area
    int gridAreaH = m_h - HEADER_H - FILTER_H - DETAIL_H - FOOTER_EXTRA - 16;
    int visRows   = std::max(1, gridAreaH / CELL_H);
    if (selRow >= m_scrollRow + visRows)
        m_scrollRow = selRow - visRows + 1;
    if (m_scrollRow < 0) m_scrollRow = 0;
}

const std::vector<int>& TrophyRoom::visibleIndices() const {
    return m_visible;
}

// ─── Events ───────────────────────────────────────────────────────────────────
void TrophyRoom::handleEvent(const SDL_Event& e) {
    // L1/R1 cycle filter tabs
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            m_filter = (TrophyFilter)(((int)m_filter + 2) % 3);
            m_dirty  = true;
            return;
        }
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            m_filter = (TrophyFilter)(((int)m_filter + 1) % 3);
            m_dirty  = true;
            return;
        }
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_LEFTBRACKET) {
            m_filter = (TrophyFilter)(((int)m_filter + 2) % 3);
            m_dirty  = true; return;
        }
        if (e.key.keysym.sym == SDLK_RIGHTBRACKET) {
            m_filter = (TrophyFilter)(((int)m_filter + 1) % 3);
            m_dirty  = true; return;
        }
    }

    NavAction action = m_nav->processEvent(e);
    if (action == NavAction::NONE) return;

    if (m_dirty) rebuildVisible();
    int total = (int)m_visible.size();
    if (total == 0) {
        if (action == NavAction::BACK || action == NavAction::MENU)
            m_wantsClose = true;
        return;
    }

    switch (action) {
        case NavAction::LEFT:
            if (m_selectedIdx % COLS > 0) { m_selectedIdx--; clampSelection(); }
            break;
        case NavAction::RIGHT:
            if (m_selectedIdx % COLS < COLS - 1 && m_selectedIdx + 1 < total) {
                m_selectedIdx++; clampSelection();
            }
            break;
        case NavAction::UP:
            if (m_selectedIdx >= COLS) { m_selectedIdx -= COLS; clampSelection(); }
            break;
        case NavAction::DOWN:
            if (m_selectedIdx + COLS < total) {
                m_selectedIdx += COLS; clampSelection();
            }
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_wantsClose = true;
            break;
        default: break;
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void TrophyRoom::update(float /*deltaMs*/) {
    if (m_dirty) rebuildVisible();

    // Try loading any textures that came in from background downloads
    bool anyNew = false;
    for (auto& e : m_entries) {
        if (!e.textureLoaded) { anyNew = true; break; }
        // Re-check if a lock texture can now be loaded from disk
        if (!e.textureLock && !e.info.badgeLockLocalPath.empty()
            && fs::exists(e.info.badgeLockLocalPath)) {
            anyNew = true; break;
        }
    }
    if (anyNew) loadTextures();
}

// ─── Render ───────────────────────────────────────────────────────────────────
void TrophyRoom::render() {
    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    if (m_dirty) rebuildVisible();

    renderHeader();
    renderFilterBar();
    renderGrid();
    renderDetailStrip();

    // Footer hint
    int footY = m_h - m_theme->layout().footerH;
    m_theme->drawLine(MARGIN, footY, m_w - MARGIN, footY, pal.gridLine);
    footY += 10;
    m_theme->drawText("L1/R1: filter    B: back",
        MARGIN, footY + 4, pal.textDisable, FontSize::SMALL);
}

// ─── renderHeader ─────────────────────────────────────────────────────────────
void TrophyRoom::renderHeader() {
    const auto& pal = m_theme->palette();

    // Trophy cup icon + "TROPHY ROOM" title
    SDL_Color gold = { 255, 210, 60, 255 };
    m_theme->drawText("\xF0\x9F\x8F\x86  TROPHY ROOM",
        MARGIN, 20, gold, FontSize::SMALL);

    // Game title on the same line, right-aligned ish
    if (!m_gameTitle.empty()) {
        m_theme->drawText(m_gameTitle,
            MARGIN, 44, pal.textSecond, FontSize::BODY);
    }

    // Progress summary — right side
    int unlocked = 0, total = (int)m_entries.size();
    for (const auto& e : m_entries)
        if (e.info.unlocked) unlocked++;

    if (total > 0) {
        std::string prog = std::to_string(unlocked) + " / " + std::to_string(total);
        int progX = m_w - MARGIN - 220;
        m_theme->drawText(prog, progX, 20, pal.textPrimary, FontSize::BODY);

        float frac = (float)unlocked / total;
        int barX = progX, barY = 44, barW = 200, barH = 14;
        SDL_Color barFill = gold;
        SDL_Color barBg   = { 40, 40, 56, 255 };
        renderProgressBar(barX, barY, barW, barH, frac, barFill, barBg);

        // Percentage
        int pct = (int)(frac * 100.f);
        m_theme->drawText(std::to_string(pct) + "%",
            barX + barW + 8, barY - 1, pal.textSecond, FontSize::TINY);
    }

    m_theme->drawLine(MARGIN, HEADER_H - 8, m_w - MARGIN, HEADER_H - 8, pal.gridLine);
}

void TrophyRoom::renderProgressBar(int x, int y, int w, int h,
                                    float fraction, SDL_Color fill, SDL_Color bg) {
    // Background track
    SDL_Rect track = { x, y, w, h };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 200);
    SDL_RenderFillRect(m_renderer, &track);

    // Fill
    int fillW = std::max(0, std::min(w, (int)(w * fraction)));
    if (fillW > 0) {
        SDL_Rect bar = { x, y, fillW, h };
        SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, 220);
        SDL_RenderFillRect(m_renderer, &bar);
    }
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Border
    SDL_SetRenderDrawColor(m_renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(m_renderer, &track);
}

// ─── renderFilterBar ──────────────────────────────────────────────────────────
void TrophyRoom::renderFilterBar() {
    const auto& pal = m_theme->palette();
    int y   = HEADER_H;
    int tabW = 140, tabH = 32, gap = 8;
    int totalTabW = tabW * 3 + gap * 2;
    int tabX = MARGIN;

    struct Tab { const char* label; TrophyFilter val; };
    static const Tab tabs[] = {
        { "ALL",      TrophyFilter::ALL      },
        { "UNLOCKED", TrophyFilter::UNLOCKED },
        { "LOCKED",   TrophyFilter::LOCKED   },
    };

    // Count per filter for the labels
    int cntAll      = (int)m_entries.size();
    int cntUnlocked = 0;
    for (const auto& e : m_entries) if (e.info.unlocked) cntUnlocked++;
    int cntLocked   = cntAll - cntUnlocked;
    int counts[]    = { cntAll, cntUnlocked, cntLocked };

    for (int i = 0; i < 3; i++) {
        bool active = (m_filter == tabs[i].val);
        SDL_Rect r  = { tabX, y + 4, tabW, tabH };

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = active ? pal.accent : pal.bgCard;
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, active ? 230 : 140);
        SDL_RenderFillRect(m_renderer, &r);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        if (active) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderDrawRect(m_renderer, &r);
        }

        SDL_Color tc = active ? SDL_Color{255,255,255,255} : pal.textSecond;
        std::string lbl = std::string(tabs[i].label) + " (" + std::to_string(counts[i]) + ")";
        m_theme->drawTextCentered(lbl, tabX + tabW / 2, y + 10, tc, FontSize::SMALL);
        tabX += tabW + gap;
    }

    m_theme->drawLine(MARGIN, HEADER_H + FILTER_H - 4,
                       m_w - MARGIN, HEADER_H + FILTER_H - 4, pal.gridLine);
}

// ─── renderGrid ───────────────────────────────────────────────────────────────
void TrophyRoom::renderGrid() {
    const auto& pal = m_theme->palette();

    int gridY    = HEADER_H + FILTER_H + 8;
    int gridAreaH = m_h - gridY - DETAIL_H - m_theme->layout().footerH - FOOTER_EXTRA - 8;
    int visRows   = std::max(1, gridAreaH / CELL_H);

    // Centre the grid horizontally
    int totalGridW = COLS * BADGE_SIZE + (COLS - 1) * BADGE_PAD;
    int gridX      = (m_w - totalGridW) / 2;

    if (m_visible.empty()) {
        std::string msg;
        if (m_filter == TrophyFilter::UNLOCKED)
            msg = "No achievements unlocked yet";
        else if (m_filter == TrophyFilter::LOCKED)
            msg = "All achievements unlocked!";
        else if (m_entries.empty())
            msg = "No achievement data — play this game to load trophies";
        else
            msg = "No achievements available";
        m_theme->drawTextCentered(msg,
            m_w / 2, gridY + 60, pal.textDisable, FontSize::BODY);
        return;
    }

    // Scroll indicator
    int totalRows = ((int)m_visible.size() + COLS - 1) / COLS;
    if (m_scrollRow > 0)
        m_theme->drawTextCentered("▲",
            m_w / 2, gridY - 14, pal.textDisable, FontSize::TINY);
    if (m_scrollRow + visRows < totalRows)
        m_theme->drawTextCentered("▼",
            m_w / 2, gridY + visRows * CELL_H + 4, pal.textDisable, FontSize::TINY);

    for (int row = 0; row < visRows; row++) {
        int absRow = m_scrollRow + row;
        for (int col = 0; col < COLS; col++) {
            int visIdx = absRow * COLS + col;
            if (visIdx >= (int)m_visible.size()) break;

            int entryIdx = m_visible[visIdx];
            const TrophyEntry& entry = m_entries[entryIdx];
            bool unlocked = entry.info.unlocked;
            bool selected = (visIdx == m_selectedIdx);

            int cellX = gridX + col * (BADGE_SIZE + BADGE_PAD);
            int cellY = gridY + row * CELL_H;

            // ── Selection glow ────────────────────────────────────────────
            if (selected) {
                SDL_Color gold = { 255, 210, 60, 255 };
                SDL_Rect sel = { cellX - 4, cellY - 4,
                                 BADGE_SIZE + 8, BADGE_SIZE + 8 };
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer,
                    gold.r, gold.g, gold.b, 60);
                SDL_RenderFillRect(m_renderer, &sel);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(m_renderer,
                    gold.r, gold.g, gold.b, 255);
                SDL_RenderDrawRect(m_renderer, &sel);
                // Second border for thickness
                SDL_Rect sel2 = { sel.x+1, sel.y+1, sel.w-2, sel.h-2 };
                SDL_RenderDrawRect(m_renderer, &sel2);
            }

            // ── Badge background ──────────────────────────────────────────
            SDL_Rect badgeRect = { cellX, cellY, BADGE_SIZE, BADGE_SIZE };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_Color bgClr = unlocked ? SDL_Color{30, 30, 45, 200}
                                       : SDL_Color{20, 20, 32, 200};
            SDL_SetRenderDrawColor(m_renderer,
                bgClr.r, bgClr.g, bgClr.b, bgClr.a);
            SDL_RenderFillRect(m_renderer, &badgeRect);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

            // ── Badge texture ─────────────────────────────────────────────
            SDL_Texture* tex = unlocked ? entry.texture : entry.textureLock;
            if (!tex && !unlocked) tex = entry.texture; // fallback: greyscale at render time

            if (tex) {
                SDL_Rect dst = badgeRect;
                if (unlocked) {
                    SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
                } else {
                    // Dim unlocked texture if we're using it as fallback
                    SDL_SetTextureColorMod(tex, 100, 100, 100);
                    SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                }
            } else {
                // No texture yet — placeholder
                SDL_SetRenderDrawColor(m_renderer, 40, 40, 58, 255);
                SDL_RenderFillRect(m_renderer, &badgeRect);
                m_theme->drawTextCentered("?",
                    cellX + BADGE_SIZE/2, cellY + BADGE_SIZE/2 - 10,
                    pal.textDisable, FontSize::TITLE);
            }

            // ── Lock overlay for locked badges ────────────────────────────
            if (!unlocked) {
                // Semi-transparent dark overlay
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 80);
                SDL_RenderFillRect(m_renderer, &badgeRect);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

                // Lock icon — small, centred bottom-right
                SDL_Color lockClr = { 160, 160, 180, 255 };
                m_theme->drawTextCentered("\xF0\x9F\x94\x92",
                    cellX + BADGE_SIZE - 14, cellY + BADGE_SIZE - 14,
                    lockClr, FontSize::TINY);
            }

            // ── Label below badge ─────────────────────────────────────────
            int labelY  = cellY + BADGE_SIZE + 4;
            SDL_Color tc = selected ? pal.textPrimary
                         : unlocked ? pal.textSecond
                                    : pal.textDisable;

            // Title — truncate if too long
            std::string title = entry.info.title;
            if ((int)title.size() > 14) title = title.substr(0, 13) + "…";
            m_theme->drawTextCentered(title,
                cellX + BADGE_SIZE / 2, labelY, tc, FontSize::TINY);

            // Points
            std::string pts = unlocked
                ? std::to_string(entry.info.points) + "pts"
                : "Locked";
            SDL_Color ptClr = unlocked
                ? SDL_Color{255, 210, 60, 200}
                : SDL_Color{100, 100, 120, 200};
            m_theme->drawTextCentered(pts,
                cellX + BADGE_SIZE / 2, labelY + 18, ptClr, FontSize::TINY);
        }
    }
}

// ─── renderDetailStrip ────────────────────────────────────────────────────────
// Shows the selected achievement's full badge, title, description, and points
// in a strip at the bottom of the screen above the footer.
void TrophyRoom::renderDetailStrip() {
    const auto& pal = m_theme->palette();

    int stripY = m_h - m_theme->layout().footerH - FOOTER_EXTRA - DETAIL_H;
    m_theme->drawLine(MARGIN, stripY, m_w - MARGIN, stripY, pal.gridLine);
    stripY += 8;

    if (m_visible.empty()) return;

    int entryIdx = m_visible[std::min(m_selectedIdx, (int)m_visible.size() - 1)];
    const TrophyEntry& entry = m_entries[entryIdx];
    bool unlocked = entry.info.unlocked;

    // Badge — 96px in the detail strip
    static constexpr int DETAIL_BADGE = 96;
    int badgeX = MARGIN;
    int badgeY = stripY + (DETAIL_H - 8 - DETAIL_BADGE) / 2;

    SDL_Rect badgeRect = { badgeX, badgeY, DETAIL_BADGE, DETAIL_BADGE };

    // Strip background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer,
        pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 160);
    SDL_Rect stripRect = { MARGIN, stripY - 4, m_w - MARGIN * 2, DETAIL_H };
    SDL_RenderFillRect(m_renderer, &stripRect);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Badge texture
    SDL_Texture* tex = unlocked ? entry.texture : entry.textureLock;
    if (!tex) tex = entry.texture;
    if (tex) {
        if (!unlocked) {
            SDL_SetTextureColorMod(tex, 80, 80, 80);
            SDL_RenderCopy(m_renderer, tex, nullptr, &badgeRect);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        } else {
            SDL_RenderCopy(m_renderer, tex, nullptr, &badgeRect);
        }
    } else {
        SDL_SetRenderDrawColor(m_renderer, 40, 40, 58, 255);
        SDL_RenderFillRect(m_renderer, &badgeRect);
        m_theme->drawTextCentered("?",
            badgeX + DETAIL_BADGE/2, badgeY + DETAIL_BADGE/2 - 10,
            pal.textDisable, FontSize::TITLE);
    }

    // Gold border for unlocked badges in detail strip
    if (unlocked) {
        SDL_Color gold = { 255, 210, 60, 255 };
        SDL_SetRenderDrawColor(m_renderer, gold.r, gold.g, gold.b, 255);
        SDL_RenderDrawRect(m_renderer, &badgeRect);
    }

    // Text content
    int textX = badgeX + DETAIL_BADGE + 16;
    int textW = m_w - MARGIN * 2 - DETAIL_BADGE - 16 - 120; // 120 for pts column

    // Title
    SDL_Color titleClr = unlocked ? pal.textPrimary : pal.textSecond;
    m_theme->drawText(entry.info.title, textX, badgeY, titleClr, FontSize::BODY);

    // Description (single wrapped line — clip to available width)
    SDL_Rect clip = { textX, badgeY + 26, textW, 52 };
    SDL_RenderSetClipRect(m_renderer, &clip);
    m_theme->drawTextWrapped(entry.info.description,
        textX, badgeY + 26, textW, pal.textSecond, FontSize::SMALL);
    SDL_RenderSetClipRect(m_renderer, nullptr);

    // Points — right-aligned in strip
    int ptsX = m_w - MARGIN - 110;
    if (unlocked) {
        SDL_Color gold = { 255, 210, 60, 255 };
        std::string ptsStr = std::to_string(entry.info.points) + " pts";
        m_theme->drawTextCentered(ptsStr, ptsX + 55, badgeY + 12, gold, FontSize::BODY);
        m_theme->drawTextCentered("UNLOCKED",
            ptsX + 55, badgeY + 38, SDL_Color{80,200,120,255}, FontSize::TINY);
    } else {
        m_theme->drawTextCentered(std::to_string(entry.info.points) + " pts",
            ptsX + 55, badgeY + 12, pal.textDisable, FontSize::BODY);
        m_theme->drawTextCentered("LOCKED",
            ptsX + 55, badgeY + 38, SDL_Color{160,100,100,255}, FontSize::TINY);
    }
}
