#include "ingame_menu.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace fs = std::filesystem;

// ─── Construction / Destruction ───────────────────────────────────────────────
InGameMenu::InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme,
                        ControllerNav* nav, SaveStateManager* saveStates)
    : m_renderer(renderer), m_theme(theme)
    , m_nav(nav), m_saveStates(saveStates)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
    rebuildMenuItems();
}

InGameMenu::~InGameMenu() {
    freeDiscTextures();
    freeThumbnails();
}

// ─── Menu items ───────────────────────────────────────────────────────────────
void InGameMenu::rebuildMenuItems() {
    m_items.clear();
    m_items.push_back({ "Resume",        "Return to game",         InGameMenuAction::RESUME        });
    m_items.push_back({ "Save State",    "Save current progress",  InGameMenuAction::SAVE_STATE    });
    m_items.push_back({ "Load State",    "Load a saved state",     InGameMenuAction::LOAD_STATE    });
    if (!m_discPaths.empty())
        m_items.push_back({ "Change Disc", "Switch to another disc", InGameMenuAction::CHANGE_DISC });
    m_items.push_back({ "Quit to Shelf", "Return to game library", InGameMenuAction::QUIT_TO_SHELF });
}

// ─── Disc info ────────────────────────────────────────────────────────────────
void InGameMenu::setDiscInfo(const std::vector<std::string>& discPaths,
                              int currentDisc) {
    m_discPaths       = discPaths;
    m_currentDisc     = currentDisc;
    m_highlightedDisc = currentDisc;
    m_pendingDiscIndex= currentDisc;
    rebuildMenuItems();
    std::cout << "[InGameMenu] Disc info set: " << discPaths.size()
              << " discs, current=" << currentDisc << "\n";
}

void InGameMenu::clearDiscInfo() {
    m_discPaths.clear();
    m_currentDisc     = 0;
    m_highlightedDisc = 0;
    m_pendingDiscIndex= 0;
    freeDiscTextures();
    rebuildMenuItems();
}

// ─── Disc art textures ────────────────────────────────────────────────────────
void InGameMenu::setDiscArtPaths(const std::vector<std::string>& paths) {
    m_discArtPaths = paths;
    // Always reload textures immediately when paths change — don't wait for
    // disc select to open. This prevents stale textures from a previous game
    // showing when a new game's disc select is opened.
    loadDiscTextures();
}

void InGameMenu::loadDiscTextures() {
    freeDiscTextures();
    m_discTextures.resize(m_discArtPaths.size(), nullptr);
    for (size_t i = 0; i < m_discArtPaths.size(); i++) {
        const auto& path = m_discArtPaths[i];
        if (path.empty() || !fs::exists(path)) {
            std::cout << "[InGameMenu] Disc art not found [" << i << "]: " << path << "\n";
            continue;
        }
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf) {
            std::cerr << "[InGameMenu] IMG_Load failed [" << i << "]: " << path << "\n";
            continue;
        }
        m_discTextures[i] = SDL_CreateTextureFromSurface(m_renderer, surf);
        SDL_FreeSurface(surf);
        if (m_discTextures[i])
            std::cout << "[InGameMenu] Loaded disc art [" << i << "]: " << path << "\n";
    }
}

void InGameMenu::freeDiscTextures() {
    for (auto* t : m_discTextures)
        if (t) SDL_DestroyTexture(t);
    m_discTextures.clear();
}

// ─── Open / Close ─────────────────────────────────────────────────────────────
void InGameMenu::open() {
    m_open            = true;
    m_section         = InGameMenuSection::MAIN;
    m_selectedItem    = 0;
    m_openAnim        = 1.f;
    m_pendingAction   = InGameMenuAction::NONE;
    m_highlightedDisc = m_currentDisc;
    std::cout << "[InGameMenu] Opened\n";
}

void InGameMenu::close() {
    m_open = false;
    m_pendingAction = InGameMenuAction::NONE;
    freeThumbnails();
}

// ─── Thumbnails ───────────────────────────────────────────────────────────────
void InGameMenu::freeThumbnails() {
    for (auto* t : m_thumbTextures)
        if (t) SDL_DestroyTexture(t);
    m_thumbTextures.clear();
}

void InGameMenu::loadThumbnails() {
    freeThumbnails();
    if (!m_saveStates) return;

    m_slots = m_saveStates->listSlots();
    int existingCount = (int)m_slots.size();
    for (int i = existingCount; i < SLOTS_PER_PAGE; i++) {
        SaveSlot empty;
        empty.slotNumber = i - 1;
        empty.exists     = false;
        empty.timestamp  = (i == 0) ? "Auto Save" : "Slot " + std::to_string(i);
        m_slots.push_back(empty);
    }
    m_thumbTextures.resize(m_slots.size(), nullptr);
    for (size_t i = 0; i < m_slots.size(); i++)
        if (m_slots[i].exists)
            m_thumbTextures[i] = m_saveStates->loadThumbnail(m_slots[i]);
}

