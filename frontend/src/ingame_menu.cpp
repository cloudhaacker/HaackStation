#include "ingame_menu.h"
#include <SDL2/SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace fs = std::filesystem;

// ─── Construction / Destruction ───────────────────────────────────────────────
InGameMenu::InGameMenu(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
    rebuildMenuItems();
}

InGameMenu::~InGameMenu() {
    freeDiscTextures();
}

// ─── Menu items ───────────────────────────────────────────────────────────────
// Memory card management lives entirely in OmniSave — no card item here.
void InGameMenu::rebuildMenuItems() {
    m_items.clear();
    m_items.push_back({ "Resume",        "Return to game",              InGameMenuAction::RESUME        });
    m_items.push_back({ "OmniSave",      "Saves, states & memory cards",InGameMenuAction::OPEN_OMNISAVE });
    m_items.push_back({ "Soft Reset",    "Restart game, keep saves",    InGameMenuAction::SOFT_RESET    });
    if (!m_discPaths.empty())
        m_items.push_back({ "Change Disc", "Switch to another disc",    InGameMenuAction::CHANGE_DISC });
    m_items.push_back({ "Quit to Shelf", "Return to game library",      InGameMenuAction::QUIT_TO_SHELF });
}

// ─── Disc info ────────────────────────────────────────────────────────────────
void InGameMenu::setDiscInfo(const std::vector<std::string>& discPaths, int currentDisc) {
    m_discPaths       = discPaths;
    m_currentDisc     = currentDisc;
    m_highlightedDisc = currentDisc;
    m_pendingDiscIndex= currentDisc;
    rebuildMenuItems();
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
    loadDiscTextures();
}

void InGameMenu::loadDiscTextures() {
    freeDiscTextures();
    m_discTextures.resize(m_discArtPaths.size(), nullptr);
    for (size_t i = 0; i < m_discArtPaths.size(); i++) {
        const auto& path = m_discArtPaths[i];
        if (path.empty() || !fs::exists(path)) continue;
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf) continue;
        m_discTextures[i] = SDL_CreateTextureFromSurface(m_renderer, surf);
        SDL_FreeSurface(surf);
    }
}

void InGameMenu::freeDiscTextures() {
    for (auto* t : m_discTextures)
        if (t) SDL_DestroyTexture(t);
    m_discTextures.clear();
}

// ─── Open / Close ─────────────────────────────────────────────────────────────
void InGameMenu::open() {
    m_open              = true;
    m_section           = InGameMenuSection::MAIN;
    m_selectedItem      = 0;
    m_openAnim          = 1.f;
    m_pendingAction     = InGameMenuAction::NONE;
    m_highlightedDisc   = m_currentDisc;
    m_confirmSoftReset  = false;
}

void InGameMenu::close() {
    m_open             = false;
    m_pendingAction    = InGameMenuAction::NONE;
    m_confirmSoftReset = false;
}

// ─── Events ───────────────────────────────────────────────────────────────────
void InGameMenu::handleEvent(const SDL_Event& e) {
    if (!m_open) return;
    NavAction action = m_nav->processEvent(e);
    if (action == NavAction::NONE) return;

    switch (m_section) {
        case InGameMenuSection::MAIN:        navigateMain(action);       break;
        case InGameMenuSection::DISC_SELECT: navigateDiscSelect(action); break;
    }
}

