# HaackStation — Master Roadmap
*Last updated Session 36 · May 2026 · v3*

---

## Current Version: v0.5.2-dev
## Next Session: 37 — Start at item 10 (OmniSave save state copy / branch)

---

## ✅ Completed (Sessions 1–36)

- ✅ **1. PER_GAME memcard default** — `MemCardMode::PER_GAME` is the default. All games get isolated `.mcr` files. Version bumped to `v0.5.2-dev`.
- ✅ **2. OmniSave — card load action** — Card entries load correctly from left panel.
- ✅ **3. OmniSave — save state / card interaction fix** — Card saves are protected from state loads.
- ✅ **4. Build output path fix** *(partial)* — Outputs land in `build/Release/Release/`. CMake path is `build/Release/Release/` rather than the intended `build/Release/` — acceptable for now, revisit in v0.6 cleanup pass.
- ✅ **5. OmniSave — entry display names** — MCR block headers decoded, serial prefix stripped, shelf title fallback.
- ✅ **6. OmniSave — 50/50 layout fix** — Memory card and save state panels are equal-width.
- ✅ **7. OmniSave — animated SpriteCard icons** — 16×16 icons animate at ~4fps matching PS1 BIOS speed.
- ✅ **8. OmniSave — load/delete confirm dialog** — Confirm overlay before any destructive action.
- ✅ **9. OmniSave — memory card entry delete** — MCR directory frame allocation chain rewritten. Individual saves can be deleted; the card file itself is retained (correct behavior).

---

## 🔴 Immediate — Session 37 (start here)

10. **OmniSave — save state copy / branch** — "Branch from this point" — copy slot N to a new slot. Self-contained change in `omnisave_vault.cpp`.

    10.1. **OmniSave — save import** — Watched `memcards/import/` folder. Users drop `.mcr` files or zips; OmniSave detects and offers to import. Controller-navigable import screen shows parsed MCR blocks with SpriteCard icons. One-way copy operation into the active card.

    10.2. **Undo Save State (panic button)** — Before any save state write, snapshot current state to `saves/states/<game>/.undo.state`. OmniSave shows "Undo Last Save" when `.undo` slot exists. 1-step Ctrl+Z for accidental overwrites. *(Medium priority)*

    10.3. **Crash recovery resume prompt** — On game launch, if auto-save slot is newer than last clean quit timestamp, prompt "Resume from [time]? [A] Yes [B] No". Write `last_quit.txt` in `stopGame()`. *(Low priority — auto-save slot already exists)*

11. **Accounts tab (RA + ScreenScraper)** — Rename current RA tab to "Accounts." ScreenScraper credentials at top, RA credentials below. OSK already built and stable — wire to both text fields.

12. **RA unlock timestamps** — `AchievementInfo::unlockTime` and display logic ("Today" / "Yesterday" / "N days ago" / date string). Verify rendering in Trophy Vault grid cards.

13. **RA game-load notification toggle** — Settings toggle: "Show login confirmation on every game load" (default on).

14. **Hardcore/Softcore badge in Trophy Vault** — Store hardcore flag in `AchievementInfo`. Show small "HC" badge on hardcore unlocks in Trophy Vault grid.

    14a. **RA feature toggle** — Single `raEnabled` bool in Settings. When off: hides Trophy Hub from HaackStation Hub, collapses trophy row in Game Panel. Default on.

---

## v0.6 — HaackStation Hub + Tools + Music + Library Polish

### HaackStation Hub
*Replaces the current Trophy button on the shelf. One button, always accessible, not game-specific.*

The Hub contains:
- **Trophy Hub** — existing per-game and all-games RA trophy views
- **OmniSave Card Shelf** — see item 15
- **Game Collections** — see item 16
- **Play History** — session log, hours per game
- **Profile switcher** — placeholder until Eden (v0.8)