// ─── Events ───────────────────────────────────────────────────────────────────
void InGameMenu::handleEvent(const SDL_Event& e) {
    if (!m_open) return;
    NavAction action = m_nav->processEvent(e);
    if (action == NavAction::NONE) return;

    switch (m_section) {
        case InGameMenuSection::MAIN:        navigateMain(action);       break;
        case InGameMenuSection::SAVE_STATES:
        case InGameMenuSection::LOAD_STATES: navigateSaveStates(action); break;
        case InGameMenuSection::DISC_SELECT: navigateDiscSelect(action); break;
    }
}

void InGameMenu::navigateMain(NavAction action) {
    switch (action) {
        case NavAction::UP:
            m_selectedItem = std::max(0, m_selectedItem - 1); break;
        case NavAction::DOWN:
            m_selectedItem = std::min((int)m_items.size()-1, m_selectedItem+1); break;
        case NavAction::CONFIRM: {
            auto chosen = m_items[m_selectedItem].action;
            m_nav->rumbleConfirm();
            if (chosen == InGameMenuAction::SAVE_STATE) {
                m_section = InGameMenuSection::SAVE_STATES;
                m_selectedSlot = 0; loadThumbnails();
            } else if (chosen == InGameMenuAction::LOAD_STATE) {
                m_section = InGameMenuSection::LOAD_STATES;
                m_selectedSlot = 0; loadThumbnails();
            } else if (chosen == InGameMenuAction::CHANGE_DISC) {
                m_section         = InGameMenuSection::DISC_SELECT;
                m_highlightedDisc = m_currentDisc;
                m_fanCentre       = (float)m_currentDisc;
                // Reset animation to beginning
                m_discPhase     = DiscAnimPhase::HOLD_COVER;
                m_phaseTimer    = 0.f;
                m_coverX        = 0.f;
                m_coverAlpha    = 1.f;
                m_fanProgress   = 0.f;
                m_loadTimer     = 0.f;
                m_loadSpinAngle = 0.f;
                // Textures are already loaded via setDiscArtPaths() — no lazy load needed
            } else {
                m_pendingAction = chosen;
            }
            break;
        }
        case NavAction::BACK:
        case NavAction::MENU:
            m_pendingAction = InGameMenuAction::RESUME; break;
        default: break;
    }
}

void InGameMenu::navigateDiscSelect(NavAction action) {
    // Input blocked during entry and load animations
    if (m_discPhase != DiscAnimPhase::SETTLED) return;

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
            m_pendingDiscIndex  = m_highlightedDisc;
            m_discPhase         = DiscAnimPhase::LOAD_DISC;
            m_loadTimer         = 0.f;
            m_loadSpinAngle     = 0.f;
            m_nav->rumbleConfirm();
            break;
        case NavAction::BACK:
        case NavAction::MENU:
            m_section = InGameMenuSection::MAIN; break;
        default: break;
    }
}

void InGameMenu::navigateSaveStates(NavAction action) {
    int total = (int)m_slots.size();
    if (total == 0) {
        if (action == NavAction::BACK) { m_section = InGameMenuSection::MAIN; freeThumbnails(); }
        return;
    }
    switch (action) {
        case NavAction::LEFT:  m_selectedSlot = std::max(0, m_selectedSlot-1); break;
        case NavAction::RIGHT: m_selectedSlot = std::min(total-1, m_selectedSlot+1); break;
        case NavAction::UP:    m_selectedSlot = std::max(0, m_selectedSlot-SLOT_COLS); break;
        case NavAction::DOWN:  m_selectedSlot = std::min(total-1, m_selectedSlot+SLOT_COLS); break;
        case NavAction::CONFIRM:
            if (m_section == InGameMenuSection::SAVE_STATES) {
                m_pendingAction = InGameMenuAction::SAVE_STATE;
                m_nav->rumbleConfirm();
            } else if (m_selectedSlot < (int)m_slots.size() &&
                       m_slots[m_selectedSlot].exists) {
                m_pendingAction = InGameMenuAction::LOAD_STATE;
                m_nav->rumbleConfirm();
            }
            break;
        case NavAction::BACK:
            m_section = InGameMenuSection::MAIN; freeThumbnails(); break;
        default: break;
    }
}

