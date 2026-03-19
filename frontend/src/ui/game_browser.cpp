#include "game_browser.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// ─── Scroll & animation constants ─────────────────────────────────────────────
static constexpr float SCROLL_SPRING     = 12.f;   // Spring constant for scroll
static constexpr float SCROLL_DAMP       = 0.75f;  // Damping
static constexpr float SELECTION_ANIM_SPD= 8.f;    // Selection highlight fade speed
static constexpr float LAUNCH_ANIM_SPD   = 3.f;    // Launch flash speed

GameBrowser::GameBrowser(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_windowW, &m_windowH);
    m_theme->layout().recalculate(m_windowW, m_windowH);
}

GameBrowser::~GameBrowser() {
    for (auto& [idx, tex] : m_coverArtCache) {
        if (tex) SDL_DestroyTexture(tex);
    }
}

void GameBrowser::setLibrary(const std::vector<GameEntry>& games) {
    m_games = games;
    m_selectedRow = 0;
    m_selectedCol = 0;
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

// ─── Event handling ───────────────────────────────────────────────────────────
void GameBrowser::handleEvent(const SDL_Event& e) {
    if (m_state != BrowserState::BROWSING) return;

    NavAction action = m_nav->processEvent(e);
    if (action != NavAction::NONE) {
        moveSelection(action);
    }

    // Also handle repeat for held directions
    action = m_nav->updateHeld(SDL_GetTicks());
    if (action != NavAction::NONE) {
        moveSelection(action);
    }
}

void GameBrowser::moveSelection(NavAction action) {
    if (m_games.empty()) return;

    const auto& lay = m_theme->layout();
    int cols = lay.cardsPerRow;
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
                    m_selectedCol = 0;  // Clamp at start
                }
            }
            break;

        case NavAction::RIGHT:
            m_selectedCol++;
            if (m_selectedCol >= cols || selectedIndex() >= (int)m_games.size()) {
                // Wrap to next row if we'd go off the end of a non-full last row
                if (selectedIndex() >= (int)m_games.size()) {
                    m_selectedCol = prevCol;  // Don't go past last game
                }
            }
            break;

        case NavAction::UP:
            if (m_selectedRow > 0) m_selectedRow--;
            break;

        case NavAction::DOWN:
            if (m_selectedRow < rows - 1) {
                m_selectedRow++;
                // Clamp col if new row is shorter (last row may not be full)
                int newIdx = selectedIndex();
                if (newIdx >= (int)m_games.size()) {
                    m_selectedCol = (int)m_games.size() - m_selectedRow * cols - 1;
                    m_selectedCol = std::max(0, m_selectedCol);
                }
            }
            break;

        case NavAction::SHOULDER_L:
            // Jump up by visible rows
            m_selectedRow = std::max(0, m_selectedRow - visibleRows());
            break;

        case NavAction::SHOULDER_R:
            // Jump down by visible rows
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
            // Per-game options — will be handled by menu system in a later phase
            std::cout << "[GameBrowser] Options for: ";
            if (const GameEntry* g = selectedGame()) std::cout << g->title;
            std::cout << "\n";
            break;

        default:
            break;
    }

    // If selection moved, trigger animation reset
    if (m_selectedRow != prevRow || m_selectedCol != prevCol) {
        m_selectionAnim    = 0.f;
        m_selectionChanged = true;
        ensureSelectionVisible();
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void GameBrowser::update(float deltaMs) {
    float dt = deltaMs / 1000.f;

    // Smooth scroll spring
    float diff       = m_scrollTarget - m_scrollOffset;
    m_scrollVelocity += diff * SCROLL_SPRING * dt;
    m_scrollVelocity *= std::pow(SCROLL_DAMP, dt * 60.f);
    m_scrollOffset   += m_scrollVelocity * dt;

    // Snap when very close
    if (std::abs(diff) < 0.001f && std::abs(m_scrollVelocity) < 0.001f) {
        m_scrollOffset   = m_scrollTarget;
        m_scrollVelocity = 0.f;
    }

    // Selection animation (ease in when selection changes)
    if (m_selectionAnim < 1.f) {
        m_selectionAnim += SELECTION_ANIM_SPD * dt;
        m_selectionAnim  = std::min(1.f, m_selectionAnim);
    }

    // Spinner animation
    m_spinnerAngle += 3.f * dt;
    if (m_spinnerAngle > 2.f * 3.14159f) m_spinnerAngle -= 2.f * 3.14159f;

    // Launch animation
    if (m_state == BrowserState::LAUNCHING) {
        m_launchAnim += LAUNCH_ANIM_SPD * dt;
        if (m_launchAnim >= 1.f) {
            m_launchAnim = 1.f;
            // pendingLaunch stays true — app.cpp will pick it up
        }
    }
}

// ─── Ensure selected card is visible ─────────────────────────────────────────
void GameBrowser::ensureSelectionVisible() {
    float rowH = m_theme->layout().cardH + m_theme->layout().cardPadY;
    float visRows = (float)visibleRows();

    // If selected row is above scroll
    if ((float)m_selectedRow < m_scrollTarget) {
        m_scrollTarget = (float)m_selectedRow;
    }
    // If selected row is below visible area
    else if ((float)m_selectedRow >= m_scrollTarget + visRows - 1) {
        m_scrollTarget = (float)m_selectedRow - visRows + 1.f;
    }
    m_scrollTarget = std::max(0.f, m_scrollTarget);
}

// ─── Render ───────────────────────────────────────────────────────────────────
void GameBrowser::render() {
    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    // Header
    m_theme->drawHeader(m_windowW, m_windowH,
                        "HaackStation", "v0.1",
                        (int)m_games.size());

    // Main content
    switch (m_state) {
        case BrowserState::SCANNING:
            renderScanningState();
            break;
        case BrowserState::EMPTY:
            renderEmptyState();
            break;
        case BrowserState::BROWSING:
        case BrowserState::LAUNCHING:
            renderGrid();
            break;
    }

    // Footer hints
    m_theme->drawFooterHints(m_windowW, m_windowH, "Launch", "Options");

    // Scrollbar (right edge)
    if (totalRows() > visibleRows()) {
        m_theme->drawScrollBar(
            m_windowW - 12,
            m_theme->layout().shelfPadTop,
            m_windowH - m_theme->layout().shelfPadTop - m_theme->layout().footerH - 8,
            totalRows(), visibleRows(),
            (int)m_scrollOffset
        );
    }

    // Launch flash overlay
    if (m_state == BrowserState::LAUNCHING) {
        Uint8 alpha = (Uint8)(m_launchAnim * 200.f);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, alpha);
        SDL_Rect full = { 0, 0, m_windowW, m_windowH };
        SDL_RenderFillRect(m_renderer, &full);
    }
}

