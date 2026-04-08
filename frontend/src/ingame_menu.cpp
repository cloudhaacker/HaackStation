#include "ingame_menu.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace fs = std::filesystem;

InGameMenu::InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
                        ControllerNav* nav, SaveStateManager* saveStates)
    : m_renderer(renderer), m_theme(theme)
    , m_nav(nav), m_saveStates(saveStates)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);

    rebuildMenuItems();
}

void InGameMenu::rebuildMenuItems() {
    m_items.clear();
    m_items.push_back({ "Resume",        "Return to game",         InGameMenuAction::RESUME        });
    m_items.push_back({ "Save State",    "Save current progress",  InGameMenuAction::SAVE_STATE    });
    m_items.push_back({ "Load State",    "Load a saved state",     InGameMenuAction::LOAD_STATE    });
    if (!m_discPaths.empty())
        m_items.push_back({ "Change Disc",   "Switch to another disc", InGameMenuAction::CHANGE_DISC   });
    m_items.push_back({ "Quit to Shelf", "Return to game library", InGameMenuAction::QUIT_TO_SHELF });
}

void InGameMenu::setDiscInfo(const std::vector<std::string>& discPaths,
                              int currentDisc) {
    m_discPaths      = discPaths;
    m_currentDisc    = currentDisc;
    m_highlightedDisc= currentDisc;
    m_pendingDiscIndex = currentDisc;
    rebuildMenuItems();
    std::cout << "[InGameMenu] Disc info set: " << discPaths.size()
              << " discs, current=" << currentDisc << "\n";
}

void InGameMenu::clearDiscInfo() {
    m_discPaths.clear();
    m_currentDisc     = 0;
    m_highlightedDisc = 0;
    m_pendingDiscIndex= 0;
    rebuildMenuItems();
}

void InGameMenu::open() {
    m_open            = true;
    m_section         = InGameMenuSection::MAIN;
    m_selectedItem    = 0;
    m_openAnim        = 1.f;
    m_pendingAction   = InGameMenuAction::NONE;
    m_highlightedDisc = m_currentDisc; // reset highlight to current disc
    std::cout << "[InGameMenu] Opened\n";
}

void InGameMenu::close() {
    m_open = false;
    m_pendingAction = InGameMenuAction::NONE;
    freeThumbnails();
}

void InGameMenu::freeThumbnails() {
    for (auto* t : m_thumbTextures)
        if (t) SDL_DestroyTexture(t);
    m_thumbTextures.clear();
}

void InGameMenu::loadThumbnails() {
    freeThumbnails();
    if (!m_saveStates) return;

    // Get existing slots
    m_slots = m_saveStates->listSlots();

    // Always show at least SLOTS_PER_PAGE slots so user can save to new ones
    // Fill up to SLOTS_PER_PAGE with empty placeholder slots
    int existingCount = (int)m_slots.size();
    for (int i = existingCount; i < SLOTS_PER_PAGE; i++) {
        SaveSlot empty;
        empty.slotNumber = i - 1; // -1=auto,0,1,2,3,4...
        empty.exists     = false;
        empty.timestamp  = (i == 0) ? "Auto Save" :
                           "Slot " + std::to_string(i);
        m_slots.push_back(empty);
    }

    // Load thumbnails for existing slots
    m_thumbTextures.resize(m_slots.size(), nullptr);
    for (size_t i = 0; i < m_slots.size(); i++) {
        if (m_slots[i].exists)
            m_thumbTextures[i] = m_saveStates->loadThumbnail(m_slots[i]);
    }
}

// ─── Events ───────────────────────────────────────────────────────────────────
void InGameMenu::handleEvent(const SDL_Event& e) {
    if (!m_open) return;

    NavAction action = m_nav->processEvent(e);
    if (action == NavAction::NONE) return;

    switch (m_section) {
        case InGameMenuSection::MAIN:        navigateMain(action);         break;
        case InGameMenuSection::SAVE_STATES:
        case InGameMenuSection::LOAD_STATES: navigateSaveStates(action);   break;
        case InGameMenuSection::DISC_SELECT: navigateDiscSelect(action);   break;
    }
}