// ─── Update ───────────────────────────────────────────────────────────────────
void InGameMenu::update(float deltaMs) {
    if (!m_open) return;
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    m_spinAngle += 2.f * (deltaMs / 1000.f);

    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held != NavAction::NONE) {
        switch (m_section) {
            case InGameMenuSection::MAIN:        navigateMain(held);        break;
            case InGameMenuSection::SAVE_STATES:
            case InGameMenuSection::LOAD_STATES: navigateSaveStates(held);  break;
            case InGameMenuSection::DISC_SELECT: navigateDiscSelect(held);  break;
        }
    }

    // ── Disc select phase state machine ───────────────────────────────────────
    if (m_section != InGameMenuSection::DISC_SELECT) return;

    m_phaseTimer += deltaMs;

    switch (m_discPhase) {

        case DiscAnimPhase::HOLD_COVER:
            m_coverX     = 0.f;
            m_coverAlpha = 1.f;
            if (m_phaseTimer >= DUR_HOLD_COVER) {
                m_discPhase  = DiscAnimPhase::SLIDE_COVER;
                m_phaseTimer = 0.f;
            }
            break;

        case DiscAnimPhase::SLIDE_COVER: {
            float t    = std::min(1.f, m_phaseTimer / DUR_SLIDE_COVER);
            float ease = Ease::inOutQuad(t);
            m_coverX     = -(float)m_w * 0.65f * ease;
            m_coverAlpha = 1.f - ease;
            if (m_phaseTimer >= DUR_SLIDE_COVER) {
                m_coverAlpha = 0.f;
                m_discPhase  = DiscAnimPhase::HOLD_STACK;
                m_phaseTimer = 0.f;
            }
            break;
        }

        case DiscAnimPhase::HOLD_STACK:
            m_fanProgress = 0.f;
            if (m_phaseTimer >= DUR_HOLD_STACK) {
                m_discPhase  = DiscAnimPhase::FAN_OUT;
                m_phaseTimer = 0.f;
            }
            break;

        case DiscAnimPhase::FAN_OUT: {
            float t = std::min(1.f, m_phaseTimer / DUR_FAN_OUT);
            m_fanProgress = Ease::outCubic(t);
            if (m_phaseTimer >= DUR_FAN_OUT) {
                m_fanProgress = 1.f;
                m_discPhase   = DiscAnimPhase::SETTLED;
                m_phaseTimer  = 0.f;
            }
            break;
        }

        case DiscAnimPhase::SETTLED: {
            float target = (float)m_highlightedDisc;
            float diff   = target - m_fanCentre;
            m_fanCentre += diff * std::min(1.f, FAN_LERP_SPEED * (deltaMs / 1000.f));
            break;
        }

        case DiscAnimPhase::LOAD_DISC:
            m_loadTimer += deltaMs;
            // Spin accelerates over the animation duration
            m_loadSpinAngle += (float)(m_loadTimer / DUR_LOAD) * 0.30f;
            if (m_loadTimer >= DUR_LOAD)
                m_pendingAction = InGameMenuAction::CHANGE_DISC;
            break;
    }
}

// ─── Render ───────────────────────────────────────────────────────────────────
void InGameMenu::render(SDL_Texture*) {
    if (!m_open) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
    SDL_Rect full = { 0, 0, m_w, m_h };
    SDL_RenderFillRect(m_renderer, &full);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    switch (m_section) {
        case InGameMenuSection::MAIN:        renderMain();            break;
        case InGameMenuSection::SAVE_STATES: renderSaveStates(true);  break;
        case InGameMenuSection::LOAD_STATES: renderSaveStates(false); break;
        case InGameMenuSection::DISC_SELECT: renderDiscSelect();      break;
    }
}

// ─── renderMain ───────────────────────────────────────────────────────────────
void InGameMenu::renderMain() {
    const auto& pal = m_theme->palette();
    int panelW = std::min(MENU_W, m_w - 80);
    int itemH  = 70;
    int panelH = 80 + (int)m_items.size() * itemH + 40;
    int panelX = m_w - panelW - 60;
    int panelY = (m_h - panelH) / 2;
    if (panelX < 20) panelX = 20;
    if (panelY < 20) panelY = 20;

    SDL_Rect panel = { panelX, panelY, panelW, panelH };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 240);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    m_theme->drawRect({ panelX, panelY, 4, panelH }, pal.accent);
    m_theme->drawText("HAACKSTATION", panelX+16, panelY+16, pal.accent, FontSize::BODY);
    m_theme->drawText("In-Game Menu", panelX+16, panelY+40, pal.textSecond, FontSize::SMALL);
    m_theme->drawLine(panelX+16, panelY+64, panelX+panelW-16, panelY+64, pal.gridLine);

    int itemY = panelY + 76;
    for (int i = 0; i < (int)m_items.size(); i++) {
        bool sel = (i == m_selectedItem);
        const auto& item = m_items[i];
        if (sel) {
            SDL_Rect hi = { panelX+4, itemY-2, panelW-8, itemH-4 };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 50);
            SDL_RenderFillRect(m_renderer, &hi);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            m_theme->drawRect({ panelX+4, itemY-2, 4, itemH-4 }, pal.accent);
        }
        m_theme->drawText(item.label, panelX+20, itemY+8,
            sel ? pal.textPrimary : pal.textSecond, FontSize::BODY);
        m_theme->drawText(item.hint, panelX+20, itemY+32,
            sel ? pal.accent : pal.textDisable, FontSize::TINY);
        itemY += itemH;
    }
    m_theme->drawFooterHints(m_w, m_h, "Select", "Resume");
}

