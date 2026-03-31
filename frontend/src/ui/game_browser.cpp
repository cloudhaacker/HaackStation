#include "game_browser.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cmath>

static constexpr float SCROLL_SPRING      = 12.f;
static constexpr float SCROLL_DAMP        = 0.75f;
static constexpr float SELECTION_ANIM_SPD = 8.f;
static constexpr float LAUNCH_ANIM_SPD    = 3.f;

// Shelf names shown in the indicator bar
static const char* SHELF_NAMES[] = { "All Games", "Recently Played", "Favorites" };

GameBrowser::GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_wantsDetails(false)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
    m_theme->layout().recalculate(m_windowW, m_windowH);
}

GameBrowser::~GameBrowser() {
    for (auto& [idx, tex] : m_coverArtCache)
        if (tex) SDL_DestroyTexture(tex);
}

// ─── Library ──────────────────────────────────────────────────────────────────
void GameBrowser::setLibrary(const std::vector<GameEntry>& games) {
    m_allGames     = games;
    m_selectedRow  = 0;
    m_selectedCol  = 0;
    m_scrollOffset = 0.f;
    m_scrollTarget = 0.f;
    rebuildActiveList();
    std::cout << "[GameBrowser] Library set: " << games.size() << " games\n";
}

// ─── Rebuild active list ──────────────────────────────────────────────────────
// Called whenever the shelf changes or history updates.
// Populates m_activeGames and m_activeToAllIndex for the current shelf.
void GameBrowser::rebuildActiveList() {
    m_activeGames.clear();
    m_activeToAllIndex.clear();
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_scrollOffset = 0.f;
    m_scrollTarget = 0.f;

    switch (m_shelfMode) {
        case ShelfMode::ALL_GAMES:
            m_activeGames = m_allGames;
            m_activeToAllIndex.resize(m_allGames.size());
            for (int i = 0; i < (int)m_allGames.size(); i++)
                m_activeToAllIndex[i] = i;
            break;

        case ShelfMode::RECENTLY_PLAYED:
            if (m_playHistory) {
                for (const auto& entry : m_playHistory->entries()) {
                    // Find matching GameEntry in allGames
                    for (int i = 0; i < (int)m_allGames.size(); i++) {
                        if (m_allGames[i].path == entry.path) {
                            m_activeGames.push_back(m_allGames[i]);
                            m_activeToAllIndex.push_back(i);
                            break;
                        }
                    }
                }
            }
            break;

        case ShelfMode::FAVORITES:
            // Phase 4 — stub, empty for now
            break;
    }

    // Update browser state based on active list
    if (m_allGames.empty())
        m_state = BrowserState::SCANNING; // Still scanning or no roms
    else if (m_activeGames.empty())
        m_state = BrowserState::EMPTY;    // Shelf is empty (no history, no favorites)
    else
        m_state = BrowserState::BROWSING;

    std::cout << "[GameBrowser] Shelf '" << SHELF_NAMES[(int)m_shelfMode]
              << "': " << m_activeGames.size() << " games\n";
}

// ─── Shelf cycling ────────────────────────────────────────────────────────────
void GameBrowser::cycleShelf(int direction) {
    int next = ((int)m_shelfMode + direction + NUM_SHELVES) % NUM_SHELVES;
    m_shelfMode = static_cast<ShelfMode>(next);
    m_nav->rumbleConfirm();
    rebuildActiveList();
    std::cout << "[GameBrowser] Switched to shelf: "
              << SHELF_NAMES[(int)m_shelfMode] << "\n";
}

void GameBrowser::onWindowResize(int w, int h) {
    m_windowW = w;
    m_windowH = h;
    m_theme->layout().recalculate(w, h);
    m_theme->onWindowResize(w, h);
}

// ─── Event handling ───────────────────────────────────────────────────────────
void GameBrowser::handleEvent(const SDL_Event& e) {
    if (m_state == BrowserState::SCANNING) return;
    // Allow L1/R1 shelf cycling even from EMPTY state
    if (m_state == BrowserState::EMPTY) {
        NavAction action = m_nav->processEvent(e);
        if (action == NavAction::SHOULDER_L) cycleShelf(-1);
        if (action == NavAction::SHOULDER_R) cycleShelf(+1);
        return;
    }

    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) moveSelection(action);

    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE) moveSelection(action);
}

