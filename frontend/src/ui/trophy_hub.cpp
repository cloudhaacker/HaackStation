#include "trophy_hub.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
namespace fs = std::filesystem;

// Minimal JSON helpers (no external dependency)
// We write a simple array-of-objects by hand and parse with basic string ops.

// ─── Construction / Destruction ───────────────────────────────────────────────
TrophyHub::TrophyHub(SDL_Renderer* renderer, ThemeEngine* theme, ControllerNav* nav)
    : m_renderer(renderer), m_theme(theme), m_nav(nav)
{
    SDL_GetRendererOutputSize(renderer, &m_w, &m_h);
}

TrophyHub::~TrophyHub() {
    freeTextures();
}

// ─── refresh ──────────────────────────────────────────────────────────────────
void TrophyHub::refresh() {
    freeTextures();
    m_games.clear();
    m_selectedIdx  = 0;
    m_scrollOffset = 0;
    loadFromDisk();
    // Sort: alphabetical by game title (matches game shelf order)
    std::sort(m_games.begin(), m_games.end(),
        [](const GameTrophySummary& a, const GameTrophySummary& b) {
            return a.gameTitle < b.gameTitle;
        });
    loadTextures();
    std::cout << "[TrophyHub] Loaded " << m_games.size() << " game summaries\n";
}

// ─── updateGame ───────────────────────────────────────────────────────────────
void TrophyHub::updateGame(const GameTrophySummary& summary) {
    for (auto& g : m_games) {
        if (g.gameId == summary.gameId) {
            // Preserve loaded textures, update data
            g.gameTitle         = summary.gameTitle;
            g.coverPath         = summary.coverPath;
            g.unlocked          = summary.unlocked;
            g.total             = summary.total;
            g.totalPoints       = summary.totalPoints;
            g.possiblePoints    = summary.possiblePoints;
            g.recentBadgePaths  = summary.recentBadgePaths;
            saveToDisk();
            return;
        }
    }
    m_games.push_back(summary);
    saveToDisk();
}

// ─── Texture loading ──────────────────────────────────────────────────────────
void TrophyHub::freeTextures() {
    for (auto& g : m_games) {
        if (g.coverTex) { SDL_DestroyTexture(g.coverTex); g.coverTex = nullptr; }
        for (auto* t : g.badgeTextures) if (t) SDL_DestroyTexture(t);
        g.badgeTextures.clear();
    }
}

void TrophyHub::loadTextures() {
    for (auto& g : m_games) {
        // Cover art
        if (!g.coverPath.empty() && fs::exists(g.coverPath) && !g.coverTex) {
            g.coverTex = IMG_LoadTexture(m_renderer, g.coverPath.c_str());
            if (g.coverTex) SDL_SetTextureBlendMode(g.coverTex, SDL_BLENDMODE_BLEND);
        }
        // Recent unlock badges
        g.badgeTextures.clear();
        for (const auto& path : g.recentBadgePaths) {
            SDL_Texture* t = nullptr;
            if (!path.empty() && fs::exists(path)) {
                t = IMG_LoadTexture(m_renderer, path.c_str());
                if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            }
            g.badgeTextures.push_back(t);
        }
    }
}

// ─── Persistence ──────────────────────────────────────────────────────────────
// Minimal hand-rolled JSON — avoids adding a JSON library dependency.

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