void InGameMenu::navigateMain(NavAction action) {
    // ── Soft reset confirm intercepts all input when visible ──────────────────
    if (m_confirmSoftReset) {
        if (action == NavAction::CONFIRM) {
            m_confirmSoftReset = false;
            m_pendingAction    = InGameMenuAction::SOFT_RESET;
        } else if (action == NavAction::BACK || action == NavAction::MENU) {
            m_confirmSoftReset = false;
        }
        return;
    }

    switch (action) {
        case NavAction::UP:
            m_selectedItem = std::max(0, m_selectedItem - 1); break;
        case NavAction::DOWN:
            m_selectedItem = std::min((int)m_items.size()-1, m_selectedItem+1); break;
        case NavAction::CONFIRM: {
            auto chosen = m_items[m_selectedItem].action;
            m_nav->rumbleConfirm();
            if (chosen == InGameMenuAction::CHANGE_DISC) {
                m_section         = InGameMenuSection::DISC_SELECT;
                m_highlightedDisc = m_currentDisc;
                m_fanCentre       = (float)m_currentDisc;
                m_discPhase       = DiscAnimPhase::HOLD_COVER;
                m_phaseTimer      = 0.f;
                m_coverX          = 0.f;
                m_coverAlpha      = 1.f;
                m_fanProgress     = 0.f;
                m_loadTimer       = 0.f;
                m_loadSpinAngle   = 0.f;
            } else if (chosen == InGameMenuAction::SOFT_RESET) {
                m_confirmSoftReset = true;
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

// ─── Update ───────────────────────────────────────────────────────────────────
void InGameMenu::update(float deltaMs) {
    if (!m_open) return;
    SDL_GetRendererOutputSize(m_renderer, &m_w, &m_h);
    m_spinAngle += 2.f * (deltaMs / 1000.f);

    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held != NavAction::NONE) {
        switch (m_section) {
            case InGameMenuSection::MAIN:        navigateMain(held);       break;
            case InGameMenuSection::DISC_SELECT: navigateDiscSelect(held); break;
        }
    }

    if (m_section != InGameMenuSection::DISC_SELECT) return;

    m_phaseTimer += deltaMs;

    switch (m_discPhase) {
        case DiscAnimPhase::HOLD_COVER:
            m_coverX = 0.f; m_coverAlpha = 1.f;
            if (m_phaseTimer >= DUR_HOLD_COVER) { m_discPhase = DiscAnimPhase::SLIDE_COVER; m_phaseTimer = 0.f; }
            break;
        case DiscAnimPhase::SLIDE_COVER: {
            float t = std::min(1.f, m_phaseTimer / DUR_SLIDE_COVER);
            float ease = Ease::inOutQuad(t);
            m_coverX     = -(float)m_w * 0.65f * ease;
            m_coverAlpha = 1.f - ease;
            if (m_phaseTimer >= DUR_SLIDE_COVER) { m_coverAlpha = 0.f; m_discPhase = DiscAnimPhase::HOLD_STACK; m_phaseTimer = 0.f; }
            break;
        }
        case DiscAnimPhase::HOLD_STACK:
            m_fanProgress = 0.f;
            if (m_phaseTimer >= DUR_HOLD_STACK) { m_discPhase = DiscAnimPhase::FAN_OUT; m_phaseTimer = 0.f; }
            break;
        case DiscAnimPhase::FAN_OUT: {
            float t = std::min(1.f, m_phaseTimer / DUR_FAN_OUT);
            m_fanProgress = Ease::outCubic(t);
            if (m_phaseTimer >= DUR_FAN_OUT) { m_fanProgress = 1.f; m_discPhase = DiscAnimPhase::SETTLED; m_phaseTimer = 0.f; }
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
        case InGameMenuSection::MAIN:
            renderMain();
            if (m_confirmSoftReset) renderSoftResetConfirm();
            break;
        case InGameMenuSection::DISC_SELECT:
            renderDiscSelect();
            break;
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

// ─── renderSoftResetConfirm ───────────────────────────────────────────────────
void InGameMenu::renderSoftResetConfirm() {
    const auto& pal = m_theme->palette();
    static const SDL_Color AMBER = { 220, 140, 0, 255 };

    int dlgW = 420, dlgH = 180;
    int dlgX = (m_w - dlgW) / 2;
    int dlgY = (m_h - dlgH) / 2;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, pal.bgPanel.r, pal.bgPanel.g, pal.bgPanel.b, 252);
    SDL_RenderFillRect(m_renderer, &SDL_Rect{ dlgX, dlgY, dlgW, dlgH });
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    for (int t = 0; t < 3; ++t)
        m_theme->drawRect({ dlgX+t, dlgY+t, dlgW-2*t, dlgH-2*t }, AMBER);

    int cx = dlgX + dlgW / 2;
    m_theme->drawTextCentered("Soft Reset?",               cx, dlgY + 32,  AMBER,            FontSize::TITLE);
    m_theme->drawTextCentered("The game will restart.",    cx, dlgY + 76,  pal.textPrimary,  FontSize::BODY);
    m_theme->drawTextCentered("Your memory card saves are safe.", cx, dlgY + 104, pal.textSecond, FontSize::SMALL);
    m_theme->drawTextCentered("[A] Reset    [B] Cancel",   cx, dlgY + 146, pal.textDisable,  FontSize::SMALL);
}

// ─── renderTextureAsDisc ──────────────────────────────────────────────────────
void InGameMenu::renderTextureAsDisc(SDL_Texture* srcTex, int cx, int cy,
                                      int radius, Uint8 alpha, float spinAngle)
{
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

        if (!spinning) {
            double nyNorm  = (double)dy / (double)radius;
            double nx0Norm = -(chordHalf / (double)radius);
            double nx1Norm =  (chordHalf / (double)radius);
            int srcX = (int)((nx0Norm + 1.0) * 0.5 * texW);
            int srcY = (int)((nyNorm  + 1.0) * 0.5 * texH);
            int srcW = (int)((nx1Norm - nx0Norm) * 0.5 * texW);
            if (srcW <= 0) continue;
            srcX = std::max(0, std::min(srcX, texW - 1));
            srcY = std::max(0, std::min(srcY, texH - 1));
            srcW = std::min(srcW, texW - srcX);
            SDL_Rect srcRect = { srcX, srcY, srcW, 1 };
            SDL_Rect dstRect = { cx - (int)chordHalf, lineY, chordW, 1 };
            SDL_RenderCopy(m_renderer, srcTex, &srcRect, &dstRect);
        } else {
            float cosA = std::cos(spinAngle), sinA = std::sin(spinAngle);
            for (int dx = -(int)chordHalf; dx <= (int)chordHalf; dx++) {
                float nx = (float)dx / (float)radius;
                float ny = (float)dy / (float)radius;
                float rnx = cosA * nx - sinA * ny;
                float rny = sinA * nx + cosA * ny;
                int srcX = std::max(0, std::min((int)((rnx + 1.0f) * 0.5f * texW), texW - 1));
                int srcY = std::max(0, std::min((int)((rny + 1.0f) * 0.5f * texH), texH - 1));
                SDL_Rect srcRect = { srcX, srcY, 1, 1 };
                SDL_Rect dstRect = { cx + dx, lineY, 1, 1 };
                SDL_RenderCopy(m_renderer, srcTex, &srcRect, &dstRect);
            }
        }
    }
    SDL_SetTextureAlphaMod(srcTex, 255);
}

// ─── renderDiscGraphic ────────────────────────────────────────────────────────
void InGameMenu::renderDiscGraphic(int discIndex, int cx, int cy, int radius,
                                    float opacity, bool selected, bool lifted,
                                    SDL_Texture* discTexture, float spinAngle)
{
    const auto& pal = m_theme->palette();
    Uint8 alpha = (Uint8)(opacity * 255.f);

    if (lifted && radius > 0) {
        int shadowOff = radius / 8 + 4;
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)(80.f * opacity));
        double r2 = (double)radius * (double)radius;
        for (int dy = -radius; dy <= radius; dy++) {
            double d2 = (double)dy * (double)dy;
            if (d2 > r2) continue;
            int hw = (int)std::sqrt(r2 - d2);
            SDL_RenderDrawLine(m_renderer, cx-hw+shadowOff, cy+dy+shadowOff, cx+hw+shadowOff, cy+dy+shadowOff);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    if (discTexture) {
        renderTextureAsDisc(discTexture, cx, cy, radius, alpha, spinAngle);
    } else {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 30, 30, 36, alpha);
        double r2 = (double)radius * (double)radius;
        for (int dy = -radius; dy <= radius; dy++) {
            double d2 = (double)dy * (double)dy;
            if (d2 > r2) continue;
            int hw = (int)std::sqrt(r2 - d2);
            SDL_RenderDrawLine(m_renderer, cx-hw, cy+dy, cx+hw, cy+dy);
        }
        int holeR = radius / 6;
        SDL_SetRenderDrawColor(m_renderer, 10, 10, 14, alpha);
        double hr2 = (double)holeR * (double)holeR;
        for (int dy = -holeR; dy <= holeR; dy++) {
            double d2 = (double)dy * (double)dy;
            if (d2 > hr2) continue;
            int hw = (int)std::sqrt(hr2 - d2);
            SDL_RenderDrawLine(m_renderer, cx-hw, cy+dy, cx+hw, cy+dy);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    if (selected) {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, pal.accent.r, pal.accent.g, pal.accent.b, alpha);
        for (int ring = 0; ring <= 1; ring++) {
            int rr = radius + 2 + ring;
            double r2  = (double)rr * (double)rr;
            double ri2 = (double)(rr-1) * (double)(rr-1);
            for (int dy = -rr; dy <= rr; dy++) {
                double d2 = (double)dy * (double)dy;
                if (d2 > r2) continue;
                int outerHW = (int)std::sqrt(r2  - d2);
                int innerHW = (d2 > ri2) ? 0 : (int)std::sqrt(ri2 - d2);
                if (outerHW > innerHW) {
                    SDL_RenderDrawLine(m_renderer, cx-outerHW, cy+dy, cx-innerHW-1, cy+dy);
                    SDL_RenderDrawLine(m_renderer, cx+innerHW+1, cy+dy, cx+outerHW, cy+dy);
                }
            }
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    }

    if (!discTexture && opacity > 0.4f) {
        std::string label = "D" + std::to_string(discIndex + 1);
        m_theme->drawTextCentered(label, cx, cy - 6,
            SDL_Color{(Uint8)(180*opacity),(Uint8)(180*opacity),(Uint8)(200*opacity),alpha},
            FontSize::TINY);
    }
}

// ─── renderDiscSelect ─────────────────────────────────────────────────────────
void InGameMenu::renderDiscSelect() {
    const auto& pal = m_theme->palette();
    int total = (int)m_discPaths.size();
    if (total == 0) return;

    int centreX = m_w / 2;
    int footerH = 56;  // always reserved — prevents layout pop when footer appears
    int centreY = (m_h - footerH) / 2;

    if (m_discPhase == DiscAnimPhase::SETTLED && m_settledCentreY == 0)
        m_settledCentreY = centreY;
    if (m_discPhase == DiscAnimPhase::SETTLED)
        centreY = m_settledCentreY;
    else
        m_settledCentreY = 0;

    int discR      = std::min(m_w / 6, m_h / 4);
    int discRSmall = (int)(discR * 0.80f);
    int coverW_large = (int)(discR * 2.4f);
    int coverH_large = (int)(discR * 3.0f);
    int fanSpacing = (int)(discR * 1.22f);

    std::string gameTitle;
    if (!m_discPaths.empty()) {
        fs::path p(m_discPaths[0]);
        std::string stem = p.stem().string();
        auto paren = stem.find(" (");
        if (paren != std::string::npos) stem = stem.substr(0, paren);
        gameTitle = stem;
    }
    m_theme->drawTextCentered("DISC SELECT", centreX, 28, pal.textSecond, FontSize::SMALL);
    m_theme->drawTextCentered(gameTitle,     centreX, 56, pal.textPrimary, FontSize::TITLE);

    if (m_coverAlpha > 0.01f) {
        int cx = centreX + (int)m_coverX;
        int cy = centreY;
        if (m_coverTexture) {
            int texW = 0, texH = 0;
            SDL_QueryTexture(m_coverTexture, nullptr, nullptr, &texW, &texH);
            if (texW > 0 && texH > 0) {
                float scale = std::min((float)coverW_large / (float)texW,
                                       (float)coverH_large / (float)texH);
                int dw = (int)(texW * scale), dh = (int)(texH * scale);
                int dx = cx - dw/2,           dy = cy - dh/2;
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, (Uint8)(150 * m_coverAlpha));
                SDL_RenderFillRect(m_renderer, &SDL_Rect{dx+10, dy+12, dw, dh});
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                SDL_SetTextureBlendMode(m_coverTexture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(m_coverTexture, (Uint8)(255 * m_coverAlpha));
                SDL_RenderCopy(m_renderer, m_coverTexture, nullptr, &SDL_Rect{dx, dy, dw, dh});
                SDL_SetTextureAlphaMod(m_coverTexture, 255);
            }
        }
    }

    bool showDiscs = (m_discPhase != DiscAnimPhase::HOLD_COVER &&
                      m_discPhase != DiscAnimPhase::SLIDE_COVER);
    if (!showDiscs) goto render_footer;

    {
        std::vector<int> drawOrder;
        for (int i = 0; i < total; i++)
            if (i != m_highlightedDisc) drawOrder.push_back(i);
        drawOrder.push_back(m_highlightedDisc);

        bool isStacked = (m_discPhase == DiscAnimPhase::HOLD_STACK);

        for (int i : drawOrder) {
            bool isSelected = (i == m_highlightedDisc);
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

            int r;
            if (isSelected) {
                r = discR;
            } else {
                float sizeEase = Ease::outCubic(m_fanProgress);
                r = discR + (int)((discRSmall - discR) * sizeEase);
            }

            float baseOpacity = isSelected ? 1.f
                              : std::max(0.32f, 1.f - std::abs(relI) * 0.20f);
            float fadeIn  = (m_discPhase == DiscAnimPhase::HOLD_STACK) ? 0.88f : 1.f;
            float opacity = baseOpacity * fadeIn;

            float spinAngle  = 0.f;
            bool  drawShadow = isSelected;

            if (m_discPhase == DiscAnimPhase::LOAD_DISC && isSelected) {
                float loadT    = std::min(1.f, m_loadTimer / DUR_LOAD);
                float loadEase = Ease::inOutQuad(loadT);
                discY     -= (int)((centreY + discR + 60) * loadEase);
                spinAngle  = m_loadSpinAngle;
                opacity    = std::max(0.f, 1.f - loadEase * 0.4f);
                drawShadow = false;
            }

            SDL_Texture* discTex = (i < (int)m_discTextures.size()) ? m_discTextures[i] : nullptr;
            renderDiscGraphic(i, discX, discY, r, opacity, isSelected, drawShadow, discTex, spinAngle);

            // Floor shadow during load animation
            if (m_discPhase == DiscAnimPhase::LOAD_DISC && isSelected) {
                float loadT    = std::min(1.f, m_loadTimer / DUR_LOAD);
                float loadEase = Ease::inOutQuad(loadT);
                int   shadowCY = centreY + discR + 14;
                int   shadowCX = settledX;
                float shadowW  = (float)discR * 1.1f * (1.f - loadEase);
                int   shadowH  = std::max(1, (int)(discR * 0.12f));
                Uint8 shadowA  = (Uint8)(80.f * (1.f - loadEase));
                if (shadowW > 1.f && shadowA > 2) {
                    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, shadowA);
                    float hw = shadowW * 0.5f, hh = (float)shadowH * 0.5f;
                    for (int dy = -(int)hh; dy <= (int)hh; dy++) {
                        float t  = (float)dy / hh;
                        float xw = hw * std::sqrt(std::max(0.f, 1.f - t*t));
                        if (xw < 0.5f) continue;
                        SDL_RenderDrawLine(m_renderer, shadowCX-(int)xw, shadowCY+dy, shadowCX+(int)xw, shadowCY+dy);
                    }
                    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                }
            }

            if (m_discPhase == DiscAnimPhase::SETTLED && isSelected) {
                std::string label = "Disc " + std::to_string(i+1);
                if (i == m_currentDisc) label += "  \xe2\x80\xa2  current";
                m_theme->drawTextCentered(label, discX, discY + discR + 26, pal.accent, FontSize::BODY);
            }
        }
    }

    render_footer:
    {
        std::string confirmHint = (m_discPhase == DiscAnimPhase::SETTLED)
            ? ((m_highlightedDisc == m_currentDisc)
                ? "Disc " + std::to_string(m_currentDisc+1) + " (current)"
                : "Load disc " + std::to_string(m_highlightedDisc+1))
            : "";  // blank during animation phases, but space is always reserved
        m_theme->drawFooterHints(m_w, m_h, confirmHint, "Back");
    }
}
