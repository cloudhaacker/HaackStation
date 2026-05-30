#include "omnisave_card_shelf.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <regex>
#include <map>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
OmniSaveCardShelf::OmniSaveCardShelf(SDL_Renderer* renderer,
                                     ThemeEngine*  theme,
                                     ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

OmniSaveCardShelf::~OmniSaveCardShelf()
{
    freeCoverTextures();
}

// ─────────────────────────────────────────────────────────────────────────────
//  open
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::open()
{
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    m_wantsClose = false;
    m_pendingOpen.reset();
    m_sel    = 0;
    m_scroll = 0;

    freeCoverTextures();
    m_entries.clear();

    scanSaveData();
    loadCoverTextures();

    std::cout << "[OmniSaveCardShelf] " << m_entries.size()
              << " games with save data\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  stripRegion — mirrors HaackApp::stripRomRegion exactly
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveCardShelf::stripRegion(const std::string& stem)
{
    static const std::regex tagPattern(R"(\s*[\(\[][^\)\]]*[\)\]])");
    std::string result = std::regex_replace(stem, tagPattern, "");
    auto start = result.find_first_not_of(" \t");
    auto end   = result.find_last_not_of(" \t");
    if (start == std::string::npos) return stem;
    return result.substr(start, end - start + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  countStateSlots — count .state files in saves/states/<folderName>/
// ─────────────────────────────────────────────────────────────────────────────
int OmniSaveCardShelf::countStateSlots(const std::string& folderName) const
{
    fs::path dir = fs::path("saves/states") / folderName;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return 0;
    int count = 0;
    for (const auto& f : fs::directory_iterator(dir)) {
        if (!f.is_regular_file()) continue;
        if (f.path().extension() != ".state") continue;
        // Exclude the undo snapshot
        if (f.path().stem().string().find(".undo") != std::string::npos) continue;
        ++count;
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  countCardSlots — count .mcr files in memcards/per_game/ for a serial
// ─────────────────────────────────────────────────────────────────────────────
int OmniSaveCardShelf::countCardSlots(const std::string& serial) const
{
    if (serial.empty()) return 0;
    fs::path dir = "memcards/per_game/";
    if (!fs::exists(dir)) return 0;
    int count = 0;
    for (const auto& f : fs::directory_iterator(dir)) {
        if (!f.is_regular_file()) continue;
        if (f.path().extension() != ".mcr") continue;
        // Match files starting with this serial e.g. SLUS-00553_1.mcr
        std::string stem = f.path().stem().string();
        if (stem.substr(0, serial.size()) == serial)
            ++count;
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  scanSaveData
//
//  Strategy:
//  1. If scanner library is available, use it as the primary source.
//     Each library entry gives us: ROM path (→ folderName), serial, title.
//     This handles multi-disc deduplication and clean titles automatically
//     because the scanner already did that work.
//  2. After processing the library, scan saves/states/ for any remaining
//     folders not already covered — these are orphaned saves for games that
//     have since been removed from the ROM library but still have data.
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::scanSaveData()
{
    std::set<std::string> coveredFolders;

    // ── Pass 1: scanner library ───────────────────────────────────────────
    if (m_library && !m_library->empty()) {
        for (const auto& ge : *m_library) {
            fs::path gamePath(ge.path);
            std::string folderName   = gamePath.stem().string();
            std::string displayTitle = ge.title.empty()
                                       ? stripRegion(folderName)
                                       : ge.title;

            int states = countStateSlots(folderName);
            int cards  = countCardSlots(ge.serial);

            if (states == 0 && cards == 0) continue;

            ShelfEntry e;
            e.title        = displayTitle;
            e.serial       = ge.serial;
            e.folderName   = folderName;
            e.stateCount   = states;
            e.cardCount    = cards;
            e.fromLibrary  = true;
            e.coverArtPath = findCoverArt(displayTitle, ge.serial);

            coveredFolders.insert(folderName);
            m_entries.push_back(std::move(e));
        }
    }

    // ── Pass 2: orphaned saves/states/ folders ────────────────────────────
    // Build stripped name set so old disc-named folders don't duplicate
    // entries already covered by an M3U library entry.
    std::set<std::string> libraryStripped;
    for (const auto& e : m_entries) {
        if (e.fromLibrary)
            libraryStripped.insert(stripRegion(e.folderName));
    }

    const fs::path statesRoot = "saves/states/";
    if (fs::exists(statesRoot) && fs::is_directory(statesRoot)) {
        for (const auto& gameDir : fs::directory_iterator(statesRoot)) {
            if (!gameDir.is_directory()) continue;
            std::string folderName = gameDir.path().filename().string();

            if (coveredFolders.count(folderName)) continue;
            if (libraryStripped.count(stripRegion(folderName))) continue;

            int states = countStateSlots(folderName);
            if (states == 0) continue;

            ShelfEntry e;
            e.title        = stripRegion(folderName);
            e.serial       = "";
            e.folderName   = folderName;
            e.stateCount   = states;
            e.cardCount    = 0;
            e.fromLibrary  = false;
            e.coverArtPath = findCoverArt(e.title, "");

            m_entries.push_back(std::move(e));
        }
    }

    // ── Sort alphabetically ignoring leading articles ─────────────────────
    auto sortKey = [](const std::string& s) -> std::string {
        std::string t = s;
        std::string lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        const char* articles[] = { "the ", "a ", "an " };
        for (auto* art : articles) {
            if (lower.size() >= strlen(art) &&
                lower.substr(0, strlen(art)) == art) {
                t = t.substr(strlen(art));
                break;
            }
        }
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        return t;
    };

    std::sort(m_entries.begin(), m_entries.end(),
        [&sortKey](const ShelfEntry& a, const ShelfEntry& b) {
            return sortKey(a.title) < sortKey(b.title);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
//  findCoverArt
// ─────────────────────────────────────────────────────────────────────────────
std::string OmniSaveCardShelf::findCoverArt(const std::string& title,
                                             const std::string& serial) const
{
    const std::vector<std::string> exts  = { ".png", ".jpg", ".jpeg" };
    std::vector<std::string>       stems = { title };
    if (!serial.empty()) stems.push_back(serial);

    for (const auto& stem : stems) {
        for (const auto& ext : exts) {
            std::string path = m_coverArtDir + stem + ext;
            if (fs::exists(path)) return path;
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
//  loadCoverTextures / freeCoverTextures
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::loadCoverTextures()
{
    for (auto& entry : m_entries) {
        if (entry.coverArtPath.empty()) continue;
        SDL_Surface* surf = IMG_Load(entry.coverArtPath.c_str());
        if (!surf) continue;
        entry.coverTex = SDL_CreateTextureFromSurface(m_renderer, surf);
        SDL_FreeSurface(surf);
    }
}

void OmniSaveCardShelf::freeCoverTextures()
{
    for (auto& entry : m_entries) {
        if (entry.coverTex) {
            SDL_DestroyTexture(entry.coverTex);
            entry.coverTex = nullptr;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleEvent
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::handleEvent(const SDL_Event& e)
{
    if (m_entries.empty()) {
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
            m_wantsClose = true;
        if (e.type == SDL_CONTROLLERBUTTONDOWN &&
            e.cbutton.button == SDL_CONTROLLER_BUTTON_B)
            m_wantsClose = true;
        return;
    }

    const int total = static_cast<int>(m_entries.size());

    auto moveLeft  = [&]() { if (m_sel > 0) --m_sel; };
    auto moveRight = [&]() { if (m_sel < total - 1) ++m_sel; };
    auto moveUp    = [&]() { if (m_sel >= GRID_COLS) m_sel -= GRID_COLS; };
    auto moveDown  = [&]() { if (m_sel + GRID_COLS < total) m_sel += GRID_COLS; };

    auto confirm = [&]() {
        if (m_sel >= 0 && m_sel < total) {
            m_pendingOpen = PendingOpen{
                m_entries[m_sel].title,
                m_entries[m_sel].serial,
                m_entries[m_sel].folderName
            };
            if (m_nav) m_nav->rumbleConfirm();
        }
    };
    auto cancel = [&]() { m_wantsClose = true; };

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_LEFT:   moveLeft();  break;
            case SDLK_RIGHT:  moveRight(); break;
            case SDLK_UP:     moveUp();    break;
            case SDLK_DOWN:   moveDown();  break;
            case SDLK_x:
            case SDLK_RETURN: confirm();   break;
            case SDLK_z:
            case SDLK_ESCAPE: cancel();    break;
            default: break;
        }
    }

    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  moveLeft();  break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moveRight(); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    moveUp();    break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  moveDown();  break;
            case SDL_CONTROLLER_BUTTON_A:          confirm();   break;
            case SDL_CONTROLLER_BUTTON_B:          cancel();    break;
            default: break;
        }
    }

    // Keep selected tile scrolled into view
    int tileW   = (m_w - GRID_MARGIN * 2 - GRID_GAP * (GRID_COLS - 1)) / GRID_COLS;
    int tileH   = tileW + META_H;
    int gridH   = m_h - HEADER_H - FOOTER_H;
    int visRows = std::max(1, gridH / (tileH + GRID_GAP));
    int selRow  = m_sel / GRID_COLS;

    if (selRow < m_scroll)            m_scroll = selRow;
    if (selRow >= m_scroll + visRows) m_scroll = selRow - visRows + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  update
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::update(float /*deltaMs*/) {}

// ─────────────────────────────────────────────────────────────────────────────
//  render
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::render()
{
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    const auto& pal = m_theme->palette();

    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_Rect screen = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &screen);

    renderHeader();
    renderGrid();
    renderFooter();
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderHeader
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::renderHeader()
{
    const auto& pal = m_theme->palette();

    SDL_Rect hdr = { 0, 0, m_w, HEADER_H };
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &hdr);

    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, HEADER_H - 1, m_w, HEADER_H - 1);

    m_theme->drawText("OmniSave", GRID_MARGIN, (HEADER_H - 28) / 2,
                      pal.textPrimary, FontSize::TITLE);

    if (!m_entries.empty()) {
        std::string sub = std::to_string(m_entries.size()) + " games with save data";
        int sw = 0, sh = 0;
        m_theme->measureText(sub, FontSize::SMALL, sw, sh);
        m_theme->drawText(sub, m_w - GRID_MARGIN - sw,
                          (HEADER_H - sh) / 2,
                          pal.textSecond, FontSize::SMALL);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderGrid
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::renderGrid()
{
    const auto& pal = m_theme->palette();

    const int gridW  = m_w - GRID_MARGIN * 2;
    const int tileW  = (gridW - GRID_GAP * (GRID_COLS - 1)) / GRID_COLS;
    const int tileH  = tileW + META_H;

    if (m_entries.empty()) {
        std::string msg = "No save data found.";
        int mw = 0, mh = 0;
        m_theme->measureText(msg, FontSize::BODY, mw, mh);
        m_theme->drawText(msg, (m_w - mw) / 2, m_h / 2,
                          pal.textSecond, FontSize::BODY);
        return;
    }

    const int total    = static_cast<int>(m_entries.size());
    const int gridH    = m_h - HEADER_H - FOOTER_H;
    const int visRows  = std::max(1, gridH / (tileH + GRID_GAP));
    const int firstIdx = m_scroll * GRID_COLS;
    const int lastIdx  = std::min(total, (m_scroll + visRows + 1) * GRID_COLS);

    SDL_Rect clip = { 0, HEADER_H, m_w, gridH };
    SDL_RenderSetClipRect(m_renderer, &clip);

    for (int i = firstIdx; i < lastIdx; ++i) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        int x   = GRID_MARGIN + col * (tileW + GRID_GAP);
        int y   = HEADER_H + GRID_GAP + (row - m_scroll) * (tileH + GRID_GAP);
        renderTile(m_entries[i], x, y, tileW, tileH, i == m_sel);
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);

    // Scroll dot indicator
    int totalRows = (total + GRID_COLS - 1) / GRID_COLS;
    if (totalRows > visRows) {
        int dotR  = 4;
        int dotGap = 14;
        int dotY  = m_h - FOOTER_H - 14;
        int dotTotalW = totalRows * dotR * 2 + (totalRows - 1) * (dotGap - dotR * 2);
        int dotX  = (m_w - dotTotalW) / 2;
        for (int r = 0; r < totalRows; ++r) {
            SDL_Color c = (r == m_scroll) ? pal.accent : pal.gridLine;
            SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, 255);
            SDL_Rect dot = { dotX + r * dotGap - dotR, dotY - dotR, dotR * 2, dotR * 2 };
            SDL_RenderFillRect(m_renderer, &dot);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderTile
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::renderTile(const ShelfEntry& entry,
                                    int x, int y, int w, int h,
                                    bool selected)
{
    const auto& pal  = m_theme->palette();
    const int coverH = w;
    const int metaY  = y + coverH;

    SDL_Color bg = selected ? pal.bgCardHover : pal.bgCard;
    SDL_Rect card = { x, y, w, h };
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(m_renderer, &card);

    // ── Cover ─────────────────────────────────────────────────────────────
    SDL_Rect coverRect = { x, y, w, coverH };
    if (entry.coverTex) {
        int texW = 0, texH = 0;
        SDL_QueryTexture(entry.coverTex, nullptr, nullptr, &texW, &texH);
        SDL_Rect dst = coverRect;
        if (texW > 0 && texH > 0) {
            float scale = std::min(
                static_cast<float>(coverRect.w) / texW,
                static_cast<float>(coverRect.h) / texH);
            int dstW = static_cast<int>(texW * scale);
            int dstH = static_cast<int>(texH * scale);
            dst.x = coverRect.x + (coverRect.w - dstW) / 2;
            dst.y = coverRect.y + (coverRect.h - dstH) / 2;
            dst.w = dstW;
            dst.h = dstH;
        }
        SDL_SetRenderDrawColor(m_renderer, bg.r / 2, bg.g / 2, bg.b / 2, 255);
        SDL_RenderFillRect(m_renderer, &coverRect);
        SDL_RenderCopy(m_renderer, entry.coverTex, nullptr, &dst);
    } else {
        renderPlaceholder(x, y, w, coverH);
    }

    // ── Selection border ──────────────────────────────────────────────────
    if (selected) {
        SDL_SetRenderDrawColor(m_renderer,
            pal.accent.r, pal.accent.g, pal.accent.b, 255);
        for (int t = 0; t < 2; ++t) {
            SDL_Rect border = { x - t, y - t, w + t * 2, h + t * 2 };
            SDL_RenderDrawRect(m_renderer, &border);
        }
    } else {
        SDL_SetRenderDrawColor(m_renderer,
            pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
        SDL_RenderDrawRect(m_renderer, &card);
    }

    // ── Title ─────────────────────────────────────────────────────────────
    m_theme->drawTextTruncated(entry.title, x + 4, metaY + 4,
                                w - 8, pal.textPrimary, FontSize::SMALL);

    // ── Badges ────────────────────────────────────────────────────────────
    int badgeY = metaY + META_H - 18;
    int badgeX = x + 4;

    SDL_Color badgeBg = selected ? pal.accentDim : pal.gridLine;
    SDL_Color badgeTx = selected ? pal.accent    : pal.textSecond;

    auto drawBadge = [&](const std::string& label) {
        int lw = 0, lh = 0;
        m_theme->measureText(label, FontSize::TINY, lw, lh);
        SDL_Rect br = { badgeX, badgeY - 2, lw + 8, lh + 4 };
        SDL_SetRenderDrawColor(m_renderer, badgeBg.r, badgeBg.g, badgeBg.b, 255);
        SDL_RenderFillRect(m_renderer, &br);
        m_theme->drawText(label, badgeX + 4, badgeY, badgeTx, FontSize::TINY);
        badgeX += lw + 8 + 4;
    };

    if (entry.stateCount > 0)
        drawBadge(std::to_string(entry.stateCount) +
                  (entry.stateCount == 1 ? " state" : " states"));
    if (entry.cardCount > 0)
        drawBadge(std::to_string(entry.cardCount) +
                  (entry.cardCount == 1 ? " card" : " cards"));

    // Orphan indicator — small dot so user knows this game isn't in the library
    if (!entry.fromLibrary) {
        SDL_SetRenderDrawColor(m_renderer,
            pal.textDisable.r, pal.textDisable.g, pal.textDisable.b, 255);
        SDL_Rect dot = { x + w - 10, metaY + 6, 5, 5 };
        SDL_RenderFillRect(m_renderer, &dot);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderPlaceholder
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::renderPlaceholder(int x, int y, int w, int h)
{
    const auto& pal = m_theme->palette();

    SDL_Rect bg = { x, y, w, h };
    SDL_SetRenderDrawColor(m_renderer, pal.bgCard.r, pal.bgCard.g, pal.bgCard.b, 255);
    SDL_RenderFillRect(m_renderer, &bg);

    int cx = x + w / 2;
    int cy = y + h / 2;
    int outerR = std::min(w, h) / 4;
    int innerR = outerR / 4;

    auto drawCircle = [&](int pcx, int pcy, int r, SDL_Color col) {
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, 255);
        int rx = r, ry = 0, err = 0;
        while (rx >= ry) {
            SDL_RenderDrawPoint(m_renderer, pcx + rx, pcy + ry);
            SDL_RenderDrawPoint(m_renderer, pcx + ry, pcy + rx);
            SDL_RenderDrawPoint(m_renderer, pcx - ry, pcy + rx);
            SDL_RenderDrawPoint(m_renderer, pcx - rx, pcy + ry);
            SDL_RenderDrawPoint(m_renderer, pcx - rx, pcy - ry);
            SDL_RenderDrawPoint(m_renderer, pcx - ry, pcy - rx);
            SDL_RenderDrawPoint(m_renderer, pcx + ry, pcy - rx);
            SDL_RenderDrawPoint(m_renderer, pcx + rx, pcy - ry);
            ++ry;
            if (err <= 0) err += 2 * ry + 1;
            if (err > 0)  { --rx; err -= 2 * rx + 1; }
        }
    };

    drawCircle(cx, cy, outerR,     pal.gridLine);
    drawCircle(cx, cy, outerR - 1, pal.gridLine);
    drawCircle(cx, cy, innerR,     pal.gridLine);

    std::string label = "no cover";
    int lw = 0, lh = 0;
    m_theme->measureText(label, FontSize::TINY, lw, lh);
    m_theme->drawText(label, cx - lw / 2, cy + outerR + 6,
                      pal.textDisable, FontSize::TINY);
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderFooter
// ─────────────────────────────────────────────────────────────────────────────
void OmniSaveCardShelf::renderFooter()
{
    const auto& pal = m_theme->palette();
    int footerY = m_h - FOOTER_H;

    SDL_Rect footer = { 0, footerY, m_w, FOOTER_H };
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &footer);

    SDL_SetRenderDrawColor(m_renderer, pal.gridLine.r, pal.gridLine.g, pal.gridLine.b, 255);
    SDL_RenderDrawLine(m_renderer, 0, footerY, m_w, footerY);

    int by = footerY + (FOOTER_H - 20) / 2;
    m_theme->drawButtonHint(GRID_MARGIN,       by, "X", "Open saves", pal.accent);
    m_theme->drawButtonHint(GRID_MARGIN + 140, by, "O", "Back",       pal.accent);
}