void TrophyHub::saveToDisk() const {
    fs::create_directories("saves");
    std::ofstream f(SAVE_PATH);
    if (!f) { std::cerr << "[TrophyHub] Cannot write " << SAVE_PATH << "\n"; return; }

    f << "[\n";
    for (size_t gi = 0; gi < m_games.size(); ++gi) {
        const auto& g = m_games[gi];
        f << "  {\n";
        f << "    \"gameId\": " << g.gameId << ",\n";
        f << "    \"gameTitle\": \"" << jsonEscape(g.gameTitle) << "\",\n";
        f << "    \"coverPath\": \"" << jsonEscape(g.coverPath) << "\",\n";
        f << "    \"unlocked\": " << g.unlocked << ",\n";
        f << "    \"total\": " << g.total << ",\n";
        f << "    \"totalPoints\": " << g.totalPoints << ",\n";
        f << "    \"possiblePoints\": " << g.possiblePoints << ",\n";
        f << "    \"recentBadgePaths\": [";
        for (size_t i = 0; i < g.recentBadgePaths.size(); ++i) {
            f << "\"" << jsonEscape(g.recentBadgePaths[i]) << "\"";
            if (i + 1 < g.recentBadgePaths.size()) f << ", ";
        }
        f << "]\n";
        f << "  }";
        if (gi + 1 < m_games.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

// Very simple JSON parser — reads only the fields we wrote.
static std::string extractJsonString(const std::string& json,
                                      const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char nc = json[++pos];
            if (nc == '"') val += '"';
            else if (nc == '\\') val += '\\';
            else if (nc == 'n') val += '\n';
            else val += nc;
        } else {
            val += json[pos];
        }
        ++pos;
    }
    return val;
}

static int extractJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    int val = 0;
    bool neg = false;
    if (pos < json.size() && json[pos] == '-') { neg = true; ++pos; }
    while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
        val = val * 10 + (json[pos++] - '0');
    return neg ? -val : val;
}

static std::vector<std::string> extractJsonStringArray(
    const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\": [";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return result;
    pos += search.size();
    // collect until closing ]
    while (pos < json.size() && json[pos] != ']') {
        if (json[pos] == '"') {
            ++pos;
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    char nc = json[++pos];
                    if (nc == '"') val += '"';
                    else val += nc;
                } else { val += json[pos]; }
                ++pos;
            }
            result.push_back(val);
        }
        ++pos;
    }
    return result;
}

void TrophyHub::loadFromDisk() {
    if (!fs::exists(SAVE_PATH)) return;
    std::ifstream f(SAVE_PATH);
    if (!f) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Split on object boundaries {  }
    size_t pos = 0;
    while (pos < content.size()) {
        size_t start = content.find('{', pos);
        if (start == std::string::npos) break;
        size_t end = content.find('}', start);
        if (end == std::string::npos) break;
        std::string obj = content.substr(start, end - start + 1);

        GameTrophySummary g;
        g.gameId         = (uint32_t)extractJsonInt(obj, "gameId");
        g.gameTitle      = extractJsonString(obj, "gameTitle");
        g.coverPath      = extractJsonString(obj, "coverPath");
        g.unlocked       = extractJsonInt(obj, "unlocked");
        g.total          = extractJsonInt(obj, "total");
        g.totalPoints    = (uint32_t)extractJsonInt(obj, "totalPoints");
        g.possiblePoints = (uint32_t)extractJsonInt(obj, "possiblePoints");
        g.recentBadgePaths = extractJsonStringArray(obj, "recentBadgePaths");

        if (g.gameId > 0 && !g.gameTitle.empty())
            m_games.push_back(std::move(g));

        pos = end + 1;
    }
}

// ─── Input ────────────────────────────────────────────────────────────────────
void TrophyHub::handleEvent(const SDL_Event& e) {
    NavAction action = m_nav->processEvent(e);
    if (action == NavAction::NONE) return;

    if (action == NavAction::BACK) {
        m_wantsClose = true;
        return;
    }
    if (action == NavAction::UP) {
        if (m_selectedIdx > 0) --m_selectedIdx;
        else m_nav->cancelHeld();
    } else if (action == NavAction::DOWN) {
        if (m_selectedIdx < (int)m_games.size() - 1) ++m_selectedIdx;
        else m_nav->cancelHeld();
    } else if (action == NavAction::CONFIRM) {
        if (m_selectedIdx >= 0 && m_selectedIdx < (int)m_games.size()) {
            const auto& g = m_games[m_selectedIdx];
            if (m_onViewGame) m_onViewGame(g.gameId, g.gameTitle);
        }
    }
    clampScroll();
}

void TrophyHub::update(float deltaMs) {
    NavAction held = m_nav->updateHeld(SDL_GetTicks());
    if (held == NavAction::UP) {
        if (m_selectedIdx > 0) { --m_selectedIdx; clampScroll(); }
        else m_nav->cancelHeld();
    } else if (held == NavAction::DOWN) {
        if (m_selectedIdx < (int)m_games.size() - 1) { ++m_selectedIdx; clampScroll(); }
        else m_nav->cancelHeld();
    }
}