15. **OmniSave Card Shelf** — Grid of tiles, one per game with save data. Each tile shows animated SpriteCard icon, game title, memcard block count, save state count. Selecting a tile enters that game's OmniSave Vault. New files: `omnisave_card_shelf.h/.cpp`, `AppState::OMNISAVE_CARD_SHELF`. *When accessed in-game, skip the Card Shelf and go straight to that game's Vault.*

    15a. **OmniSave — keybind consistency pass** — Right panel (save states): move Save Here from X to Y/Triangle, move Delete to X. X always = Delete, A always = primary action. *(Polish)*

    15b. **OmniSave — Card Time Machine restore UI** — Snapshot data is accumulating as of v0.5.1. Restore screen: third panel or Y/Triangle from memcard panel. Shows timestamped `.mcr` snapshots with paired game frame thumbnails. Select + A = restore (atomic copy, flush + reload core SRAM). Always snapshots current card before restore. *(High priority — data already collecting)*

    15c. **OmniSave — memory card rename / description (OSK)** — Y/Triangle in memcard panel opens OSK overlay. Saves display name + optional note to `memcards/per_game/<SERIAL>_1.meta.json`. *(Medium priority — OSK already built)*

16. **Game Collections** — User-curated named lists of games. Lightweight JSON. Lives in the Hub. Auto-collections from scraped genre metadata as opt-in toggle.

17. **Shelf show/hide control** — Show or hide individual shelves from Hub or Settings.

### Tools Section
*Scraping actions move out of Settings entirely. Settings keeps only credential fields.*

```
Tools
├── Library
│   ├── Scrape Game Art
│   ├── Manual Scrape
│   ├── Convert Games to CHD     (chdman integration)
│   ├── Verify ROM Files
│   └── Clean Library
├── Saves
│   ├── Backup All Saves
│   └── Restore Save Backup
└── System
    ├── Download Tools           (chdman + future tools)
    ├── Download Translation Model
    ├── Export Settings
    ├── Import Settings
    └── View Session Log
```

18. **chdman integration** — Wire chdman into Tools → Convert Games to CHD. In-app progress bar. No visible console window. Auto-download from official MAME release.

19. **Game Details Panel enhancements** — Add release year, developer, rating, genre from ScreenScraper.

    19a. **Genre-themed placeholder covers** — Bundled PS1 serial-to-genre lookup table (~200–300KB). Genre-appropriate illustrated cover with game name when no scraped art exists. Scraping replaces placeholder automatically.

    19b. **Controller-navigable folder browser (FolderBrowser component)** — Reusable D-pad file/folder selector. Used for ROM path selection, save backup destination, import folder, future path needs.

20. **Video Previews + Persistent Game Panel Mode** — Two independent toggles: video preview (autoplays scraped clip after hover delay) and always-on panel (split-view shelf layout). Requires shelf layout manager.

21. **Game time tracking** — Hours played per game. Show on shelf card. Persist in JSON.

22. **Search / filter** — Type-to-search via OSK or keyboard. Filter by region, genre once scraped.

23. **Ambient Music Player** — `music/<Game Name>/track.ogg`, `music/_frontend/ambient.ogg`. Crossfades on navigation. Now-playing bar in footer. Isolated `MusicPlayer` module.

24. **Auto-scrolling screenshots** — Timer-based auto-advance in game details panel.

25. **FPS display overlay** — Toggle hotkey or in-game menu option.

26. **Run-Ahead** — Reduces input lag. Wire to per-game settings. Beetle supports via libretro.

27. **Widescreen hacks + CPU overclock** — Core options, wire to per-game settings.

28. **Overscan control** — Crop black borders, per-game setting.

29. **Bilinear filtering toggle** — Per-game setting.

30. **Aspect ratio options** — 4:3, 16:9 stretched, pixel-perfect integer scaling.

31. **Cheat code manager** — GameShark/Action Replay codes. Beetle supports via libretro cheat interface.

32. **Custom trophy unlock sound** — WAV/OGG on achievement unlock. User-replaceable in `assets/`.