// ─── renderSaveStates ─────────────────────────────────────────────────────────
void InGameMenu::renderSaveStates(bool isSaving) {
    const auto& pal = m_theme->palette();
    m_theme->drawTextCentered(isSaving ? "Save State" : "Load State",
        m_w/2, 40, pal.accent, FontSize::TITLE);

    if (m_slots.empty()) {
        m_theme->drawTextCentered("No save states yet",
            m_w/2, m_h/2, pal.textSecond, FontSize::BODY);
        m_theme->drawFooterHints(m_w, m_h, "", "Back");
        return;
    }

    int cols   = SLOT_COLS;
    int cardW  = (m_w - 120) / cols - 16;
    int cardH  = (int)(cardW * 0.65f) + 40;
    int padX   = 16;
    int gridW  = cols * cardW + (cols-1) * padX;
    int startX = (m_w - gridW) / 2;
    int startY = 90;

    for (int i = 0; i < (int)m_slots.size() && i < SLOTS_PER_PAGE; i++) {
        int col = i % cols, row = i / cols;
        renderSlotCard(m_slots[i],
            startX + col*(cardW+padX), startY + row*(cardH+12),
            cardW, cardH, i == m_selectedSlot);
    }
    m_theme->drawFooterHints(m_w, m_h, isSaving ? "Save here" : "Load", "Back");
}

// ─── renderSlotCard ───────────────────────────────────────────────────────────
void InGameMenu::renderSlotCard(const SaveSlot& slot, int x, int y,
                                 int w, int h, bool selected) {
    const auto& pal = m_theme->palette();
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer,
        selected ? pal.bgCardHover.r : pal.bgCard.r,
        selected ? pal.bgCardHover.g : pal.bgCard.g,
        selected ? pal.bgCardHover.b : pal.bgCard.b, 220);
    SDL_Rect card = { x, y, w, h };
    SDL_RenderFillRect(m_renderer, &card);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    if (selected) {
        SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, 255);
        SDL_RenderDrawRect(m_renderer, &card);
    }
    int thumbH = h - 36;
    if (slot.exists) {
        SDL_Texture* thumb = nullptr;
        for (int i = 0; i < (int)m_slots.size(); i++)
            if (m_slots[i].slotNumber == slot.slotNumber && i < (int)m_thumbTextures.size())
                { thumb = m_thumbTextures[i]; break; }
        if (thumb) {
            SDL_RenderCopy(m_renderer, thumb, nullptr, &SDL_Rect{x+2,y+2,w-4,thumbH-4});
        } else {
            m_theme->drawRect({x+2,y+2,w-4,thumbH-4}, pal.bgPanel);
            m_theme->drawTextCentered("No Preview", x+w/2, y+thumbH/2, pal.textDisable, FontSize::TINY);
        }
        m_theme->drawText(slot.timestamp, x+6, y+thumbH+4, pal.textPrimary, FontSize::TINY);
    } else {
        m_theme->drawRect({x+2,y+2,w-4,thumbH-4}, pal.bgPanel);
        std::string label = slot.slotNumber < 0 ? "Auto Save" :
                            "Slot " + std::to_string(slot.slotNumber + 1);
        m_theme->drawTextCentered(label,  x+w/2, y+h/2-8,  pal.textDisable, FontSize::SMALL);
        m_theme->drawTextCentered("Empty",x+w/2, y+h/2+14, pal.textDisable, FontSize::TINY);
    }
}