void TrophyHub::clampScroll() {
    // Ensure selected row is visible
    int rowTotalH = ROW_H + ROW_PAD;
    int visibleH  = m_h - HEADER_H - 40;   // 40px footer hint
    int selectedY = m_selectedIdx * rowTotalH - m_scrollOffset;

    if (selectedY < 0)
        m_scrollOffset = m_selectedIdx * rowTotalH;
    else if (selectedY + ROW_H > visibleH)
        m_scrollOffset = m_selectedIdx * rowTotalH - visibleH + ROW_H;

    int maxScroll = std::max(0, (int)m_games.size() * rowTotalH - visibleH);
    m_scrollOffset = std::max(0, std::min(m_scrollOffset, maxScroll));
}

// ─── Rendering ────────────────────────────────────────────────────────────────
void TrophyHub::render() {
    const auto& pal = m_theme->palette();

    // Background
    SDL_SetRenderDrawColor(m_renderer, pal.bg.r, pal.bg.g, pal.bg.b, 255);
    SDL_RenderClear(m_renderer);

    renderHeader();
    renderGlobalBar();
    renderRows();

    // Footer hint
    m_theme->drawText("A: view trophies    B: back",
        MARGIN, m_h - 28, pal.textDisable, FontSize::SMALL);
}

void TrophyHub::renderHeader() {
    const auto& pal = m_theme->palette();
    SDL_Color gold = { 255, 215, 0, 255 };

    m_theme->drawText("\xF0\x9F\x8F\x86  TROPHY HUB",
        MARGIN, 20, gold, FontSize::SMALL);

    // Global totals (top right)
    int totalUnlocked = 0, totalAch = 0;
    for (const auto& g : m_games) { totalUnlocked += g.unlocked; totalAch += g.total; }
    if (totalAch > 0) {
        std::string totStr = "Total: " + std::to_string(totalUnlocked)
            + " / " + std::to_string(totalAch);
        m_theme->drawText(totStr, m_w - MARGIN - 200, 20, pal.textPrimary, FontSize::BODY);
    }

    // Games count subtitle
    std::string sub = std::to_string(m_games.size()) + " game"
        + (m_games.size() == 1 ? "" : "s") + " with RetroAchievements";
    m_theme->drawText(sub, MARGIN, 48, pal.textSecond, FontSize::BODY);
}

void TrophyHub::renderGlobalBar() {
    const auto& pal = m_theme->palette();

    int totalUnlocked = 0, totalAch = 0;
    for (const auto& g : m_games) { totalUnlocked += g.unlocked; totalAch += g.total; }

    int barY  = 76;
    int barX  = MARGIN;
    int barW  = m_w - MARGIN * 2 - 60;
    int barH  = 12;

    float frac = (totalAch > 0) ? (float)totalUnlocked / totalAch : 0.f;
    SDL_Color goldFill = { 255, 200, 30, 255 };
    SDL_Color barBg    = { 50, 50, 60, 255 };
    renderProgressBar(barX, barY, barW, barH, frac, goldFill, barBg);

    int pct = (int)(frac * 100.f + 0.5f);
    std::string pctStr = std::to_string(pct) + "%";
    m_theme->drawText(pctStr, barX + barW + 8, barY - 2, pal.textSecond, FontSize::TINY);

    m_theme->drawLine(MARGIN, HEADER_H - 8, m_w - MARGIN, HEADER_H - 8, pal.gridLine);
}

void TrophyHub::renderRows() {
    if (m_games.empty()) {
        const auto& pal = m_theme->palette();
        m_theme->drawText("No trophy data yet. Play a game with RetroAchievements enabled.",
            MARGIN, HEADER_H + 40, pal.textSecond, FontSize::BODY);
        return;
    }

    int rowTotalH = ROW_H + ROW_PAD;
    int rowW      = m_w - MARGIN * 2;
    int startY    = HEADER_H - m_scrollOffset;

    // Set clipping rect to the list area
    SDL_Rect clip = { 0, HEADER_H, m_w, m_h - HEADER_H - 40 };
    SDL_RenderSetClipRect(m_renderer, &clip);

    for (int i = 0; i < (int)m_games.size(); ++i) {
        int rowY = startY + i * rowTotalH;
        if (rowY + ROW_H < HEADER_H) continue;
        if (rowY > m_h - 40)          break;
        renderRow(m_games[i], MARGIN, rowY, rowW, i == m_selectedIdx);
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);
}