void InGameMenu::navigateMain(NavAction action) {
    switch (action) {
        case NavAction::UP:
            m_selectedItem = std::max(0, m_selectedItem - 1);
            break;
        case NavAction::DOWN:
            m_selectedItem = std::min((int)m_items.size()-1, m_selectedItem+1);
            break;
        case NavAction::CONFIRM: {
            auto chosen = m_items[m_selectedItem].action;
            m_nav->rumbleConfirm();
            if (chosen == InGameMenuAction::SAVE_STATE) {
                m_section      = InGameMenuSection::SAVE_STATES;
                m_selectedSlot = 0;
                loadThumbnails();
            } else if (chosen == InGameMenuAction::LOAD_STATE) {
                m_section      = InGameMenuSection::LOAD_STATES;
                m_selectedSlot = 0;
                loadThumbnails();
            } else if (chosen == InGameMenuAction::CHANGE_DISC) {
                m_section         = InGameMenuSection::DISC_SELECT;
                m_highlightedDisc = m_currentDisc;
            } else {
                // RESUME or QUIT_TO_SHELF
                m_pendingAction = chosen;
            }
            break;
        }
        case NavAction::BACK:
        case NavAction::MENU:
            m_pendingAction = InGameMenuAction::RESUME;
            break;
        default: break;
    }
}

void InGameMenu::navigateDiscSelect(NavAction action) {
    int total = (int)m_discPaths.size();
    if (total == 0) { m_section = InGameMenuSection::MAIN; return; }

    switch (action) {
        case NavAction::LEFT:
        case NavAction::SHOULDER_L:
            m_highlightedDisc = (m_highlightedDisc - 1 + total) % total;
            m_nav->rumbleConfirm();
            break;
        case NavAction::RIGHT:
        case NavAction::SHOULDER_R:
            m_highlightedDisc = (m_highlightedDisc + 1) % total;
            m_nav->rumbleConfirm();
            break;
        case NavAction::CONFIRM:
            m_pendingDiscIndex = m_highlightedDisc;
            m_pendingAction    = InGameMenuAction::CHANGE_DISC;
            m_nav->rumbleConfirm();
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_section = InGameMenuSection::MAIN;
            break;
        default: break;
    }
}

void InGameMenu::navigateSaveStates(NavAction action) {
    int total = (int)m_slots.size();
    if (total == 0) {
        if (action == NavAction::BACK) {
            m_section = InGameMenuSection::MAIN;
            freeThumbnails();
        }
        return;
    }

    switch (action) {
        case NavAction::LEFT:
            m_selectedSlot = std::max(0, m_selectedSlot - 1);
            break;
        case NavAction::RIGHT:
            m_selectedSlot = std::min(total - 1, m_selectedSlot + 1);
            break;
        case NavAction::UP:
            m_selectedSlot = std::max(0, m_selectedSlot - SLOT_COLS);
            break;
        case NavAction::DOWN:
            m_selectedSlot = std::min(total - 1, m_selectedSlot + SLOT_COLS);
            break;
        case NavAction::CONFIRM:
            if (m_section == InGameMenuSection::SAVE_STATES) {
                // Can always save to any slot
                m_pendingAction = InGameMenuAction::SAVE_STATE;
                m_nav->rumbleConfirm();
            } else {
                // Can only load if slot has data
                if (m_selectedSlot < (int)m_slots.size() &&
                    m_slots[m_selectedSlot].exists) {
                    m_pendingAction = InGameMenuAction::LOAD_STATE;
                    m_nav->rumbleConfirm();
                }
            }
            break;
        case NavAction::BACK:
            m_section = InGameMenuSection::MAIN;
            freeThumbnails();
            break;
        default: break;
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void InGameMenu::update(float deltaMs) {
    if (!m_open) return;
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    m_spinAngle += 2.f * (deltaMs / 1000.f);

    // updateHeld fires every frame so d-pad/stick hold works in the menu
    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held != NavAction::NONE) {
        switch (m_section) {
            case InGameMenuSection::MAIN:        navigateMain(held);        break;
            case InGameMenuSection::SAVE_STATES:
            case InGameMenuSection::LOAD_STATES: navigateSaveStates(held);  break;
            case InGameMenuSection::DISC_SELECT: navigateDiscSelect(held);  break;
        }
    }
}

// ─── Render ───────────────────────────────────────────────────────────────────
void InGameMenu::render(SDL_Texture*) {
    if (!m_open) return;
    const auto& pal = m_theme->palette();

    // Full-screen dim overlay
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
    SDL_Rect full = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &full);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    switch (m_section) {
        case InGameMenuSection::MAIN:        renderMain();              break;
        case InGameMenuSection::SAVE_STATES: renderSaveStates(true);   break;
        case InGameMenuSection::LOAD_STATES: renderSaveStates(false);  break;
        case InGameMenuSection::DISC_SELECT: renderDiscSelect();        break;
    }
}