33. **In-game Trophy Room access** — "Trophies" option in in-game menu. Pauses core, opens Trophy Room.

34. **Spinning CD case in game details panel** — Flat front/back case with spinning disc. Optional 3D jewel case toggle.

35. **Bookmark slide animation** — `float m_bookmarkAnim` per game entry. Slides on favorite/unfavorite.

### v0.6 Milestone
- [ ] Update GitHub README with Hub navigation, new features, fresh screenshots

---

## v0.6 — Game Manuals + Strategy Guide Viewer
*Added Session 35 · May 2026*

**S37 target — Game Manuals (PDF via ScreenScraper + MuPDF viewer)**
- ScreenScraper `manuel` media type in `game_scraper.cpp`
- Opt-in checkbox in `scrape_screen.cpp`
- "Manual" button in `game_details_panel.cpp`
- New `ManualViewer` class (`manual_viewer.h/.cpp`) wrapping MuPDF (C API, BSD/AGPL)
- Controller nav: L1/R1 page-turn, B close

**S38 target — Strategy Guide infrastructure + text viewer**
- `ps1_guide_index.json` (ships with app, generated offline)
- `GuideManager` class (index lookup, download, cache to `guides/<Serial>/`)
- Guide picker in Game Details Panel
- Plain-text viewer with D-pad scroll, L1/R1 page-turn, bookmarks

**S39 target — In-game Guide + ToC + search**
- "Guide" item in in-game menu (only when guide downloaded for current game)
- Table-of-contents parser (ASCII `===`/`---` section markers)
- In-guide search via OSK
- Full bookmark system (`guides/<Serial>/<Guide>.bookmarks.json`)

**Controller nav summary:**

| Action | Control |
|--------|---------|
| Open Manual / Guide | A on button in Game Details Panel |
| Open Guide (in-game) | A on "Guide" in in-game menu |
| Scroll line | D-pad Up / Down |
| Scroll page | L1 / R1 |
| Table of contents | Triangle (Y) |
| Search | Square (X) → OSK |
| Bookmark toggle | Select |
| Jump bookmark | L2 / R2 |
| Close viewer | B |

---

## v0.7 — Hardware Renderer + Visual Enhancements

36. **OpenGL hardware renderer** — Set up OpenGL context, handle Beetle HW render callbacks. Gateway for items 37–41.
37. **Internal resolution upscaling** — 2x, 4x, 8x, 16x. Per-game setting. Requires item 36.
38. **DPI scaling** — `uiScale` float in `HaackSettings`, single pass through all rendering constants.
39. **Post-processing shader browser (HaackShaders)** — Browser UI, live preview, per-game assignment. Requires hardware renderer.
40. **Texture replacement (HaackTextures)** — Requires hardware renderer.
41. **PGXP + geometry fixes** — Requires hardware renderer. Per-game setting.
42. **Smart compatibility mode** — Per-game toggle that disables known-problematic enhancements.
43. **Input remap screen redesign** — Controller SVG dominant left (~55%), scrollable bindings list right.
44. **Zero-config / HLE BIOS** — Biggest new-user barrier removed.

### v0.7 Milestone
- [ ] Update GitHub README with hardware renderer screenshots, upscaling examples

---

## v0.8 — Eden Profiles + Cloud + AI