void TrophyHub::renderRow(const GameTrophySummary& g,
                            int x, int y, int rowW, bool selected) {
    const auto& pal = m_theme->palette();
    SDL_Color gold  = { 255, 215, 0, 255 };

    // Row background
    SDL_Color bg = selected ? pal.bgCard : SDL_Color{ (Uint8)(pal.bg.r + 8),
                                                        (Uint8)(pal.bg.g + 8),
                                                        (Uint8)(pal.bg.b + 12), 255 };
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, 255);
    SDL_Rect rowRect = { x, y, rowW, ROW_H };
    SDL_RenderFillRect(m_renderer, &rowRect);

    // Selected highlight border
    if (selected) {
        SDL_SetRenderDrawColor(m_renderer, gold.r, gold.g, gold.b, 200);
        SDL_Rect border = { x, y, 4, ROW_H };
        SDL_RenderFillRect(m_renderer, &border);
    }
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    int innerX = x + 8;

    // Cover art
    if (g.coverTex) {
        SDL_Rect coverDst = { innerX, y + (ROW_H - COVER_H) / 2, COVER_W, COVER_H };
        SDL_RenderCopy(m_renderer, g.coverTex, nullptr, &coverDst);
    } else {
        // Placeholder box
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 65, 255);
        SDL_Rect coverBox = { innerX, y + (ROW_H - COVER_H) / 2, COVER_W, COVER_H };
        SDL_RenderFillRect(m_renderer, &coverBox);
    }

    int textX = innerX + COVER_W + 14;
    int textY = y + 12;

    // Game title
    SDL_Color titleColor = selected ? gold : pal.textPrimary;
    m_theme->drawText(g.gameTitle, textX, textY, titleColor, FontSize::BODY);

    // Progress bar
    int barW = 180;
    int barH = 8;
    int barX = textX;
    int barY = textY + 28;
    float frac = (g.total > 0) ? (float)g.unlocked / g.total : 0.f;
    SDL_Color fillColor = (frac >= 1.f)
        ? SDL_Color{ 255, 215, 0, 255 }   // gold for mastery
        : SDL_Color{ 80, 160, 255, 255 }; // blue for in-progress
    SDL_Color barBg = { 40, 40, 55, 255 };
    renderProgressBar(barX, barY, barW, barH, frac, fillColor, barBg);

    // Count text
    std::string countStr = std::to_string(g.unlocked) + " / " + std::to_string(g.total);
    int pct = (g.total > 0) ? (int)(frac * 100.f + 0.5f) : 0;
    countStr += "  (" + std::to_string(pct) + "%)";
    m_theme->drawText(countStr, barX + barW + 10, barY - 2, pal.textSecond, FontSize::TINY);

    // Points
    if (g.possiblePoints > 0) {
        std::string pts = std::to_string(g.totalPoints) + " / "
            + std::to_string(g.possiblePoints) + " pts";
        m_theme->drawText(pts, textX, barY + 14, pal.textDisable, FontSize::TINY);
    }

    // Recent badge strip (up to 5)
    int badgeX = textX;
    int badgeY = textY + 56;
    for (size_t i = 0; i < g.badgeTextures.size() && i < 5; ++i) {
        if (g.badgeTextures[i]) {
            SDL_Rect dst = { badgeX, badgeY, BADGE_SIZE, BADGE_SIZE };
            SDL_RenderCopy(m_renderer, g.badgeTextures[i], nullptr, &dst);
            badgeX += BADGE_SIZE + 4;
        }
    }

    // Mastery crown if 100%
    if (g.total > 0 && g.unlocked == g.total) {
        m_theme->drawText("\xF0\x9F\x91\x91 MASTERED",
            m_w - MARGIN - 120, y + 38, gold, FontSize::TINY);
    }
}

void TrophyHub::renderProgressBar(int x, int y, int w, int h,
                                   float fraction, SDL_Color fill, SDL_Color bg) {
    // Background track
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect track = { x, y, w, h };
    SDL_RenderFillRect(m_renderer, &track);

    // Fill
    int fillW = (int)(w * std::max(0.f, std::min(1.f, fraction)));
    if (fillW > 0) {
        SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_Rect filled = { x, y, fillW, h };
        SDL_RenderFillRect(m_renderer, &filled);
    }
}
