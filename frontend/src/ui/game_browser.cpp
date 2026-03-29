#include "game_browser.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cmath>

// ─── Scroll & animation constants ─────────────────────────────────────────────
static constexpr float SCROLL_SPRING      = 12.f;
static constexpr float SCROLL_DAMP        = 0.75f;
static constexpr float SELECTION_ANIM_SPD = 8.f;
static constexpr float LAUNCH_ANIM_SPD   = 3.f;

GameBrowser::GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav), m_wantsDetails(false)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
    m_theme->layout().recalculate(m_windowW, m_windowH);
}

GameBrowser::~GameBrowser() {
    for (auto& [idx, tex] : m_coverArtCache)
        if (tex) SDL_DestroyTexture(tex);
    for (auto& [idx, tex] : m_topRowCoverCache)
        if (tex) SDL_DestroyTexture(tex);
}

void GameBrowser::setLibrary(const std::vector<GameEntry>& games) {
    m_games = games;
    m_selectedRow  = 0;
    m_selectedCol  = 0;
    m_inTopRow     = false;
    m_scrollOffset = 0.f;
    m_scrollTarget = 0.f;
    m_state = games.empty() ? BrowserState::EMPTY : BrowserState::BROWSING;
    std::cout << "[GameBrowser] Library set: " << games.size() << " games\n";
}

void GameBrowser::onWindowResize(int w, int h) {
    m_windowW = w;
    m_windowH = h;
    m_theme->layout().recalculate(w, h);
    m_theme->onWindowResize(w, h);
}

// ─── Top row helpers ──────────────────────────────────────────────────────────
bool GameBrowser::topRowHasContent() const {
    if (m_topRowMode == TopRowMode::NONE) return false;
    if (m_topRowMode == TopRowMode::RECENTLY_PLAYED) {
        return m_playHistory && !m_playHistory->entries().empty();
    }
    // FAVORITES: not yet implemented — hide until Phase 4
    return false;
}

int GameBrowser::topRowHeight() const {
    return topRowHasContent() ? TOP_ROW_H : 0;
}

int GameBrowser::topRowCardCount() const {
    // Fit as many cards as possible in one row, using the same width as the main grid
    const auto& lay = m_theme->layout();
    int gridW = lay.cardsPerRow * lay.cardW + (lay.cardsPerRow - 1) * lay.cardPadX;
    int cardW = TOP_ROW_CARD_H; // Square-ish cards in top row (height = width)
    if (cardW <= 0) return 1;
    return std::max(1, (gridW + TOP_ROW_PAD) / (cardW + TOP_ROW_PAD));
}

SDL_Texture* GameBrowser::topRowCoverArt(int topRowIndex) {
    auto it = m_topRowCoverCache.find(topRowIndex);
    if (it != m_topRowCoverCache.end()) return it->second;

    if (!m_playHistory) { m_topRowCoverCache[topRowIndex] = nullptr; return nullptr; }
    const auto& entries = m_playHistory->entries();
    if (topRowIndex >= (int)entries.size()) { m_topRowCoverCache[topRowIndex] = nullptr; return nullptr; }

    const auto& entry = entries[topRowIndex];

    // Try to find cover in the main library cache first
    for (int i = 0; i < (int)m_games.size(); i++) {
        if (m_games[i].path == entry.path) {
            SDL_Texture* tex = getCoverArt(i);
            m_topRowCoverCache[topRowIndex] = tex; // May be null — that's fine
            return tex;
        }
    }

    // Fallback: try media/covers/ directly
    auto safeName = entry.title;
    for (auto& c : safeName)
        if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||
            c=='"'||c=='<'||c=='>'||c=='|') c='_';

    std::vector<std::string> candidates = {
        "media/covers/" + safeName + ".png",
        "media/covers/" + safeName + ".jpg",
    };
    for (const auto& path : candidates) {
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
            SDL_FreeSurface(surf);
            if (tex) { m_topRowCoverCache[topRowIndex] = tex; return tex; }
        }
    }

    m_topRowCoverCache[topRowIndex] = nullptr;
    return nullptr;
}

// ─── Event handling ───────────────────────────────────────────────────────────
void GameBrowser::handleEvent(const SDL_Event& e) {
    if (m_state != BrowserState::BROWSING) return;

    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) moveSelection(action);

    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE) moveSelection(action);
}