45. **Eden — User Profile System** *(named for the developer's wife)*
    - Each user lives in an isolated "Garden" — saves, memcards, states, screenshots, trophies, theme, controller config
    - Profile selector on launch, optional PIN lock
    - Per-profile game filter + child account mode
    - **Critical prerequisite:** all path strings must route through `ProfileManager` — no hardcoded paths

46. **Cloud save state sync** — Google Drive API (free, cross-platform). Hooks into OmniSave architecture.
47. **Local AI translation overlay** — Configurable hotkey, local LLM (Mistral 7B / Gemma 2B), translation overlay without leaving game.
48. **AI texture upscaling (NCNN)** — Real-time texture upscaling. Requires hardware renderer.
49. **Rewind — delta compression** — Store only frame deltas. Expected 3–4x more rewind time from same buffer.
50. **Session Timeline** — Auto-saves full state every 60–90 seconds during play. Visual browsable strip.
51. **Achievement-led discovery** — Query RA API for incomplete games, suggest based on play history. Lives in Hub.

### v0.8 Milestone
- [ ] Update GitHub README with Eden profiles, cloud sync, AI translation

---

## v0.9 — Platform Expansion + Netplay

52. **Netplay** — Rollback netplay via libretro netpacket API.
53. **Android / Ayn Thor build** — CMake Android NDK, SDL2 for Android, touch input, APK packaging.
54. **Mac / Linux builds** — SDL2, C++17, CMake all cross-platform. `dlopen` bridge path already exists.
    54.1. **iOS: separate port from Mac** — Requires separate Xcode target, Metal/OpenGL ES. Do Android first.
55. **Multi-system support** — Point libretro bridge at other cores: PS2, PSP, N64, SNES, etc.
56. **PBP multi-disc support** — PSP format PS1 releases. Most relevant after Android build.
57. **Vulkan renderer** — Additional backend after OpenGL is stable.

### v0.9 Milestone
- [ ] Update GitHub README with platform support matrix, Android screenshots

---

## v1.0 — Polish Pass

58. **Input remap screen — Option B** — Lines radiating from centred controller image to button label chips.
59. **Color themes** — Full theme system. HaackStation ships with a default; user-selectable.
60. **File/folder housekeeping** — Move `scrape_screen`, `per_game_settings_screen`, `remap_screen` into `ui/` subfolder. Update all includes + CMakeLists.
61. **Full attribution / credits pass** — Kenny icons, libchdr, rcheevos, SoundTouch, SDL2, SDL2_ttf, SDL2_image, LLM weights, chdman (MAME/GPL).
62. **RetroAchievements official recognition** — Reapply ~July 2026 targeting v1.0 milestone.

### v1.0 Milestone
- [ ] Final README pass — full feature list, screenshots, attribution, license
- [ ] Tag v1.0 release on GitHub

---

## Standing Bugs / Minor Polish *(fix opportunistically)*

- **Disc swap LiveCard toast** — False positives on disc swap still firing. Root cause: `OmniSaveVault` `loadSaveRAM` callback fires after checksum is captured, invalidating the baseline. Will be fully resolved when disc swap moves to open-tray/close-tray (no core reload). Workaround: `m_sramSettleUntil` 3.5s cooldown suppresses most cases.
- **RA notification first launch** — Occasionally doesn't appear. Timing issue from `loadSaveRAM()` running immediately after `m_ra->loadGame()`. Non-critical, monitor.
- **Build output path** — Currently `build/Release/Release/` instead of target `build/Release/`. Revisit in v0.6 cleanup pass.
- **OmniSave footer label** — Verify correct button hint is showing.
- **Trophy Hub footer** — Verify "Powered by RetroAchievements" is rendering.
- **Trophy auto-screenshot timing** — If screenshots look partially slid-in, bump `m_trophyShotCountdown` from 4 to 6–8.

---

## Phase Summary

| Version | Focus |
|---------|-------|
| v0.5.x | OmniSave completion, Accounts tab, RA polish *(current)* |
| v0.6 | HaackStation Hub, Collections, Tools, Music, Manuals, Guide Viewer |
| v0.7 | Hardware renderer, upscaling, shaders, textures, PGXP |
| v0.8 | Eden profiles, cloud saves, AI translation, AI upscaling, rewind++ |
| v0.9 | Netplay, Android/Ayn Thor, Mac/Linux, multi-system |
| v1.0 | Polish, credits, RA recognition, final release |

---
*Next session starts at item 10 (OmniSave save state copy / branch). Commit after every working change. Update GitHub README at the end of every version milestone.*