void InGameMenu::renderMain() {
    const auto& pal = m_theme->palette();

    // Panel centered on right third of screen
    int panelW = std::min(MENU_W, m_w - 80);
    int itemH  = 70;
    int panelH = 80 + (int)m_items.size() * itemH + 40;
    int panelX = m_w - panelW - 60;
    int panelY = (m_h - panelH) / 2;

    // Clamp to screen
    if (panelX < 20) panelX = 20;
    if (panelY < 20) panelY = 20;

    // Background
    SDL_Rect panel = { panelX, panelY, panelW, panelH };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g,
                            pal.bgPanel.b, 240);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Accent left border
    SDL_Rect border = { panelX, panelY, 4, panelH };
    m_theme->drawRect(border, pal.accent);

    // Title
    m_theme->drawText("HAACKSTATION",
        panelX + 16, panelY + 16, pal.accent, FontSize::BODY);
    m_theme->drawText("In-Game Menu",
        panelX + 16, panelY + 40, pal.textSecond, FontSize::SMALL);

    // Divider
    m_theme->drawLine(panelX + 16, panelY + 64,
                       panelX + panelW - 16, panelY + 64, pal.gridLine);

    // Menu items
    int itemY = panelY + 76;
    for (int i = 0; i < (int)m_items.size(); i++) {
        bool sel = (i == m_selectedItem);
        const auto& item = m_items[i];

        if (sel) {
            SDL_Rect hi = { panelX + 4, itemY - 2, panelW - 8, itemH - 4 };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                pal.accent.r, pal.accent.g, pal.accent.b, 50);
            SDL_RenderFillRect(m_renderer, &hi);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            SDL_Rect ind = { panelX + 4, itemY - 2, 4, itemH - 4 };
            m_theme->drawRect(ind, pal.accent);
        }

        m_theme->drawText(item.label,
            panelX + 20, itemY + 8,
            sel ? pal.textPrimary : pal.textSecond, FontSize::BODY);
        m_theme->drawText(item.hint,
            panelX + 20, itemY + 32,
            sel ? pal.accent : pal.textDisable, FontSize::TINY);

        itemY += itemH;
    }

    // Footer hints
    m_theme->drawFooterHints(m_w, m_h, "Select", "Resume");
}

void InGameMenu::renderSaveStates(bool isSaving) {
    const auto& pal = m_theme->palette();
    const char* title = isSaving ? "Save State" : "Load State";
    m_theme->drawTextCentered(title, m_w / 2, 40, pal.accent, FontSize::TITLE);

    if (m_slots.empty()) {
        m_theme->drawTextCentered("No save states yet",
            m_w / 2, m_h / 2, pal.textSecond, FontSize::BODY);
        m_theme->drawFooterHints(m_w, m_h, "", "Back");
        return;
    }

    // 3-column grid, card size based on screen width
    int cols   = SLOT_COLS;
    int cardW  = (m_w - 120) / cols - 16;
    int cardH  = (int)(cardW * 0.65f) + 40;
    int padX   = 16;
    int gridW  = cols * cardW + (cols - 1) * padX;
    int startX = (m_w - gridW) / 2;
    int startY = 90;

    for (int i = 0; i < (int)m_slots.size() && i < SLOTS_PER_PAGE; i++) {
        int col = i % cols;
        int row = i / cols;
        int x   = startX + col * (cardW + padX);
        int y   = startY + row * (cardH + 12);
        renderSlotCard(m_slots[i], x, y, cardW, cardH, i == m_selectedSlot);
    }

    m_theme->drawFooterHints(m_w, m_h,
        isSaving ? "Save here" : "Load", "Back");
}