void GameBrowser::moveSelection(NavAction action) {
    if (m_games.empty() && !topRowHasContent()) return;

    const auto& lay = m_theme->layout();
    int cols = lay.cardsPerRow;

    // ── Top row navigation ────────────────────────────────────────────────────
    if (m_inTopRow) {
        int maxTopRow = (int)m_playHistory->entries().size() - 1;
        maxTopRow = std::min(maxTopRow, topRowCardCount() - 1);

        switch (action) {
            case NavAction::LEFT:
                m_topRowSelected = std::max(0, m_topRowSelected - 1);
                break;
            case NavAction::RIGHT:
                m_topRowSelected = std::min(maxTopRow, m_topRowSelected + 1);
                break;
            case NavAction::DOWN:
                m_inTopRow    = false;
                m_selectedRow = 0;
                m_selectionAnim    = 0.f;
                m_selectionChanged = true;
                break;
            case NavAction::CONFIRM: {
                // Launch selected recently played game
                if (m_playHistory && m_topRowSelected < (int)m_playHistory->entries().size()) {
                    const auto& entry = m_playHistory->entries()[m_topRowSelected];
                    std::cout << "[GameBrowser] Recently Played launch: " << entry.title << "\n";
                    m_nav->rumbleConfirm();
                    m_launchPath    = entry.path;
                    m_pendingLaunch = true;
                    m_launchAnim    = 0.f;
                    m_state         = BrowserState::LAUNCHING;
                }
                break;
            }
            case NavAction::OPTIONS:
                // Open details for the recently played game
                // Find it in the main library and select it
                if (m_playHistory && m_topRowSelected < (int)m_playHistory->entries().size()) {
                    const auto& entry = m_playHistory->entries()[m_topRowSelected];
                    for (int i = 0; i < (int)m_games.size(); i++) {
                        if (m_games[i].path == entry.path) {
                            m_selectedRow = i / cols;
                            m_selectedCol = i % cols;
                            m_inTopRow    = false;
                            break;
                        }
                    }
                    m_wantsDetails = true;
                }
                break;
            default: break;
        }
        return;
    }

    // ── Main grid navigation ──────────────────────────────────────────────────
    int rows = totalRows();
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
            if (m_selectedCol >= cols || selectedIndex() >= (int)m_games.size()) {
                if (selectedIndex() >= (int)m_games.size())
                    m_selectedCol = prevCol;
            }
            break;

        case NavAction::UP:
            if (m_selectedRow > 0) {
                m_selectedRow--;
            } else if (topRowHasContent()) {
                // Move up into the top row
                m_inTopRow       = true;
                m_topRowSelected = std::min(m_selectedCol,
                    (int)m_playHistory->entries().size() - 1);
                m_topRowSelected = std::max(0, m_topRowSelected);
                m_selectionAnim    = 0.f;
                m_selectionChanged = true;
            }
            break;

        case NavAction::DOWN:
            if (m_selectedRow < rows - 1) {
                m_selectedRow++;
                int newIdx = selectedIndex();
                if (newIdx >= (int)m_games.size()) {
                    m_selectedCol = (int)m_games.size() - m_selectedRow * cols - 1;
                    m_selectedCol = std::max(0, m_selectedCol);
                }
            }
            break;

        case NavAction::SHOULDER_L:
            m_selectedRow = std::max(0, m_selectedRow - visibleRows());
            break;

        case NavAction::SHOULDER_R:
            m_selectedRow = std::min(rows - 1, m_selectedRow + visibleRows());
            {
                int newIdx = selectedIndex();
                if (newIdx >= (int)m_games.size()) {
                    m_selectedRow = ((int)m_games.size() - 1) / cols;
                    m_selectedCol = ((int)m_games.size() - 1) % cols;
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

    m_theme->drawHeader(m_windowW, m_windowH, "HaackStation", "v0.1",
                        (int)m_games.size());

    switch (m_state) {
        case BrowserState::SCANNING:
            renderScanningState();
            break;
        case BrowserState::EMPTY:
            renderEmptyState();
            break;
        case BrowserState::BROWSING:
        case BrowserState::LAUNCHING:
            // Top row first (behind main grid visually, above it positionally)
            if (topRowHasContent())
                renderTopRow();
            renderGrid();
            break;
    }

    m_theme->drawFooterHints(m_windowW, m_windowH, "Launch", "Options");

    if (totalRows() > visibleRows()) {
        m_theme->drawScrollBar(
            m_windowW - 12,
            m_theme->layout().shelfPadTop + topRowHeight(),
            m_windowH - m_theme->layout().shelfPadTop - topRowHeight()
                      - m_theme->layout().footerH - 8,
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

// ─── renderTopRow ─────────────────────────────────────────────────────────────
// Draws the Recently Played strip between the header and the main grid.
// Cards are smaller than the main grid, square-ish, same width as the grid.
// The currently selected card gets an accent border when m_inTopRow is true.
void GameBrowser::renderTopRow() {
    if (!m_playHistory) return;
    const auto& entries = m_playHistory->entries();
    if (entries.empty()) return;

    const auto& pal = m_theme->palette();
    const auto& lay = m_theme->layout();

    int labelY = lay.shelfPadTop + 4;
    int cardY  = labelY + TOP_ROW_LABEL_H;
    int cardW  = TOP_ROW_CARD_H; // Square cards
    int count  = std::min((int)entries.size(), topRowCardCount());

    // Section label
    std::string label = (m_topRowMode == TopRowMode::RECENTLY_PLAYED)
        ? "Recently Played" : "Favorites";
    m_theme->drawText(label, lay.shelfPadLeft, labelY,
        m_inTopRow ? pal.accent : pal.textSecond, FontSize::SMALL);

    // Cards
    for (int i = 0; i < count; i++) {
        int cardX = lay.shelfPadLeft + i * (cardW + TOP_ROW_PAD);
        bool selected = m_inTopRow && (i == m_topRowSelected);

        SDL_Rect cardRect = { cardX, cardY, cardW, TOP_ROW_CARD_H };

        // Card background
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_Color bg = selected ? pal.bgCardHover : pal.bgCard;
        SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 220);
        SDL_RenderFillRect(m_renderer, &cardRect);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

        // Cover art
        SDL_Texture* cover = topRowCoverArt(i);
        if (cover) {
            SDL_RenderCopy(m_renderer, cover, nullptr, &cardRect);
        } else {
            // Placeholder "?"
            m_theme->drawTextCentered("?",
                cardX + cardW / 2, cardY + TOP_ROW_CARD_H / 2 - 8,
                pal.textDisable, FontSize::BODY);
        }

        // Selection border
        if (selected) {
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 255);
            SDL_RenderDrawRect(m_renderer, &cardRect);
            // Second inner border for visibility
            SDL_Rect inner = { cardX + 1, cardY + 1, cardW - 2, TOP_ROW_CARD_H - 2 };
            SDL_RenderDrawRect(m_renderer, &inner);
        }

        // Title tooltip on selected card
        if (selected && i < (int)entries.size()) {
            std::string title = entries[i].title;
            if (title.size() > 20) title = title.substr(0, 17) + "...";
            m_theme->drawTextCentered(title,
                cardX + cardW / 2, cardY + TOP_ROW_CARD_H + 4,
                pal.accent, FontSize::TINY);
        }
    }

    // Divider between top row and main grid
    int dividerY = cardY + TOP_ROW_CARD_H + (m_inTopRow ? 20 : 8);
    m_theme->drawLine(lay.shelfPadLeft, dividerY,
                       m_windowW - 20, dividerY, pal.gridLine);
}

// ─── renderGrid ───────────────────────────────────────────────────────────────
void GameBrowser::renderGrid() {
    const auto& lay = m_theme->layout();
    int extraTop = topRowHeight();  // Push main grid down if top row is active

    for (int row = 0; row < totalRows(); row++) {
        for (int col = 0; col < lay.cardsPerRow; col++) {
            int idx = row * lay.cardsPerRow + col;
            if (idx >= (int)m_games.size()) break;

            SDL_Rect r = cardRect(row, col);
            r.y += extraTop; // Offset for top row

            if (r.y + r.h < lay.shelfPadTop + extraTop) continue;
            if (r.y > m_windowH - lay.footerH) continue;

            bool selected = !m_inTopRow && (row == m_selectedRow && col == m_selectedCol);
            const GameEntry& game = m_games[idx];

            m_theme->drawGameCard(
                r,
                game.title,
                selected,
                game.isMultiDisc,
                game.discCount,
                getCoverArt(idx),
                selected ? Ease::outQuad(m_selectionAnim) : 0.f
            );
        }
    }

    // Selected game info panel
    if (selectionValid() && !m_inTopRow) {
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
    m_theme->drawTextCentered("Press Start to open Settings and set your ROMs path",
        m_windowW / 2, cy + 70, pal.accent, FontSize::SMALL);
}

void GameBrowser::renderScanningState() {
    int cx = m_windowW / 2;
    int cy = m_windowH / 2;
    m_theme->drawLoadingSpinner(cx, cy - 20, m_spinnerAngle, m_theme->palette().accent);
    m_theme->drawTextCentered("Scanning for games...", cx, cy + 10,
                               m_theme->palette().textSecond, FontSize::BODY);
}

// ─── Card rect calculation ────────────────────────────────────────────────────
// Note: extraTop offset is applied in renderGrid, not here, so cardRect()
// stays pure and can be reused for hit testing etc.
SDL_Rect GameBrowser::cardRect(int row, int col) const {
    const auto& lay = m_theme->layout();
    int x   = lay.shelfPadLeft + col * (lay.cardW + lay.cardPadX);
    float rowF = (float)row - m_scrollOffset;
    int y   = lay.shelfPadTop + (int)(rowF * (lay.cardH + lay.cardPadY));
    return { x, y, lay.cardW, lay.cardH };
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
int GameBrowser::totalRows() const {
    if (m_games.empty()) return 0;
    int cols = m_theme->layout().cardsPerRow;
    return ((int)m_games.size() + cols - 1) / cols;
}

int GameBrowser::visibleRows() const {
    const auto& lay = m_theme->layout();
    int availH = m_windowH - lay.shelfPadTop - lay.footerH - topRowHeight();
    return std::max(1, availH / (lay.cardH + lay.cardPadY));
}

bool GameBrowser::selectionValid() const {
    int idx = selectedIndex();
    return idx >= 0 && idx < (int)m_games.size();
}

const GameEntry* GameBrowser::selectedGame() const {
    if (!selectionValid()) return nullptr;
    return &m_games[selectedIndex()];
}

int GameBrowser::selectedIndex() const {
    return m_selectedRow * m_theme->layout().cardsPerRow + m_selectedCol;
}

const GameEntry* GameBrowser::selectedGameEntry() const {
    if (m_inTopRow) {
        // When in top row, return the matching library entry for details panel
        if (!m_playHistory) return nullptr;
        const auto& entries = m_playHistory->entries();
        if (m_topRowSelected >= (int)entries.size()) return nullptr;
        const std::string& path = entries[m_topRowSelected].path;
        for (const auto& g : m_games)
            if (g.path == path) return &g;
        return nullptr;
    }
    return selectedGame();
}

std::string GameBrowser::consumeLaunchPath() {
    m_pendingLaunch = false;
    return m_launchPath;
}

void GameBrowser::clearCoverArtCache() {
    for (auto& [idx, tex] : m_coverArtCache)
        if (tex) SDL_DestroyTexture(tex);
    m_coverArtCache.clear();
    for (auto& [idx, tex] : m_topRowCoverCache)
        if (tex) SDL_DestroyTexture(tex);
    m_topRowCoverCache.clear();
}

void GameBrowser::resetAfterGame() {
    m_launchAnim    = 0.f;
    m_selectionAnim = 1.f;
    m_pendingLaunch = false;
    m_wantsDetails  = false;
    m_inTopRow      = false;
    if (m_games.empty())
        m_state = BrowserState::EMPTY;
    else
        m_state = BrowserState::BROWSING;
}

// ─── Cover art ────────────────────────────────────────────────────────────────
SDL_Texture* GameBrowser::getCoverArt(int gameIndex) {
    auto it = m_coverArtCache.find(gameIndex);
    if (it != m_coverArtCache.end()) return it->second;

    if (gameIndex < (int)m_games.size()) {
        const auto& game = m_games[gameIndex];

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
            size_t sl = game.path.find_last_of("/\\");
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
                    m_coverArtCache[gameIndex] = tex;
                    std::cout << "[GameBrowser] Cover loaded: " << path << "\n";
                    return tex;
                }
            }
        }
    }

    m_coverArtCache[gameIndex] = nullptr;
    return nullptr;
}
