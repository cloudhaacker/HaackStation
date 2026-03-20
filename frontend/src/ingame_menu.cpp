#include "ingame_menu.h"
#include <iostream>
#include <algorithm>
#include <cmath>

InGameMenu::InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
                        ControllerNav* nav, SaveStateManager* saveStates)
    : m_renderer(renderer), m_theme(theme)
    , m_nav(nav), m_saveStates(saveStates)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);

    m_items = {
        { "Resume",        "Return to game",          InGameMenuAction::RESUME        },
        { "Save State",    "Save current progress",   InGameMenuAction::SAVE_STATE    },
        { "Load State",    "Load a saved state",      InGameMenuAction::LOAD_STATE    },
        { "Quit to Shelf", "Return to game library",  InGameMenuAction::QUIT_TO_SHELF },
    };
}

void InGameMenu::open() {
    m_open          = true;
    m_section       = InGameMenuSection::MAIN;
    m_selectedItem  = 0;
    m_openAnim      = 1.f;  // instant open — no slide animation for now
    m_pendingAction = InGameMenuAction::NONE;
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
    if (action == NavAction::NONE)
        action = m_nav->updateHeld(SDL_GetTicks());

    switch (m_section) {
        case InGameMenuSection::MAIN:        navigateMain(action);         break;
        case InGameMenuSection::SAVE_STATES:
        case InGameMenuSection::LOAD_STATES: navigateSaveStates(action);   break;
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
            } else {
                // RESUME or QUIT_TO_SHELF — set action for app to process
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