void InGameMenu::renderSlotCard(const SaveSlot& slot, int x, int y,
                                 int w, int h, bool selected) {
    const auto& pal = m_theme->palette();
    SDL_Rect card = { x, y, w, h };

    // Card background
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer,
        selected ? pal.bgCardHover.r : pal.bgCard.r,
        selected ? pal.bgCardHover.g : pal.bgCard.g,
        selected ? pal.bgCardHover.b : pal.bgCard.b, 220);
    SDL_RenderFillRect(m_renderer, &card);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    if (selected) {
        SDL_SetRenderDrawColor(m_renderer,
            pal.accent.r, pal.accent.g, pal.accent.b, 255);
        SDL_RenderDrawRect(m_renderer, &card);
    }

    int thumbH = h - 36;

    if (slot.exists) {
        // Find thumbnail
        SDL_Texture* thumb = nullptr;
        for (int i = 0; i < (int)m_slots.size(); i++) {
            if (m_slots[i].slotNumber == slot.slotNumber &&
                i < (int)m_thumbTextures.size()) {
                thumb = m_thumbTextures[i];
                break;
            }
        }

        if (thumb) {
            SDL_Rect tr = { x + 2, y + 2, w - 4, thumbH - 4 };
            SDL_RenderCopy(m_renderer, thumb, nullptr, &tr);
        } else {
            SDL_Rect ph = { x + 2, y + 2, w - 4, thumbH - 4 };
            m_theme->drawRect(ph, pal.bgPanel);
            m_theme->drawTextCentered("No Preview",
                x + w/2, y + thumbH/2, pal.textDisable, FontSize::TINY);
        }

        m_theme->drawText(slot.timestamp,
            x + 6, y + thumbH + 4, pal.textPrimary, FontSize::TINY);
    } else {
        SDL_Rect ph = { x + 2, y + 2, w - 4, thumbH - 4 };
        m_theme->drawRect(ph, pal.bgPanel);

        std::string label = slot.slotNumber < 0 ? "Auto Save" :
                            "Slot " + std::to_string(slot.slotNumber + 1);
        m_theme->drawTextCentered(label,
            x + w/2, y + h/2 - 8, pal.textDisable, FontSize::SMALL);
        m_theme->drawTextCentered("Empty",
            x + w/2, y + h/2 + 14, pal.textDisable, FontSize::TINY);
    }
}