void GameBrowser::moveSelection(NavAction action) {
    const auto& lay = m_theme->layout();
    int cols = lay.cardsPerRow;

    // ── Shelf cycling — L1/R1 always available ────────────────────────────────
    if (action == NavAction::SHOULDER_L) { cycleShelf(-1); return; }
    if (action == NavAction::SHOULDER_R) { cycleShelf(+1); return; }

    if (m_activeGames.empty()) return;

    int rows    = totalRows();
    int prevRow = m_selectedRow;
    int prevCol = m_selectedCol;

    switch (action) {
        case NavAction::LEFT:
            m_selectedCol--;
            if (m_selectedCol < 0) {
                if (m_selectedRow > 0) {
                    m_selectedRow--;
                    m_selectedCol = cols - 1;
                } else {
                    m_selectedCol = 0;
                }
            }
            break;

        case NavAction::RIGHT:
            m_selectedCol++;
            if (m_selectedCol >= cols || selectedIndex() >= (int)m_activeGames.size())
                m_selectedCol = prevCol;
            break;

        case NavAction::UP:
            if (m_selectedRow > 0) m_selectedRow--;
            break;

        case NavAction::DOWN:
            if (m_selectedRow < rows - 1) {
                m_selectedRow++;
                int newIdx = selectedIndex();
                if (newIdx >= (int)m_activeGames.size()) {
                    m_selectedCol = (int)m_activeGames.size() - m_selectedRow * cols - 1;
                    m_selectedCol = std::max(0, m_selectedCol);
                }
            }
            break;

        case NavAction::CONFIRM: {
            const GameEntry* game = selectedGame();
            if (game) {
                std::cout << "[GameBrowser] Launching: " << game->title << "\n";
                m_nav->rumbleConfirm();
                m_launchPath    = game->path;
                m_pendingLaunch = true;
                m_launchAnim    = 0.f;
                m_state         = BrowserState::LAUNCHING;
            }
            break;
        }

        case NavAction::OPTIONS:
            m_wantsDetails = true;
            break;

        default: break;
    }

    if (m_selectedRow != prevRow || m_selectedCol != prevCol) {
        m_selectionAnim    = 0.f;
        m_selectionChanged = true;
        ensureSelectionVisible();
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void GameBrowser::update(float deltaMs) {
    float dt = deltaMs / 1000.f;

    float diff        = m_scrollTarget - m_scrollOffset;
    m_scrollVelocity += diff * SCROLL_SPRING * dt;
    m_scrollVelocity *= std::pow(SCROLL_DAMP, dt * 60.f);
    m_scrollOffset   += m_scrollVelocity * dt;

    if (std::abs(diff) < 0.001f && std::abs(m_scrollVelocity) < 0.001f) {
        m_scrollOffset   = m_scrollTarget;
        m_scrollVelocity = 0.f;
    }

    if (m_selectionAnim < 1.f) {
        m_selectionAnim += SELECTION_ANIM_SPD * dt;
        m_selectionAnim  = std::min(1.f, m_selectionAnim);
    }

    m_spinnerAngle += 3.f * dt;
    if (m_spinnerAngle > 2.f * 3.14159f) m_spinnerAngle -= 2.f * 3.14159f;

    if (m_state == BrowserState::LAUNCHING) {
        m_launchAnim += LAUNCH_ANIM_SPD * dt;
        if (m_launchAnim >= 1.f) m_launchAnim = 1.f;
    }
}

void GameBrowser::ensureSelectionVisible() {
    float visRows = (float)visibleRows();
    if ((float)m_selectedRow < m_scrollTarget)
        m_scrollTarget = (float)m_selectedRow;
    else if ((float)m_selectedRow >= m_scrollTarget + visRows - 1)
        m_scrollTarget = (float)m_selectedRow - visRows + 1.f;
    m_scrollTarget = std::max(0.f, m_scrollTarget);
}

// ─── Render ───────────────────────────────────────────────────────────────────
void GameBrowser::render() {
    SDL_GetRendererOutputSize(m_renderer, &m_windowW, &m_windowH);
    m_theme->layout().recalculate(m_windowW, m_windowH);

    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    // Header — always uses the full library count
    m_theme->drawHeader(m_windowW, m_windowH, "HaackStation", "v0.1",
                        (int)m_allGames.size());

    // Shelf indicator bar (shelf name + dots) — shown in all states
    renderShelfIndicator();

    switch (m_state) {
        case BrowserState::SCANNING:
            renderScanningState();
            break;
        case BrowserState::EMPTY:
            // Could be empty shelf (Recently Played with no history) or
            // truly empty library — check which
            if (m_allGames.empty())
                renderEmptyState();   // No games at all
            else
                renderEmptyShelf();   // Shelf is empty (no history/favorites yet)
            break;
        case BrowserState::BROWSING:
        case BrowserState::LAUNCHING:
            renderGrid();
            break;
    }

    // Footer — hint changes based on shelf availability
    m_theme->drawFooterHints(m_windowW, m_windowH, "Launch", "Options");

    if (totalRows() > visibleRows()) {
        m_theme->drawScrollBar(
            m_windowW - 12,
            m_theme->layout().shelfPadTop,
            m_windowH - m_theme->layout().shelfPadTop - m_theme->layout().footerH - 8,
            totalRows(), visibleRows(),
            (int)m_scrollOffset
        );
    }

    if (m_state == BrowserState::LAUNCHING) {
        Uint8 alpha = (Uint8)(m_launchAnim * 200.f);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, alpha);
        SDL_Rect full = { 0, 0, m_windowW, m_windowH };
        SDL_RenderFillRect(m_renderer, &full);
    }
}

// ─── renderShelfIndicator ─────────────────────────────────────────────────────
// Draws the shelf navigation bar just below the main header.
// Layout:   ◀  All Games  ●  ○  ○  ▶
// The shelf name is centered; dots show current position; arrows hint L1/R1.
void GameBrowser::renderShelfIndicator() {
    const auto& pal = m_theme->palette();
    const auto& lay = m_theme->layout();

    // Position: just below header, above the shelf content
    int barY = lay.shelfPadTop - 28;
    if (barY < 50) barY = 50; // Safety clamp

    int cx = m_windowW / 2;

    // Shelf name — centered, accent color
    const char* name = SHELF_NAMES[(int)m_shelfMode];
    m_theme->drawTextCentered(name, cx, barY, pal.accent, FontSize::SMALL);

    // Dot indicators
    int dotSize    = 6;
    int dotPad     = 10;
    int totalDotW  = NUM_SHELVES * dotSize + (NUM_SHELVES - 1) * dotPad;
    int dotStartX  = cx - totalDotW / 2;
    int dotY       = barY + 18;

    for (int i = 0; i < NUM_SHELVES; i++) {
        int dx = dotStartX + i * (dotSize + dotPad);
        SDL_Rect dot = { dx, dotY, dotSize, dotSize };
        if (i == (int)m_shelfMode) {
            // Active dot — filled accent
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderFillRect(m_renderer, &dot);
        } else {
            // Inactive dot — outline only
            SDL_SetRenderDrawColor(m_renderer,
                pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 180);
            SDL_RenderDrawRect(m_renderer, &dot);
        }
    }

    // L1 / R1 arrows at edges
    m_theme->drawText("< L1", lay.shelfPadLeft, barY, pal.textDisable, FontSize::TINY);
    int r1W = 0, r1H = 0;
    m_theme->measureText("R1 >", FontSize::TINY, r1W, r1H);
    m_theme->drawText("R1 >", m_windowW - lay.shelfPadLeft - r1W, barY,
                       pal.textDisable, FontSize::TINY);
}

// ─── renderGrid ───────────────────────────────────────────────────────────────
void GameBrowser::renderGrid() {
    const auto& lay = m_theme->layout();

    for (int row = 0; row < totalRows(); row++) {
        for (int col = 0; col < lay.cardsPerRow; col++) {
            int activeIdx = row * lay.cardsPerRow + col;
            if (activeIdx >= (int)m_activeGames.size()) break;

            SDL_Rect r = cardRect(row, col);
            if (r.y + r.h < lay.shelfPadTop) continue;
            if (r.y > m_windowH - lay.footerH) continue;

            bool selected = (row == m_selectedRow && col == m_selectedCol);
            const GameEntry& game = m_activeGames[activeIdx];

            m_theme->drawGameCard(
                r,
                game.title,
                selected,
                game.isMultiDisc,
                game.discCount,
                activeCoverArt(activeIdx),
                selected ? Ease::outQuad(m_selectionAnim) : 0.f
            );
        }
    }

    // Selected game title above footer
    if (selectionValid()) {
        const GameEntry& sel = *selectedGame();
        int lineCount = sel.isMultiDisc ? 2 : 1;
        int blockH    = lineCount * 24 + 8;
        int infoY     = m_windowH - lay.footerH - blockH - 8;

        m_theme->drawTextCentered(sel.title, m_windowW / 2, infoY,
                                   m_theme->palette().textPrimary, FontSize::BODY);
        if (sel.isMultiDisc) {
            std::string discInfo = std::to_string(sel.discCount) + " discs";
            m_theme->drawTextCentered(discInfo, m_windowW / 2, infoY + 24,
                                       m_theme->palette().multiDisc, FontSize::SMALL);
        }
    }
}

void GameBrowser::renderEmptyState() {
    const auto& pal = m_theme->palette();
    int cy = m_windowH / 2;
    m_theme->drawTextCentered("No games found",
        m_windowW / 2, cy - 40, pal.textSecond, FontSize::TITLE);
    m_theme->drawTextCentered("Add PS1 game files to your ROMs folder",
        m_windowW / 2, cy, pal.textDisable, FontSize::BODY);
    m_theme->drawTextCentered("Supported: ISO  BIN/CUE  CHD  M3U",
        m_windowW / 2, cy + 30, pal.textDisable, FontSize::SMALL);
    m_theme->drawTextCentered("Press Start / Enter to open Settings",
        m_windowW / 2, cy + 70, pal.accent, FontSize::SMALL);
}

// Shown when the active shelf is empty but there ARE games in the library
void GameBrowser::renderEmptyShelf() {
    const auto& pal = m_theme->palette();
    int cy = m_windowH / 2;

    if (m_shelfMode == ShelfMode::RECENTLY_PLAYED) {
        m_theme->drawTextCentered("No recently played games yet",
            m_windowW / 2, cy - 20, pal.textSecond, FontSize::BODY);
        m_theme->drawTextCentered("Launch a game to see it here",
            m_windowW / 2, cy + 10, pal.textDisable, FontSize::SMALL);
    } else { // FAVORITES
        m_theme->drawTextCentered("No favorites yet",
            m_windowW / 2, cy - 20, pal.textSecond, FontSize::BODY);
        m_theme->drawTextCentered("Coming in Phase 4!",
            m_windowW / 2, cy + 10, pal.textDisable, FontSize::SMALL);
    }
    m_theme->drawTextCentered("Use L1 / R1 to switch shelves",
        m_windowW / 2, cy + 44, pal.accent, FontSize::SMALL);
}

void GameBrowser::renderScanningState() {
    int cx = m_windowW / 2;
    int cy = m_windowH / 2;
    m_theme->drawLoadingSpinner(cx, cy - 20, m_spinnerAngle, m_theme->palette().accent);
    m_theme->drawTextCentered("Scanning for games...", cx, cy + 10,
                               m_theme->palette().textSecond, FontSize::BODY);
}

// ─── Card geometry ────────────────────────────────────────────────────────────
SDL_Rect GameBrowser::cardRect(int row, int col) const {
    const auto& lay = m_theme->layout();
    int x      = lay.shelfPadLeft + col * (lay.cardW + lay.cardPadX);
    float rowF = (float)row - m_scrollOffset;
    int y      = lay.shelfPadTop + (int)(rowF * (lay.cardH + lay.cardPadY));
    return { x, y, lay.cardW, lay.cardH };
}

int GameBrowser::totalRows() const {
    if (m_activeGames.empty()) return 0;
    int cols = m_theme->layout().cardsPerRow;
    return ((int)m_activeGames.size() + cols - 1) / cols;
}

int GameBrowser::visibleRows() const {
    const auto& lay = m_theme->layout();
    int availH = m_windowH - lay.shelfPadTop - lay.footerH;
    return std::max(1, availH / (lay.cardH + lay.cardPadY));
}

bool GameBrowser::selectionValid() const {
    int idx = selectedIndex();
    return idx >= 0 && idx < (int)m_activeGames.size();
}

const GameEntry* GameBrowser::selectedGame() const {
    if (!selectionValid()) return nullptr;
    return &m_activeGames[selectedIndex()];
}

int GameBrowser::selectedIndex() const {
    return m_selectedRow * m_theme->layout().cardsPerRow + m_selectedCol;
}

const GameEntry* GameBrowser::selectedGameEntry() const {
    return selectedGame();
}

std::string GameBrowser::consumeLaunchPath() {
    m_pendingLaunch = false;
    return m_launchPath;
}

void GameBrowser::resetAfterGame() {
    m_launchAnim    = 0.f;
    m_selectionAnim = 1.f;
    m_pendingLaunch = false;
    m_wantsDetails  = false;
    // Refresh active list in case history changed during play
    rebuildActiveList();
    if (m_allGames.empty())
        m_state = BrowserState::EMPTY;
    else if (m_activeGames.empty())
        m_state = BrowserState::EMPTY;
    else
        m_state = BrowserState::BROWSING;
}

// ─── Cover art ────────────────────────────────────────────────────────────────
// activeCoverArt maps through m_activeToAllIndex so the cache is always
// keyed by the allGames index — covers load once and are shared across shelves.
SDL_Texture* GameBrowser::activeCoverArt(int activeIndex) {
    if (activeIndex < 0 || activeIndex >= (int)m_activeToAllIndex.size())
        return nullptr;
    int allIdx = m_activeToAllIndex[activeIndex];
    return getCoverArt(allIdx);
}

SDL_Texture* GameBrowser::getCoverArt(int allGamesIndex) {
    auto it = m_coverArtCache.find(allGamesIndex);
    if (it != m_coverArtCache.end()) return it->second;

    if (allGamesIndex < 0 || allGamesIndex >= (int)m_allGames.size()) {
        m_coverArtCache[allGamesIndex] = nullptr;
        return nullptr;
    }

    const auto& game = m_allGames[allGamesIndex];
    std::vector<std::string> candidates;

    if (!game.coverArtPath.empty())
        candidates.push_back(game.coverArtPath);

    std::string safeName = game.title;
    for (auto& c : safeName)
        if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||
            c=='"'||c=='<'||c=='>'||c=='|') c='_';

    candidates.push_back("media/covers/" + safeName + ".png");
    candidates.push_back("media/covers/" + safeName + ".jpg");

    {
        size_t sl  = game.path.find_last_of("/\\");
        std::string stem = (sl != std::string::npos)
            ? game.path.substr(sl + 1) : game.path;
        size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        std::string safeStem = stem;
        for (auto& c : safeStem)
            if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||
                c=='"'||c=='<'||c=='>'||c=='|') c='_';
        if (safeStem != safeName) {
            candidates.push_back("media/covers/" + safeStem + ".png");
            candidates.push_back("media/covers/" + safeStem + ".jpg");
        }
    }

    size_t slash = game.path.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::string romDir = game.path.substr(0, slash + 1);
        candidates.push_back(romDir + "media/covers/" + safeName + ".png");
    }

    for (const auto& path : candidates) {
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                m_coverArtCache[allGamesIndex] = tex;
                return tex;
            }
        }
    }

    m_coverArtCache[allGamesIndex] = nullptr;
    return nullptr;
}

void GameBrowser::clearCoverArtCache() {
    for (auto& [idx, tex] : m_coverArtCache)
        if (tex) SDL_DestroyTexture(tex);
    m_coverArtCache.clear();
}

SDL_Texture* GameBrowser::getCoverArtForGame(const std::string& path) {
    for (int i = 0; i < (int)m_allGames.size(); i++) {
        if (m_allGames[i].path == path)
            return getCoverArt(i);
    }
    return nullptr;
}