void GameBrowser::renderGrid() {
    const auto& lay = m_theme->layout();
    int visibleRowCount = visibleRows() + 2;  // +2 buffer rows for smooth scroll

    for (int row = 0; row < totalRows(); row++) {
        for (int col = 0; col < lay.cardsPerRow; col++) {
            int idx = row * lay.cardsPerRow + col;
            if (idx >= (int)m_games.size()) break;

            SDL_Rect r = cardRect(row, col);

            // Skip cards that are completely off-screen
            if (r.y + r.h < m_theme->layout().shelfPadTop) continue;
            if (r.y > m_windowH - m_theme->layout().footerH)  continue;

            bool selected = (row == m_selectedRow && col == m_selectedCol);
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

    // Selected game info panel (below grid, above footer) — shows full title
    if (selectionValid()) {
        const GameEntry& sel = *selectedGame();
        int infoY = m_windowH - m_theme->layout().footerH - 34;
        m_theme->drawTextCentered(sel.title, m_windowW / 2, infoY,
                                   m_theme->palette().textPrimary, FontSize::BODY);
        if (sel.isMultiDisc) {
            std::string discInfo = std::to_string(sel.discCount) + " discs";
            m_theme->drawTextCentered(discInfo, m_windowW / 2, infoY + 22,
                                       m_theme->palette().multiDisc, FontSize::SMALL);
        }
    }
}

void GameBrowser::renderEmptyState() {
    const auto& pal = m_theme->palette();
    int cy = m_windowH / 2;

    m_theme->drawTextCentered("No games found",
                               m_windowW / 2, cy - 40,
                               pal.textSecond, FontSize::TITLE);

    m_theme->drawTextCentered("Add PS1 game files to your ROMs folder",
                               m_windowW / 2, cy,
                               pal.textDisable, FontSize::BODY);

    m_theme->drawTextCentered("Supported: ISO  BIN/CUE  CHD  M3U",
                               m_windowW / 2, cy + 30,
                               pal.textDisable, FontSize::SMALL);

    // Settings hint
    m_theme->drawTextCentered("Press Start to open Settings and set your ROMs path",
                               m_windowW / 2, cy + 70,
                               pal.accent, FontSize::SMALL);
}

void GameBrowser::renderScanningState() {
    int cx = m_windowW / 2;
    int cy = m_windowH / 2;

    m_theme->drawLoadingSpinner(cx, cy - 20, m_spinnerAngle, m_theme->palette().accent);
    m_theme->drawTextCentered("Scanning for games...", cx, cy + 10,
                               m_theme->palette().textSecond, FontSize::BODY);
}

// ─── Card rect calculation ────────────────────────────────────────────────────
SDL_Rect GameBrowser::cardRect(int row, int col) const {
    const auto& lay = m_theme->layout();
    int x = lay.shelfPadLeft + col * (lay.cardW + lay.cardPadX);
    float rowF = (float)row - m_scrollOffset;
    int y = lay.shelfPadTop + (int)(rowF * (lay.cardH + lay.cardPadY));
    return { x, y, lay.cardW, lay.cardH };
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
int GameBrowser::totalRows() const {
    if (m_games.empty()) return 0;
    int cols = m_theme->layout().cardsPerRow;
    return ((int)m_games.size() + cols - 1) / cols;
}

int GameBrowser::visibleRows() const {
    const auto& lay = m_theme->layout();
    int availH = m_windowH - lay.shelfPadTop - lay.footerH;
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

std::string GameBrowser::consumeLaunchPath() {
    m_pendingLaunch = false;
    return m_launchPath;
}

// ─── Cover art ────────────────────────────────────────────────────────────────
SDL_Texture* GameBrowser::getCoverArt(int gameIndex) {
    auto it = m_coverArtCache.find(gameIndex);
    if (it != m_coverArtCache.end()) return it->second;

    // If the GameEntry has a cover art path, try to load it
    if (gameIndex < (int)m_games.size()) {
        const auto& game = m_games[gameIndex];
        if (!game.coverArtPath.empty()) {
            SDL_Surface* surf = SDL_LoadBMP(game.coverArtPath.c_str());
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    m_coverArtCache[gameIndex] = tex;
                    return tex;
                }
            }
        }
    }

    // No cover art available — cache nullptr so we don't retry
    m_coverArtCache[gameIndex] = nullptr;
    return nullptr;
}