// ─── renderDiscSelect ─────────────────────────────────────────────────────────
// Stacked disc UI matching the concept design:
//   • Cover art panel — far left, with game title and disc count
//   • Stacked disc graphics — centre, back discs peeking behind, selected lifted
//   • L1/R1 hint badges flanking the stack
//   • Footer: A=launch  B=back
//
// Disc numbering is 1-based in the UI, 0-based internally.
void InGameMenu::renderDiscSelect() {
    const auto& pal = m_theme->palette();
    int total = (int)m_discPaths.size();
    if (total == 0) return;

    // ── Full-screen dim already applied by render() ────────────────────────────

    // ── Header ────────────────────────────────────────────────────────────────
    m_theme->drawTextCentered("disc select",
        m_w / 2, 28, pal.textSecond, FontSize::SMALL);
    // Extract game title from first disc path stem (strip region tags crudely)
    std::string gameTitle;
    if (!m_discPaths.empty()) {
        fs::path p(m_discPaths[0]);
        std::string stem = p.stem().string();
        // Strip trailing parenthetical e.g. " (Disc 1) (USA)"
        auto paren = stem.find(" (");
        if (paren != std::string::npos) stem = stem.substr(0, paren);
        gameTitle = stem;
    }
    m_theme->drawTextCentered(gameTitle,
        m_w / 2, 52, pal.textPrimary, FontSize::BODY);

    // ── Layout constants ──────────────────────────────────────────────────────
    int centreX  = m_w / 2 + 30;    // shift right to make room for cover
    int centreY  = m_h / 2 - 10;
    int discR    = std::min(72, m_h / 6);
    int stackOffX=  22;              // each back disc steps right by this
    int stackOffY= -5;              // each back disc steps up slightly

    // ── Cover art panel — left side ───────────────────────────────────────────
    int coverW = 90, coverH = 120;
    int coverX = 30;
    int coverY = centreY - coverH / 2 - 20;

    SDL_Rect coverRect = { coverX, coverY, coverW, coverH };
    // Background placeholder
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 26, 18, 64, 220);
    SDL_RenderFillRect(m_renderer, &coverRect);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // Accent border
    SDL_SetRenderDrawColor(m_renderer,
        pal.accent.r, pal.accent.g, pal.accent.b, 255);
    SDL_RenderDrawRect(m_renderer, &coverRect);

    if (m_coverTexture) {
        // Letterbox the cover art to fit the panel
        int texW = 0, texH = 0;
        SDL_QueryTexture(m_coverTexture, nullptr, nullptr, &texW, &texH);
        if (texW > 0 && texH > 0) {
            float aspect   = (float)texW / (float)texH;
            float panelAsp = (float)coverW / (float)coverH;
            int dw, dh;
            if (aspect > panelAsp) { dw = coverW; dh = (int)(coverW / aspect); }
            else                   { dh = coverH; dw = (int)(coverH * aspect); }
            SDL_Rect dst = { coverX + (coverW - dw)/2,
                             coverY + (coverH - dh)/2, dw, dh };
            SDL_RenderCopy(m_renderer, m_coverTexture, nullptr, &dst);
        }
    } else {
        // Placeholder text
        m_theme->drawTextCentered("cover",
            coverX + coverW/2, coverY + coverH/2,
            pal.textDisable, FontSize::TINY);
    }

    // Disc label under cover
    std::string discLabel = "disc " + std::to_string(m_highlightedDisc + 1);
    m_theme->drawTextCentered(discLabel,
        coverX + coverW/2, coverY + coverH + 14,
        pal.accent, FontSize::TINY);

    // Disc count
    std::string countStr = std::to_string(total) + " discs";
    m_theme->drawTextCentered(countStr,
        coverX + coverW/2, coverY + coverH + 30,
        pal.textSecond, FontSize::TINY);

    // ── Stacked discs — back to front ─────────────────────────────────────────
    // Draw non-highlighted discs as a receding stack behind, highlighted lifted
    for (int i = total - 1; i >= 0; --i) {
        if (i == m_highlightedDisc) continue; // draw highlighted last (on top)
        int   dist    = std::abs(i - m_highlightedDisc);
        float opacity = std::max(0.2f, 1.0f - dist * 0.25f);
        int   cx      = centreX + (i - m_highlightedDisc) * stackOffX;
        int   cy      = centreY + (i - m_highlightedDisc) * stackOffY;
        renderDiscGraphic(i, cx, cy, discR, opacity, false, false);
    }

    // Highlighted disc — lifted up 20px, full opacity, accent ring
    {
        int cx = centreX;
        int cy = centreY - 20; // lift
        renderDiscGraphic(m_highlightedDisc, cx, cy, discR, 1.0f,
                          true, true);

        // "selected" text below
        m_theme->drawTextCentered("\xe2\x96\xb2 disc " +
            std::to_string(m_highlightedDisc + 1),
            cx, cy + discR + 26, pal.accent, FontSize::TINY);

        // Currently-inserted indicator
        if (m_highlightedDisc == m_currentDisc) {
            m_theme->drawTextCentered("(current)",
                cx, cy + discR + 42, pal.textSecond, FontSize::TINY);
        }
    }

    // ── L1 / R1 hint badges ────────────────────────────────────────────────────
    auto drawBadge = [&](int bx, int by, const std::string& txt) {
        int bw = 64, bh = 26;
        SDL_Rect br = { bx - bw/2, by - bh/2, bw, bh };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 48, 48, 46, 220);
        SDL_RenderFillRect(m_renderer, &br);
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &br);
        m_theme->drawTextCentered(txt, bx, by - 5, pal.textSecond, FontSize::TINY);
    };

    drawBadge(centreX - discR - 60, centreY, "prev  L1");
    drawBadge(centreX + discR + 60, centreY, "R1  next");

    // ── Footer ────────────────────────────────────────────────────────────────
    std::string confirmHint = (m_highlightedDisc == m_currentDisc)
        ? "Disc " + std::to_string(m_currentDisc + 1) + " (current)"
        : "Launch disc " + std::to_string(m_highlightedDisc + 1);
    m_theme->drawFooterHints(m_w, m_h, confirmHint, "Back");
}