// ─── renderTextureAsDisc ──────────────────────────────────────────────────────
// Maps srcTex onto a filled circle at (cx,cy) radius r.
//
// Key fix for the stretch bug: we treat the source texture as a square canvas
// and sample it using NORMALISED coordinates (-1..1 in both axes), so the
// mapping is always 1:1 regardless of the texture's pixel dimensions.
// For each output scanline y in [-r,r]:
//   chordHalf = sqrt(r²-dy²)          — half-width of chord at this y
//   For each output pixel x in that chord:
//     nx = x/r,  ny = dy/r            — normalised disc coords [-1..1]
//     if spinAngle != 0: rotate (nx,ny) by spinAngle
//     srcU = (nx+1)/2 * texW           — map to texture U coordinate
//     srcV = (ny+1)/2 * texH           — map to texture V coordinate
//   Blit that one-pixel-high strip as a SDL_RenderCopy.
//
// We batch the strip into a single RenderCopy per scanline (not per pixel) by
// computing a source sub-rect for the whole chord when not spinning.
// When spinning we fall through to the per-pixel path — it's only visible
// during the short load animation so performance is acceptable.
void InGameMenu::renderTextureAsDisc(SDL_Texture* srcTex, int cx, int cy,
                                      int radius, Uint8 alpha, float spinAngle) {
    if (!srcTex || radius <= 0) return;
    int texW = 0, texH = 0;
    SDL_QueryTexture(srcTex, nullptr, nullptr, &texW, &texH);
    if (texW <= 0 || texH <= 0) return;

    SDL_SetTextureBlendMode(srcTex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(srcTex, alpha);

    bool spinning = (std::abs(spinAngle) > 0.001f);

    for (int dy = -radius; dy <= radius; dy++) {
        double r2 = (double)radius * (double)radius;
        double d2 = (double)dy * (double)dy;
        if (d2 > r2) continue;

        double chordHalf = std::sqrt(r2 - d2);
        int    chordW    = (int)(chordHalf * 2.0);
        if (chordW <= 0) continue;

        int lineY = cy + dy;

        // Normalised Y coordinate for this scanline: dy/radius in [-1..1]
        float ny = (float)dy / (float)radius;

        if (!spinning) {
            // Fast path: blit whole scanline as one strip.
            // Left pixel's normalised X: -chordHalf/radius
            // Right pixel's normalised X: +chordHalf/radius
            // Map to texture: srcU = (nx+1)/2 * texW
            float nxLeft  = -(float)chordHalf / (float)radius;
            float nxRight =  (float)chordHalf / (float)radius;

            int srcX0 = (int)(((nxLeft  + 1.f) * 0.5f) * (float)(texW - 1));
            int srcX1 = (int)(((nxRight + 1.f) * 0.5f) * (float)(texW - 1));
            int srcY  = (int)(((ny      + 1.f) * 0.5f) * (float)(texH - 1));

            srcX0 = std::max(0, std::min(texW - 1, srcX0));
            srcX1 = std::max(0, std::min(texW - 1, srcX1));
            srcY  = std::max(0, std::min(texH - 1, srcY));

            int srcW = srcX1 - srcX0 + 1;
            if (srcW <= 0) continue;

            SDL_Rect src = { srcX0, srcY, srcW, 1 };
            SDL_Rect dst = { cx - (int)chordHalf, lineY, chordW, 1 };
            SDL_RenderCopy(m_renderer, srcTex, &src, &dst);
        } else {
            // Spinning path: rotate each pixel's sample point, then blit strips
            // Batch consecutive pixels with the same srcY into strips for speed.
            int batchDstX  = cx - (int)chordHalf;
            int batchSrcX0 = -1;
            int batchSrcY  = -1;
            int batchSrcW  = 0;

            float cosA = std::cos(spinAngle);
            float sinA = std::sin(spinAngle);

            for (int dx = -(int)chordHalf; dx <= (int)chordHalf; dx++) {
                float nx = (float)dx / (float)radius;
                // Rotate sample point
                float rx = cosA * nx - sinA * ny;
                float ry = sinA * nx + cosA * ny;
                // Clamp to disc (outside disc boundary wrap to edge)
                float len = std::sqrt(rx*rx + ry*ry);
                if (len > 1.f) { rx /= len; ry /= len; }

                int srcX = (int)(((rx + 1.f) * 0.5f) * (float)(texW - 1));
                int srcY = (int)(((ry + 1.f) * 0.5f) * (float)(texH - 1));
                srcX = std::max(0, std::min(texW - 1, srcX));
                srcY = std::max(0, std::min(texH - 1, srcY));

                if (srcY == batchSrcY && srcX == batchSrcX0 + batchSrcW) {
                    batchSrcW++;
                } else {
                    // Flush previous batch
                    if (batchSrcW > 0) {
                        SDL_Rect src = { batchSrcX0, batchSrcY, batchSrcW, 1 };
                        SDL_Rect dst = { batchDstX, lineY, batchSrcW, 1 };
                        SDL_RenderCopy(m_renderer, srcTex, &src, &dst);
                        batchDstX += batchSrcW;
                    }
                    batchSrcX0 = srcX;
                    batchSrcY  = srcY;
                    batchSrcW  = 1;
                }
            }
            // Flush last batch
            if (batchSrcW > 0) {
                SDL_Rect src = { batchSrcX0, batchSrcY, batchSrcW, 1 };
                SDL_Rect dst = { batchDstX, lineY, batchSrcW, 1 };
                SDL_RenderCopy(m_renderer, srcTex, &src, &dst);
            }
        }
    }
    SDL_SetTextureAlphaMod(srcTex, 255);
}

// ─── renderDiscGraphic ────────────────────────────────────────────────────────
void InGameMenu::renderDiscGraphic(int discIndex, int cx, int cy, int radius,
                                    float opacity, bool selected, bool lifted,
                                    SDL_Texture* discTexture, float spinAngle) {
    const auto& pal = m_theme->palette();
    Uint8 a = (Uint8)(255.f * std::max(0.f, std::min(1.f, opacity)));

    auto fillCircle = [&](int x, int y, int r, SDL_Color col, Uint8 alpha) {
        if (r <= 0) return;
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, alpha);
        for (int dy = -r; dy <= r; dy++) {
            int dx = (int)std::sqrt(std::max(0.0, (double)(r*r - dy*dy)));
            SDL_RenderDrawLine(m_renderer, x-dx, y+dy, x+dx, y+dy);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    };

    auto drawCircle = [&](int x, int y, int r, SDL_Color col, Uint8 alpha) {
        if (r <= 0) return;
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, alpha);
        int dx = r, dy = 0, err = 0;
        while (dx >= dy) {
            SDL_RenderDrawPoint(m_renderer, x+dx, y+dy); SDL_RenderDrawPoint(m_renderer, x+dy, y+dx);
            SDL_RenderDrawPoint(m_renderer, x-dy, y+dx); SDL_RenderDrawPoint(m_renderer, x-dx, y+dy);
            SDL_RenderDrawPoint(m_renderer, x-dx, y-dy); SDL_RenderDrawPoint(m_renderer, x-dy, y-dx);
            SDL_RenderDrawPoint(m_renderer, x+dy, y-dx); SDL_RenderDrawPoint(m_renderer, x+dx, y-dy);
            dy++; err += 1 + 2*dy;
            if (2*(err-dx)+1 > 0) { dx--; err += 1-2*dx; }
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    };

    // Drop shadow when lifted
    if (lifted && a > 0) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)(80 * opacity));
        for (int dy = -8; dy <= 8; dy++) {
            double f = 1.0 - (dy/(double)9)*(dy/(double)9);
            int sw = (int)(radius * 1.1f * std::sqrt(std::max(0.0, f)));
            SDL_RenderDrawLine(m_renderer, cx-sw, cy+radius+14+dy, cx+sw, cy+radius+14+dy);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    if (discTexture) {
        // Dark base fill
        fillCircle(cx, cy, radius, {16, 12, 32, 255}, a);
        // Disc art mapped onto circle (fixed normalised coordinate sampling)
        renderTextureAsDisc(discTexture, cx, cy, radius, a, spinAngle);
        // Subtle vignette ring for depth
        for (int v = 0; v < 6; v++) {
            Uint8 vigA = (Uint8)((6-v) * 15 * opacity);
            drawCircle(cx, cy, radius-v, {0,0,0,255}, vigA);
        }
        // Rim
        SDL_Color rimCol = selected
            ? SDL_Color{pal.accent.r, pal.accent.g, pal.accent.b, 255}
            : SDL_Color{90, 90, 110, 255};
        drawCircle(cx, cy, radius, rimCol, a);
        // Hub
        fillCircle(cx, cy, radius/4, {18,14,42,200}, a);
        drawCircle(cx, cy, radius/4, {70,60,110,180}, a);
        // Centre hole
        fillCircle(cx, cy, std::max(3, radius/11), {6,4,16,255}, a);
    } else {
        // Procedural fallback
        SDL_Color bodyCol = {26,26,46,255};
        SDL_Color hubCol  = {42,30,96,255};
        SDL_Color holeCol = {13,10,32,255};
        SDL_Color rimCol  = selected
            ? SDL_Color{pal.accent.r,pal.accent.g,pal.accent.b,255}
            : SDL_Color{68,68,88,255};
        fillCircle(cx, cy, radius, bodyCol, a);
        drawCircle(cx, cy, radius, rimCol, a);
        if (selected) {
            drawCircle(cx, cy, (int)(radius*0.90f), {124,93,232,255}, (Uint8)(120*opacity));
            drawCircle(cx, cy, (int)(radius*0.70f), { 91,163,217,255}, (Uint8)(90*opacity));
            drawCircle(cx, cy, (int)(radius*0.50f), { 96,200,154,255}, (Uint8)(70*opacity));
        }
        fillCircle(cx, cy, radius/3,  hubCol,  a);
        fillCircle(cx, cy, std::max(3,radius/11), holeCol, a);
        m_theme->drawTextCentered("disc " + std::to_string(discIndex+1),
            cx, cy-7, selected ? SDL_Color{226,217,255,a} : SDL_Color{160,160,180,a},
            FontSize::TINY);
    }
}