// ─── renderDiscGraphic ────────────────────────────────────────────────────────
// Draws a single disc graphic at (cx, cy) with given radius and opacity.
// Uses SDL_SetRenderDrawBlendMode for the translucent rings.
// selected=true adds the accent ring; lifted=true adds a drop shadow.
void InGameMenu::renderDiscGraphic(int discIndex, int cx, int cy,
                                    int radius, float opacity,
                                    bool selected, bool lifted) {
    const auto& pal = m_theme->palette();
    Uint8 a = (Uint8)(255 * opacity);

    // Drop shadow when lifted
    if (lifted) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 60);
        // Simple ellipse shadow below
        for (int dy = -6; dy <= 6; dy++) {
            int sw = (int)(radius * 1.05f * std::sqrt(1.0 - (dy/(double)7) * (dy/(double)7)));
            SDL_RenderDrawLine(m_renderer, cx - sw, cy + radius + 10 + dy,
                               cx + sw, cy + radius + 10 + dy);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    // Helper: draw a filled circle
    auto fillCircle = [&](int x, int y, int r, SDL_Color col, Uint8 alpha) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, alpha);
        for (int dy = -r; dy <= r; dy++) {
            int dx = (int)std::sqrt((double)(r*r - dy*dy));
            SDL_RenderDrawLine(m_renderer, x - dx, y + dy, x + dx, y + dy);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    };

    // Helper: draw a circle outline
    auto drawCircle = [&](int x, int y, int r, SDL_Color col, Uint8 alpha) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, alpha);
        // Midpoint circle algorithm
        int dx = r, dy = 0, err = 0;
        while (dx >= dy) {
            SDL_RenderDrawPoint(m_renderer, x+dx, y+dy);
            SDL_RenderDrawPoint(m_renderer, x+dy, y+dx);
            SDL_RenderDrawPoint(m_renderer, x-dy, y+dx);
            SDL_RenderDrawPoint(m_renderer, x-dx, y+dy);
            SDL_RenderDrawPoint(m_renderer, x-dx, y-dy);
            SDL_RenderDrawPoint(m_renderer, x-dy, y-dx);
            SDL_RenderDrawPoint(m_renderer, x+dy, y-dx);
            SDL_RenderDrawPoint(m_renderer, x+dx, y-dy);
            dy++;
            err += 1 + 2*dy;
            if (2*(err-dx) + 1 > 0) { dx--; err += 1 - 2*dx; }
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    };

    // Disc body — dark blue-black
    SDL_Color bodyCol  = { 26, 26, 46, 255 };
    SDL_Color hubCol   = { 42, 30, 96, 255 };
    SDL_Color holeCol  = { 13, 10, 32, 255 };
    SDL_Color rimCol   = selected
        ? SDL_Color{ pal.accent.r, pal.accent.g, pal.accent.b, 255 }
        : SDL_Color{ 68, 68, 88, 255 };

    fillCircle(cx, cy, radius, bodyCol, a);
    drawCircle(cx, cy, radius, rimCol, a);

    // Rainbow sheen rings (selected only)
    if (selected) {
        SDL_Color r1 = { 124, 93,  232, 255 };
        SDL_Color r2 = {  91, 163, 217, 255 };
        SDL_Color r3 = {  96, 200, 154, 255 };
        drawCircle(cx, cy, (int)(radius * 0.90f), r1, (Uint8)(128 * opacity));
        drawCircle(cx, cy, (int)(radius * 0.69f), r2, (Uint8)(102 * opacity));
        drawCircle(cx, cy, (int)(radius * 0.49f), r3, (Uint8)(77  * opacity));
    }

    // Hub
    fillCircle(cx, cy, radius / 3, hubCol, a);
    // Centre hole
    fillCircle(cx, cy, radius / 11, holeCol, a);

    // Disc number label
    std::string label = "disc " + std::to_string(discIndex + 1);
    SDL_Color textCol = selected ? SDL_Color{ 226, 217, 255, a }
                                 : SDL_Color{ 160, 160, 180, a };
    m_theme->drawTextCentered(label, cx, cy - 6, textCol, FontSize::TINY);
}