// ─── renderDiscSelect ─────────────────────────────────────────────────────────
void InGameMenu::renderDiscSelect() {
    const auto& pal = m_theme->palette();
    int total = (int)m_discPaths.size();
    if (total == 0) return;

    // ── Layout ────────────────────────────────────────────────────────────────
    // Discs at ~50% screen height. Selected disc centre sits at 62% down
    // so discs feel centred in the lower visual weight of the screen.
    int discR      = (int)(m_h * 0.250f);   // radius = 50% h / 2
    int discRSmall = (int)(discR * 0.76f);  // non-selected discs

    int centreX    = m_w / 2;
    int centreY    = (int)(m_h * 0.62f);    // selected disc centre

    // Cover fills 70% of screen height — larger than disc stack so it
    // looks like the discs are sliding out from behind the case.
    int coverH_large = (int)(m_h * 0.70f);
    int coverW_large = (int)(coverH_large * 0.72f);  // PS1 case portrait ratio

    // Fan spacing: generous overlap, discs separated enough to read individually
    int fanSpacing = (int)(discR * 1.22f);

    // ── Title ─────────────────────────────────────────────────────────────────
    std::string gameTitle;
    if (!m_discPaths.empty()) {
        fs::path p(m_discPaths[0]);
        std::string stem = p.stem().string();
        auto paren = stem.find(" (");
        if (paren != std::string::npos) stem = stem.substr(0, paren);
        gameTitle = stem;
    }
    m_theme->drawTextCentered("DISC SELECT",
        centreX, 28, pal.textSecond, FontSize::SMALL);
    m_theme->drawTextCentered(gameTitle,
        centreX, 56, pal.textPrimary, FontSize::TITLE);

    // ── Cover art — shown during HOLD_COVER and SLIDE_COVER only ─────────────
    // Rendered completely borderless — just the image with a drop shadow,
    // same as the game shelf card style. Scale-to-fit so the full cover is
    // always visible with no cropping regardless of aspect ratio.
    // Slides left and fades out completely during SLIDE_COVER.
    if (m_coverAlpha > 0.01f) {
        int cx = centreX + (int)m_coverX;
        int cy = centreY;   // vertically centred on the same axis as the discs

        if (m_coverTexture) {
            int texW = 0, texH = 0;
            SDL_QueryTexture(m_coverTexture, nullptr, nullptr, &texW, &texH);
            if (texW > 0 && texH > 0) {
                // Scale-to-fit: shrink uniformly so the full image fits inside
                // the coverW_large × coverH_large container. No cropping.
                float scale = std::min((float)coverW_large / (float)texW,
                                       (float)coverH_large / (float)texH);
                int dw = (int)(texW * scale);
                int dh = (int)(texH * scale);
                int dx = cx - dw / 2;
                int dy = cy - dh / 2;

                // Drop shadow — offset below/right, same style as shelf hero
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0, 0, 0,
                    (Uint8)(150 * m_coverAlpha));
                SDL_RenderFillRect(m_renderer,
                    &SDL_Rect{dx + 10, dy + 12, dw, dh});
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

                // Image — no background, no border, pure art
                SDL_SetTextureBlendMode(m_coverTexture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(m_coverTexture,
                    (Uint8)(255 * m_coverAlpha));
                SDL_RenderCopy(m_renderer, m_coverTexture, nullptr,
                    &SDL_Rect{dx, dy, dw, dh});
                SDL_SetTextureAlphaMod(m_coverTexture, 255);
            }
        }
    }

    // ── Disc fan ──────────────────────────────────────────────────────────────
    // Not shown during HOLD_COVER or SLIDE_COVER
    bool showDiscs = (m_discPhase != DiscAnimPhase::HOLD_COVER &&
                      m_discPhase != DiscAnimPhase::SLIDE_COVER);
    if (!showDiscs) goto render_footer;

    {
        // Draw order: non-selected back to front, selected on top
        std::vector<int> drawOrder;
        for (int i = 0; i < total; i++)
            if (i != m_highlightedDisc) drawOrder.push_back(i);
        drawOrder.push_back(m_highlightedDisc);

        bool isStacked = (m_discPhase == DiscAnimPhase::HOLD_STACK);

        for (int i : drawOrder) {
            bool isSelected = (i == m_highlightedDisc);

            // Settled position relative to fan centre
            float relI     = (float)i - m_fanCentre;
            int settledX   = centreX + (int)(relI * fanSpacing);
            int settledY   = isSelected ? centreY - (int)(discR * 0.16f) : centreY;

            int discX, discY;
            if (isStacked) {
                int stackOff = (i - m_highlightedDisc) * 4;
                discX = centreX + stackOff;
                discY = centreY;
            } else {
                float cascade   = std::abs(relI) * 0.055f;
                float localT    = std::max(0.f, std::min(1.f, m_fanProgress - cascade));
                float localEase = Ease::outCubic(localT);
                int stackX = centreX + (i - m_highlightedDisc) * 4;
                discX = stackX + (int)((settledX - stackX) * localEase);
                discY = centreY + (int)((settledY - centreY) * localEase);
            }

            // Radius: selected stays full, others shrink as fan opens
            int r;
            if (isSelected) {
                r = discR;
            } else {
                float sizeEase = Ease::outCubic(m_fanProgress);
                r = discR + (int)((discRSmall - discR) * sizeEase);
            }

            // Opacity: fades by distance from selection
            float baseOpacity = isSelected ? 1.f
                              : std::max(0.32f, 1.f - std::abs(relI) * 0.20f);
            float fadeIn  = (m_discPhase == DiscAnimPhase::HOLD_STACK) ? 0.88f : 1.f;
            float opacity = baseOpacity * fadeIn;

            // Load animation: selected disc spins and flies up.
            // The drop shadow is handled SEPARATELY below — it stays planted
            // on the floor while the disc rises, then shrinks and fades out.
            // Pass lifted=false so renderDiscGraphic doesn't also draw a shadow.
            float spinAngle  = 0.f;
            bool  drawShadow = isSelected; // normal shadow for settled disc
            if (m_discPhase == DiscAnimPhase::LOAD_DISC && isSelected) {
                float loadT    = std::min(1.f, m_loadTimer / DUR_LOAD);
                float loadEase = Ease::inOutQuad(loadT);
                discY     -= (int)((centreY + discR + 60) * loadEase);
                spinAngle  = m_loadSpinAngle;
                opacity    = std::max(0.f, 1.f - loadEase * 0.4f);
                drawShadow = false; // we draw it manually below
            }

            SDL_Texture* discTex = nullptr;
            if (i < (int)m_discTextures.size()) discTex = m_discTextures[i];

            // Pass lifted=drawShadow so the built-in shadow only draws when settled
            renderDiscGraphic(i, discX, discY, r, opacity,
                              isSelected, drawShadow, discTex, spinAngle);

            // ── Floor shadow during load animation ────────────────────────────
            // As the disc rises, a detached ellipse shadow stays at floor level,
            // shrinks horizontally (object subtends less angle from far away),
            // and fades to nothing — physically correct and looks great.
            if (m_discPhase == DiscAnimPhase::LOAD_DISC && isSelected) {
                float loadT    = std::min(1.f, m_loadTimer / DUR_LOAD);
                float loadEase = Ease::inOutQuad(loadT);

                // Shadow stays at the floor — the Y position where the disc sat
                int   shadowCY = centreY + discR + 14;  // floor level
                int   shadowCX = settledX;               // stays at original X

                // Width shrinks from full to zero; height stays constant
                float shadowW  = (float)discR * 1.1f * (1.f - loadEase);
                int   shadowH  = std::max(1, (int)(discR * 0.12f));

                // Opacity fades from 80 → 0
                Uint8 shadowA  = (Uint8)(80.f * (1.f - loadEase));

                if (shadowW > 1.f && shadowA > 2) {
                    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, shadowA);
                    // Draw a filled horizontal ellipse using scanlines
                    float hw = shadowW * 0.5f;
                    float hh = (float)shadowH * 0.5f;
                    for (int dy = -(int)hh; dy <= (int)hh; dy++) {
                        float t  = (float)dy / hh;
                        float xw = hw * std::sqrt(std::max(0.f, 1.f - t * t));
                        if (xw < 0.5f) continue;
                        SDL_RenderDrawLine(m_renderer,
                            shadowCX - (int)xw, shadowCY + dy,
                            shadowCX + (int)xw, shadowCY + dy);
                    }
                    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                }
            }

            // Label — settled state only
            if (m_discPhase == DiscAnimPhase::SETTLED && isSelected) {
                std::string label = "Disc " + std::to_string(i+1);
                if (i == m_currentDisc) label += "  \xe2\x80\xa2  current";
                m_theme->drawTextCentered(label,
                    discX, discY + discR + 26,
                    pal.accent, FontSize::BODY);
            }
        }
    }

    render_footer:
    if (m_discPhase == DiscAnimPhase::SETTLED) {
        std::string confirmHint = (m_highlightedDisc == m_currentDisc)
            ? "Disc " + std::to_string(m_currentDisc+1) + " (current)"
            : "Load disc " + std::to_string(m_highlightedDisc+1);
        m_theme->drawFooterHints(m_w, m_h, confirmHint, "Back");
    }
}
