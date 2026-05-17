# HaackStation — Session 20 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.0-dev**
Windows build — MSVC via CMake. Project root: `C:\Users\digit\Documents\HaackStation\`
Source in `frontend/src/`, headers in `frontend/include/`.

---

## Feature Names (branding finalised)
- **HaackStack with GhostScan** — multi-disc library manager + deduplication
- **OmniSave & SpriteCard** — save state manager + animated memory card sprite viewer
- **Trophy Vault** (per-game grid) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement feature
- Trophy Hub footer should say "Powered by RetroAchievements"

---

## Session 20 Status — What Was Completed

### CHD Hashing ✅ FULLY WORKING
After a long debugging session, CHD hashing via libchdr is now working correctly.
Achievements unlock for both CHD and BIN/CUE files.

**Root causes fixed (in order of discovery):**

1. `RC_CLIENT_SUPPORTS_HASH=1` was missing from CMakeLists.txt — the function
   `rc_client_begin_identify_and_load_game` is gated behind this define in
   `deps/rcheevos/include/rc_client.h`. Added to both `target_compile_definitions`
   and `set_source_files_properties` for rcheevos sources.

2. `ra_manager.h` — old header was never replaced (Session 19 leftover). New header
   with all per-game map members is now in place. ✅

3. CHD cdreader crashing on CUE files — the custom `open_track_iterator` was
   forwarding `rc_hash_iterator*` to the default reader which crashed. Fixed with a
   **magic-tagged handle** approach (`CHD_HANDLE_MAGIC = 0xC4DC4FE0u` as first member
   of `ChdTrackHandle`). `read_sector`, `close_track`, and `first_track_sector` all
   check the magic tag and delegate non-CHD handles to `s_defaultCdReader`.

4. Wrong hunk size calculation — CHD stores sectors with 96-byte subchannel appended,
   making raw frame size 2448, not 2352 (`CD_FRAME_SIZE`). Fixed by using
   `hdr->unitbytes` instead of `CD_FRAME_SIZE` to compute `hunkSize`.

5. Wrong sector skip — `MODE2_RAW` tracks (used by most PS1 CHDs) have a 24-byte
   header (12 sync + 4 addr + 8 subheader) before the 2048-byte payload. `sectorSkip`
   must be 24 for `MODE2_RAW`. A stray `h->sectorSkip = 0` line was overwriting the
   correctly-calculated value — removing it fixed sector reads and hashing succeeded.

**Final sector type table in `chd_open_track`:**
```
MODE2_RAW   → sectorSize=2048, sectorSkip=24
MODE1_RAW   → sectorSize=2048, sectorSkip=16
MODE1       → sectorSize=2048, sectorSkip=0
MODE2_FORM1 → sectorSize=2048, sectorSkip=24
MODE2       → sectorSize=2336, sectorSkip=16
AUDIO       → sectorSize=2352, sectorSkip=0
```

---

## Files Modified This Session

| File | Location | Status |
|------|----------|--------|
| `ra_manager.h` | `frontend/include/` | ✅ new version with per-game maps |
| `ra_manager.cpp` | `frontend/src/` | ✅ CHD cdreader fully working, debug lines removed |
| `frontend/CMakeLists.txt` | `frontend/` | ✅ `RC_CLIENT_SUPPORTS_HASH=1` added |

---

## Git — Commit This Session's Work

```
git add frontend/src/ra_manager.cpp frontend/include/ra_manager.h
git add frontend/CMakeLists.txt
git commit -m "fix(ra): CHD hashing working via libchdr cdreader

- Add RC_CLIENT_SUPPORTS_HASH=1 to CMakeLists (gates begin_identify_and_load_game)
- Replace ra_manager.h with per-game cache maps (m_cachedAchievementsMap, etc.)
- Magic-tagged ChdTrackHandle (0xC4DC4FE0) distinguishes CHD vs default reader handles
- read_sector/close_track/first_track_sector delegate non-CHD handles to s_defaultCdReader
- Fix hunk size: use hdr->unitbytes (2448 w/subchannel) not CD_FRAME_SIZE (2352)
- Fix sector skip: MODE2_RAW needs sectorSkip=24 to skip 24-byte raw sector header
- Remove stray h->sectorSkip=0 that was overwriting correct targetSectorSkip
- BIN/CUE and CHD both hash and unlock achievements correctly"

git push
```

---

## Next Priorities (Session 21+)

1. **RA notification badge image** — `UnlockNotification` has `badgeLocalPath` populated
   but `RAManager::render()` draws a coloured rect placeholder. Need to load texture
   from `badgeLocalPath` on first render and blit it. Self-contained fix in `render()`.

2. **L1/R1 screenshot cycling from description/trophy row** — minor, one condition fix
   in `game_details_panel.cpp` handleEvent L1/R1 block. Logged in Session 19.

3. **Saves System (OmniSave & SpriteCard)** — next major feature.

4. **Trophy Room visual pass** — RA unlock timestamps (need `unlockedAt` field in
   `AchievementInfo`, populated from `rc_client_achievement_t::unlock_time`), badge
   notification images, "Powered by RetroAchievements" attribution in Trophy Hub footer.

5. **chdman integration** — download/integrate CHD conversion tool into HaackStation
   tools section (CHD hashing being confirmed working unlocks this).

---

## Architecture Notes for Fresh Claude

### CHD cdreader design
`ra_manager.cpp` lines 31–260 approx. Key points:
- `s_defaultCdReader` saved at global scope (before `#ifdef HAVE_LIBCHDR`) so all
  functions can see it.
- `chd_open_track_iterator`: CHD files → `chd_open_track()` returning `ChdTrackHandle*`.
  Non-CHD → `s_defaultCdReader.open_track_iterator()` returning rcheevos internal handle.
- `chd_read_sector` / `chd_close_track` / `chd_first_track_sector`: check magic tag
  first. Our handle → CHD path. Not our handle → delegate to `s_defaultCdReader`.
- `registerChdCdReader()`: called once in `RAManager::initialize()` after
  `rc_client_create()`.

### Per-game cache maps
`ra_manager.h` — keyed by RA gameId (`uint32_t`):
- `m_cachedAchievementsMap` — `std::map<uint32_t, vector<AchievementInfo>>`
- `m_cachedGameInfoMap` — `std::map<uint32_t, RAGameInfo>`
- `m_lastGameId` — most recently loaded game
- `m_pathToGameId` — ROM path → gameId reverse lookup
- `m_loadingPath` — set before async load for callback to record path→gameId

Persistence: `saves/ra_achievements_<gameId>.json` written on load + each unlock.
`loadAllCachedGamesFromDisk()` called from constructor on startup.

---

## Build Instructions
From `C:\Users\digit\Documents\HaackStation\` in Command Prompt:
```
cmake --build build --config Release
```
For a clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```
CMake configure should show:
```
-- libchdr found — CHD hashing enabled
-- rcheevos sources: C:/... (long list of .c files)
```



---

Session 21 Handoff  
Version: 0.5.1-dev  
  
  Completed this session:

Trophy auto-screenshot (ra_auto_screenshot toggle in Emulation settings). Capture fires from app.cpp before SDL_RenderPresent, 4-frame countdown for slide animation. Files: ra_manager.cpp/h, app.cpp
[VOID] achievement suppression fixed (was showing on every Alundra load)
Game-load RA icon URL fixed — uses badge_name not numeric ID. Added badgeName to RAGameInfo
Notification panel edge-flush + clip rect for text overflow
Trophy Hub alphabetical sort
L1/R1 screenshot cycle from any nav state in Details Panel
RA auto-screenshot toggle added to Emulation settings tab. settings_screen.h/cpp, settings_manager.cpp

Screenshot timing note: Countdown is currently 4 frames. If notifications still look partially slid in, bump m_trophyShotCountdown = 4 to 6 or 8 in handleEvent. Easy one-line change.
Next priorities:

Dedicated RetroAchievements settings tab (in-app login — username/password entry, no more config-file-only auth). Needs on-screen keyboard first or can do a simplified text entry field.
OmniSave & SpriteCard — save state manager
RA unlock timestamps in Trophy Vault (unlockedAt from rc_client_achievement_t::unlock_time)
chdman integration

---

# HaackStation — Session 22 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.1-dev**
Windows build — MSVC via CMake. Project root: `C:\Users\digit\Documents\HaackStation\`
Source in `frontend/src/`, headers in `frontend/include/`.

---

## Feature Names (branding finalised)
- **HaackStack with GhostScan** — multi-disc library manager + deduplication
- **OmniSave & SpriteCard** — save state manager + animated memory card sprite viewer
- **Trophy Vault** (per-game grid) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement feature
- Trophy Hub footer should say "Powered by RetroAchievements"

---

## Session 21 Recap (completed last session)
- Trophy auto-screenshot fixed: switched from 4-frame countdown to `SDL_GetTicks()`-based
  400ms delay so screenshot fires after slide-in animation completes.
  Key fix was adding `m_trophyShotCountdown = 0` reset after capture to prevent
  repeated firing (was producing 31 screenshots per trophy pop).
- Version string updated: `settings_screen.cpp` Settings > About tab now shows `0.5.1-dev`
  (was `0.1.0-dev`). This is the only place to change the version display.

---

## Session 22 — What Was Completed

### OmniSave Vault — NEW FEATURE (awaiting first test)

Two new files produced and ready to drop in:
- `frontend/include/ui/omnisave_vault.h`
- `frontend/src/ui/omnisave_vault.cpp`

Plus a complete replacement `frontend/src/app.cpp` with OmniSave fully wired in.

**What OmniSave Vault does:**
Split-panel save management screen. Left panel = Memory Card (SpriteCard animated
icons parsed live from .mcr binary). Right panel = Save States (thumbnail grid).
L1/R1 switches panel focus. D-pad navigates within focused panel.

**Access points:**
- F5 (keyboard) → opens in SAVING mode
- F7 (keyboard) → opens in LOADING mode  
- In-game menu Save State / Load State items → routes to OmniSave instead of silent save
- Returns to IN_GAME on close if game is loaded, GAME_BROWSER otherwise

**AppState added:** `OMNISAVE_VAULT`

**New members added to app.h** (manual edits required — see below):
```cpp
#include "omnisave_vault.h"
#include "memcard_manager.h"

// In AppState enum, after TROPHY_HUB:
OMNISAVE_VAULT,

// In private members, after m_trophyHub:
std::unique_ptr<OmniSaveVault>   m_omniSave;
std::unique_ptr<MemCardManager>  m_memCards;
std::string m_currentGameTitle;   // clean display title for OmniSave
std::string m_currentGameSerial;  // serial (e.g. SCUS-94900) for memcard lookup
```

**CMakeLists.txt addition needed:**
In `FRONTEND_SOURCES`, after `src/ui/trophy_hub.cpp`:
```cmake
src/ui/omnisave_vault.cpp
```

**SpriteCard (.mcr) parser notes:**
- Reads PS1 memory card binary directly (128KB standard format)
- Validates "MC" header magic
- Walks directory frames (128 bytes each, starting at offset 128)
- Finds ALLOC_FIRST (0x51) blocks only
- Icon data in save data block: "SC" magic, icon flag (0x11/12/13 = 1/2/3 frames)
- Palette: 16 × BGR555 entries (32 bytes at offset 4 in data block)
- Pixel data: 128 bytes per frame at offsets 128/256/384 (16×16 @ 4bpp)
- Palette entry 0 = transparent
- Animates at ~4fps (250ms per frame) matching PS1 BIOS speed
- Shift-JIS title decoder handles full-width ASCII subset common in PS1 saves

**Memory card delete** is scaffolded (logs TODO) but not implemented —
rewriting the MCR directory allocation chain is its own feature, planned for
a future session.

**Known items to verify on first test:**
1. Does it compile cleanly with the new app.h members?
2. Does F5/F7 open the vault correctly from in-game?
3. Do memory card entries parse and show (even placeholder icons are fine first)?
4. Does the L1/R1 panel switch work?
5. Does closing return to the game correctly?

---

## Files Modified/Created This Session

| File | Location | Status |
|------|----------|--------|
| `omnisave_vault.h` | `frontend/include/ui/` | ✅ new file |
| `omnisave_vault.cpp` | `frontend/src/ui/` | ✅ new file |
| `app.cpp` | `frontend/src/` | ✅ full replacement with OmniSave wired in |
| `app.h` | `frontend/include/` | ⚠️ manual edits needed (see above) |
| `CMakeLists.txt` | `frontend/` | ⚠️ add omnisave_vault.cpp to sources |
| `ra_manager.h` | `frontend/include/` | ✅ trophyShotCountdown fix (Session 21) |
| `settings_screen.cpp` | `frontend/src/ui/` | ✅ version bumped to 0.5.1-dev (Session 21) |

---

## Git Commit for This Session

```
git add frontend/include/ui/omnisave_vault.h
git add frontend/src/ui/omnisave_vault.cpp
git add frontend/src/app.cpp
git add frontend/include/app.h
git add frontend/CMakeLists.txt
git commit -m "feat(omnisave): OmniSave Vault — split-panel save management screen

New screen: OmniSave Vault (AppState::OMNISAVE_VAULT)
- Split layout: Memory Card panel (left, 42%) + Save States panel (right, 58%)
- L1/R1 switches panel focus; D-pad navigates within focused panel
- Divider shows accent glow on the active panel side

SpriteCard (.mcr parser):
- Reads PS1 memory card binary directly — no external library
- Decodes up to 3 animation frames per save entry (16x16 @ 4bpp + BGR555 palette)
- Animates at ~4fps matching PS1 BIOS behaviour
- Shift-JIS title decoder for PS1 save entry display names
- Entry 0 palette index treated as transparent (PS1 spec)

Save State panel:
- Thumbnail grid (3 columns) with slot label and timestamp
- '+' New Save sentinel card always visible at end of grid
- Load on A, Save on Start/Menu, Delete on Y

Routing:
- F5 opens OmniSave in SAVING mode (replaces silent F5 quick-save)
- F7 opens OmniSave in LOADING mode (replaces silent F7 quick-load)
- In-game menu Save/Load State actions route to OmniSave
- Closes back to IN_GAME if game loaded, GAME_BROWSER otherwise

app.cpp: add MemCardManager init, m_currentGameTitle/Serial population on
game launch and clear on stop, OmniSave wired into event/update/render loops"

git push
```

---

## Next Priorities (Session 23+)

1. **Test OmniSave Vault** — first priority. Fix any compile or runtime issues.
   Key things to check: panel rendering, MCR parsing, L1/R1 switching, close behaviour.

2. **Memory card entry delete** — currently scaffolded (TODO log). Requires
   rewriting MCR directory frame allocation table. Self-contained follow-up.

3. **RA unlock timestamps in Trophy Vault** — add `unlockedAt` field to
   `AchievementInfo`, populate from `rc_client_achievement_t::unlock_time`,
   display in trophy grid cards. Purely additive, no risk to OmniSave.

4. **OmniSave Hub** — global all-games save overview (like Trophy Hub).
   One row per game with save state count, memcard entry count, first SpriteCard
   icon as thumbnail. Navigate in to open that game's Vault.
   New files: `omnisave_hub.h/.cpp`. New AppState: `OMNISAVE_HUB`.

5. **Dedicated RetroAchievements settings tab** — in-app login UI.
   Needs on-screen keyboard or simplified text entry first.

6. **chdman integration** — CHD conversion tool in HaackStation tools section.

---

## Architecture Notes for Fresh Claude

### OmniSave Vault — key design decisions
- `OmniSaveMode` enum: BROWSE, SAVING, LOADING (passed to `open()`)
- `OmniPanel` enum: MEMCARD, SAVESTATES (tracks which side has focus)
- `m_trophyShotCountdown` in ra_manager.h is now `Uint32` (SDL_GetTicks target),
  not a frame counter. Zero = inactive. Set to `SDL_GetTicks() + 400` on trophy unlock.
- Version string is hardcoded in `settings_screen.cpp` — NOT driven by CMake
  version.h.in (that system exists but isn't wired to the display yet).

### MemCardManager
- `prepareSlot1(gameSerial)` returns the .mcr path and creates the file if missing
- SHARED mode: all games use `memcards/shared/MemoryCard1.mcr`
- PER_GAME mode: `memcards/per_game/SERIAL_1.mcr`
- Currently initialised to SHARED mode in app.cpp

### m_currentGameTitle / m_currentGameSerial
- Set in `HaackApp::launchGame()` after successful core load
- `m_currentGameTitle` = `stripRomRegion(stem)` — clean display name
- `m_currentGameSerial` = `ge->serial` from GameEntry if available, else empty
- Both cleared in `HaackApp::stopGame()`
- Passed to `m_omniSave->open()` on F5/F7 and in-game menu actions

### PS1 .mcr format quick reference
```
File: 128KB (131072 bytes)
Header magic: bytes 0-1 = 'M','C'
Directory frames: offset 128, 128 bytes each, 15 usable slots
  byte 0:    allocation state (0x51=first, 0x52=mid, 0x53=last, 0xA0=free)
  bytes 4-7: file size (little-endian uint32)
  bytes 10-21: product code (ASCII)
  bytes 64-127: display title (Shift-JIS)
Save data block: (blockIndex+1) * 8192 bytes from file start
  bytes 0-1:  'S','C' magic
  byte 2:     icon flag (0x11=1frame, 0x12=2frames, 0x13=3frames)
  bytes 4-35: palette (16 × BGR555, 2 bytes each)
  bytes 128+: icon pixel data (128 bytes per frame, 4bpp)
```

---

## Build Instructions
From `C:\Users\digit\Documents\HaackStation\` in Command Prompt:
```
cmake --build build --config Release
```
For a clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```

---

# HaackStation — Session 23 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.1-dev** (bump to 0.5.2-dev once memory card reads confirmed working)
Windows build — MSVC via CMake. Project root: `C:\Users\digit\Documents\HaackStation\`
Source in `frontend/src/`, headers in `frontend/include/`.

---

## Feature Names (branding finalised)
- **HaackStack with GhostScan** — multi-disc library manager + deduplication
- **OmniSave & SpriteCard** — save state manager + animated memory card sprite viewer
- **Trophy Vault** (per-game grid) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement feature
- Trophy Hub footer should say "Powered by RetroAchievements"

---

## Session 23 — What Was Completed

### OmniSave Vault — Major Refactor & Fixes ✅

**In-game menu collapse:**
- Removed `SAVE_STATE` and `LOAD_STATE` from `InGameMenuAction` enum
- Replaced with single `OPEN_OMNISAVE` action
- Removed `InGameMenuSection::SAVE_STATES` and `LOAD_STATES` entirely
- Removed `SaveStateManager` dependency from `InGameMenu` completely
- Deleted dead code: `navigateSaveStates`, `freeThumbnails`, `loadThumbnails`,
  `renderSaveStates`, `renderSlotCard`, and all related members
- In-game menu now shows: Resume → OmniSave → (Change Disc if multi-disc) → Quit to Shelf
- Selecting OmniSave fires `m_pendingAction` immediately — no sub-screen

**Keyboard shortcut consolidation:**
- F5 + F7 collapsed to single F5 — opens OmniSave Vault in BROWSE mode
- OmniSave Vault handles both Save and Load internally — no need for separate entry points

**Screenshot capture for thumbnails:**
- `open()` now accepts `SDL_Surface* gameScreenshot` (ownership transfers to vault)
- All open() call sites capture the game frame via `captureCleanScreenshot()` before
  switching state, so thumbnails show the game — not the vault UI or a black frame
- F7/Load mode passes `nullptr` (no screenshot needed for loading)
- Vault destructor and subsequent open() calls free the surface correctly

**Button assignment in Save State panel (context-sensitive):**
- **A (CONFIRM)** = context-sensitive:
  - On "+ New Save" sentinel or empty slot → Save
  - On existing slot → Load
- **Y/Triangle (OPTIONS)** = explicit "Save Here" (overwrites selected slot deliberately)
- **Start (MENU)** = Delete selected slot
- Footer hints updated to match

**Note on button labels:** Footer currently shows "X = Save Here" but Y/Triangle
is the correct physical button (OPTIONS maps to SDL_CONTROLLER_BUTTON_Y).
X (Square) is not currently mapped to any NavAction. Fix the footer label or
add X mapping — cosmetic, deferred to next polish pass.

**Memory card path fix (partial — still not showing entries):**
- Root cause identified: core writes to `saves/<GameTitle>.srm` but MemCardManager
  was looking in `memcards/shared/MemoryCard1.mcr` — completely different paths
- Fix applied: `launchGame()` now calls `prepareSlot1()` before `loadGame()` and
  redirects the core's save path to the MemCardManager's directory via `setSavePath()`
- `MemCardManager::saveDirectory()` added — returns just the folder path
- `loadMemCardEntries()` in OmniSave now checks for both `.mcr` and `.srm` files,
  preferring whichever is larger (has more data)
- **Status: Compiles, but memory card entries still not appearing in OmniSave left panel**
- Next session must debug this — see "Memory Card Debug Priority" section below

**Save states confirmed working:**
- Auto-save loads correctly from OmniSave ✅
- Manual save to new slot works (A on "+ New Save") ✅
- Load from existing slot works (A on filled slot) ✅
- Overwrite save works (Y on any slot) ✅

---

## Memory Card Debug Priority — START HERE Session 24

Memory card saves are not appearing in OmniSave's left panel even though the
core is writing save data. Debug steps in order:

**Step 1 — Find where the core is actually writing:**
After launching a game and saving in-game, check these locations:
- `build/Release/saves/` — original save path (may have .srm here)
- `build/Release/memcards/shared/` — where we redirect to in SHARED mode
- `build/Release/memcards/per_game/` — where PER_GAME mode would write

**Step 2 — Check console output:**
App now logs: `[HaackStation] Memory card dir: <path>` on every game launch.
Also check for `[OmniSave] Using .srm from core:` if the .srm fallback fires.

**Step 3 — Confirm `setSavePath()` timing:**
The core reads `RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY` during `retro_load_game`.
Our `setSavePath()` call must happen BEFORE `m_core->loadGame(path)`.
In `launchGame()`, the order is now:
  1. `prepareSlot1(serial)` — creates .mcr file
  2. `setSavePath(saveDir)` — redirects core's save directory
  3. `m_core->loadGame(path)` — core reads the save dir here
If order is wrong, core will still write to old `saves/` path.

**Step 4 — Verify .srm filename matching:**
Beetle PSX HW names the .srm after the ROM filename stem (e.g. `Alundra.srm`).
`loadMemCardEntries()` tries `m_gameTitle + ".srm"` and `m_gameSerial + ".srm"`.
Make sure `m_gameTitle` in OmniSave matches exactly what the core uses.
Add a debug log in `loadMemCardEntries()` to print what paths it's checking.

**Step 5 — Verify MCR parser:**
If the .srm file is found but entries still don't show, the parser may be
rejecting the file. The core writes a standard 128KB PS1 memory card image
to the .srm — it should parse identically to a .mcr. Add a log in `parseMcr()`
at the top to confirm it's being called and what it finds for the "MC" header magic.

**Likely fix:** Either the save directory redirect isn't taking effect (check timing),
or the filename matching in `loadMemCardEntries()` isn't finding the .srm.

---

## Files Modified This Session

| File | Location | Change |
|------|----------|--------|
| `ingame_menu.h` | `frontend/include/` | Removed SAVE_STATE/LOAD_STATE, added OPEN_OMNISAVE, removed SAVE_STATES/LOAD_STATES sections, removed SaveStateManager dependency |
| `ingame_menu.cpp` | `frontend/src/` | Removed navigateSaveStates, renderSaveStates, renderSlotCard, freeThumbnails, loadThumbnails; single OmniSave menu item |
| `omnisave_vault.h` | `frontend/include/ui/` | Added SDL_Surface* m_gameScreenshot member; updated open() signature |
| `omnisave_vault.cpp` | `frontend/src/ui/` | Screenshot capture fix; context-sensitive A button; button remapping; .srm fallback in loadMemCardEntries |
| `memcard_manager.h` | `frontend/include/` | Added saveDirectory() method |
| `memcard_manager.cpp` | `frontend/src/` | Implemented saveDirectory() |
| `app.cpp` | `frontend/src/` | InGameMenu constructor (3 args); OPEN_OMNISAVE handler; setSavePath redirect before loadGame; F5-only shortcut |

---

## Git Commit for This Session

```
git add frontend/include/ingame_menu.h
git add frontend/src/ingame_menu.cpp
git add frontend/include/ui/omnisave_vault.h
git add frontend/src/ui/omnisave_vault.cpp
git add frontend/include/memcard_manager.h
git add frontend/src/memcard_manager.cpp
git add frontend/src/app.cpp
git commit -m "refactor(omnisave): collapse save/load menu, fix screenshot capture, memcard path redirect

In-game menu:
- Replace SAVE_STATE+LOAD_STATE with single OPEN_OMNISAVE action
- Remove InGameMenuSection::SAVE_STATES/LOAD_STATES and all dead code
- Remove SaveStateManager dependency from InGameMenu entirely
- Single OmniSave entry fires immediately, no sub-screen

OmniSave Vault:
- open() accepts SDL_Surface* gameScreenshot (ownership transfer)
- All open() sites capture game frame before state switch (correct thumbnails)
- A button context-sensitive: Save on empty/new, Load on existing slot
- Y/Triangle (OPTIONS) = explicit Save Here (prevents accidental overwrite)
- Start (MENU) = Delete slot
- loadMemCardEntries: checks both .mcr and .srm, prefers larger file

MemCardManager:
- Add saveDirectory() returning just the folder path

app.cpp:
- InGameMenu constructor now takes 3 args (no SaveStateManager)
- launchGame: prepareSlot1 + setSavePath before loadGame (core save dir redirect)
- F5+F7 collapsed to single F5 opening vault in BROWSE mode"

git push
```

---

## OmniSave Backlog (future sessions)

### Near-term (Session 24)
1. **Fix memory card entries not showing** — see debug section above, top priority
2. **Load confirm dialog** — `m_confirmPending` enum state overlay before `doLoadAction()`
   fires. "Load this save? [A] Confirm / [B] Cancel". Same for Delete.
3. **Footer label fix** — "X = Save Here" should say "Y = Save Here" (or add X mapping)

### Medium-term
4. **Per-game memory card mode** — switch MemCardMode to PER_GAME, one .srm per serial.
   Cleaner than shared card, avoids the PS1 "memory card full" problem.
5. **OmniSave Gallery view** — Left panel shows one animated SpriteCard icon per game
   (sourced from each game's .srm). Selecting a game drills into that game's card.
   This is John's idea from Session 23 — good v1.0 feature, build on solid foundations first.
6. **Save state copy** — "Branch from this point" use case. Copy slot N to new slot.
7. **Memory card entry delete** — rewrite MCR directory allocation chain. Self-contained.

### Long-term
8. **RA unlock timestamps** — `unlockedAt` in AchievementInfo from `unlock_time`
9. **Dedicated RA settings tab** — in-app login UI, needs on-screen keyboard
10. **chdman integration** — CHD conversion in HaackStation tools section
11. **OmniSave Hub** — global all-games save overview (like Trophy Hub)
12. **File/folder cleanup** — defer to v1.0. Some screen files (scrape_screen,
    per_game_settings_screen, remap_screen) logically belong in `ui/` subfolder
    but moving them requires updating all includes + CMakeLists. Low risk as a
    dedicated housekeeping session at v1.0.

---

## Architecture Notes for Fresh Claude

### OmniSave Vault — key design
- `OmniSaveMode` enum: BROWSE, SAVING, LOADING (all entry points now use BROWSE)
- `OmniPanel` enum: MEMCARD, SAVESTATES (tracks which side has focus)
- `open(title, serial, mode, screenshot)` — call before setState(OMNISAVE_VAULT)
- Screenshot ownership transfers to vault; freed on next open() or destruction
- Save state panel: context-sensitive A button (save if empty, load if exists)
- Returns to IN_GAME on close if game loaded, GAME_BROWSER otherwise

### InGameMenu — post-refactor state
- Constructor: `InGameMenu(SDL_Renderer*, ThemeEngine*, ControllerNav*)` — 3 args only
- No SaveStateManager dependency
- Sections: MAIN, DISC_SELECT only (SAVE_STATES/LOAD_STATES removed)
- Actions: NONE, RESUME, OPEN_OMNISAVE, CHANGE_DISC, QUIT_TO_SHELF

### MemCardManager
- `prepareSlot1(serial)` — returns .mcr path, creates blank 128KB file if missing
- `saveDirectory(serial)` — returns just the folder (used to redirect core's save path)
- SHARED mode: `memcards/shared/` — currently active default
- PER_GAME mode: `memcards/per_game/` — planned switch for Session 24

### Core save path redirect (launchGame order — critical)
```
1. m_memCards->prepareSlot1(serial)      // ensure .mcr exists
2. m_core->setSavePath(saveDir)          // MUST be before loadGame
3. m_core->loadGame(path)                // core reads save dir here
```
If setSavePath is called after loadGame, core writes to old `saves/` path.

### Button mapping (controller_nav.cpp)
- A = SDL_CONTROLLER_BUTTON_A → NavAction::CONFIRM
- B = SDL_CONTROLLER_BUTTON_B → NavAction::BACK
- Y = SDL_CONTROLLER_BUTTON_Y → NavAction::OPTIONS
- Start = SDL_CONTROLLER_BUTTON_START → NavAction::MENU
- X (Square) = NOT CURRENTLY MAPPED to any NavAction

### Version string
Hardcoded in `settings_screen.cpp` — NOT driven by CMake version.h.in.
Bump to 0.5.2-dev once memory card reads are confirmed working.

### CHD cdreader (from Session 20 — still current)
`ra_manager.cpp` lines ~31-260. Magic-tagged handle approach (0xC4DC4FE0).
Sector type table: MODE2_RAW sectorSize=2048 sectorSkip=24.

### PS1 .mcr/.srm format
```
File: 128KB (131072 bytes)
Header magic: bytes 0-1 = 'M','C'
Directory frames: offset 128, 128 bytes each, 15 usable slots
  byte 0:    allocation state (0x51=first, 0x52=mid, 0x53=last, 0xA0=free)
  bytes 4-7: file size (little-endian uint32)
  bytes 10-21: product code (ASCII)
  bytes 64-127: display title (Shift-JIS)
Save data block: (blockIndex+1) * 8192 bytes from file start
  bytes 0-1:  'S','C' magic
  byte 2:     icon flag (0x11=1frame, 0x12=2frames, 0x13=3frames)
  bytes 4-35: palette (16 × BGR555, 2 bytes each)
  bytes 128+: icon pixel data (128 bytes per frame, 4bpp)
Note: .srm written by Beetle PSX HW is identical format to .mcr
```

---

## Build Instructions
From `C:\Users\digit\Documents\HaackStation\` in Command Prompt:
```
cmake --build build --config Release
```
For a clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```
CMake configure should show:
```
-- libchdr found — CHD hashing enabled
-- rcheevos sources: C:/... (long list of .c files)
```
---

# HaackStation — Session 24 Handoff
_Last updated: April 29, 2026_

---

## Current Version
**v0.5.1-dev** (post-Session 24)

---

## What Was Completed This Session

### 1. OmniSave — X Button Remapped (Save Here)
- `controller_nav.cpp`: `SDL_CONTROLLER_BUTTON_X` → `NavAction::OPTIONS` (was unmapped)
- `SDL_CONTROLLER_BUTTON_Y` → `NavAction::NONE` (was OPTIONS)
- `omnisave_vault.cpp`: footer hints updated — X = "Save Here", Start = "Delete" (manual hint added)

### 2. Memory Card System — Full Persistence Fix (Major)
This was the primary work of the session. Root cause chain, fully resolved:

**Root causes (in order of discovery):**
1. `RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY` was receiving a relative path — Beetle may have discarded it silently. Fixed with `fs::absolute()`.
2. `beetle_psx_hw_use_mednafen_memcard0_method` was set to `libretro` — Beetle expects frontend to own the file but was never given data. Temporarily switched to `mednafen` to diagnose, then switched back.
3. `buildBlankCard()` was creating a card filled with `0xFF`. Beetle reads frame 1 byte 0 on load — `0xFF` signals a corrupt card, so Beetle zeroed its SRAM buffer and we were flushing zeros to disk. Fixed with proper PS1 format.
4. `loadSaveRAM()` was missing entirely. Beetle always starts with a zeroed SRAM buffer; the frontend must copy the `.mcr` file INTO the buffer after `retro_load_game()` completes.

**Files changed:**
- `libretro_bridge.h/.cpp`: Added `flushSaveRAM(destPath)` and `loadSaveRAM(srcPath)`
  - `flushSaveRAM`: reads `RETRO_MEMORY_SAVE_RAM`, atomic temp→rename write to `.mcr`
  - `loadSaveRAM`: reads `.mcr` from disk via staging buffer, `memcpy` into core SRAM
- `app.h`: Added `m_activeCardPath` (string), `m_memcardFlushTimer` (Uint32), `MEMCARD_FLUSH_INTERVAL_MS = 30000`
- `app.cpp`:
  - `libretro` memcard method confirmed correct
  - `fs::absolute()` on save dir path
  - Stores `m_activeCardPath` via `m_memCards->activeCardPath(serial)` on launch
  - Calls `m_core->loadSaveRAM(m_activeCardPath)` after `m_ra->loadGame()`
  - 30s autosave timer in IN_GAME update case
  - `flushSaveRAM` called in `stopGame()` and `shutdown()` before `unloadGame()`
- `memcard_manager.h`: Added public `activeCardPath(serial)` wrapper (was private `slotPath`)
- `memcard_manager.cpp`:
  - New `buildBlankCard()` static function — proper PS1 spec:
    - Frame 0: `MC` + XOR checksum at byte 127
    - Frames 1–15: `0xA0` free markers + checksums
    - Frames 16–63: `0xFF` (broken sector list)
    - Frames 64+: `0x00` (data blocks)
  - `prepareSlot1()` uses `buildBlankCard()`
  - Stale card detector: if frame 1 byte 0 == `0xFF`, deletes and rebuilds

**Expected log lines (working state):**
```
[MemCard] Slot 1: memcards/shared/MemoryCard1.mcr
[HaackStation] Memory card: C:\...\memcards\shared\MemoryCard1.mcr
[Bridge] SaveRAM loaded ← C:\...\memcards\shared\MemoryCard1.mcr (131072 bytes)
...
[Bridge] SaveRAM flushed → C:\...\memcards\shared\MemoryCard1.mcr (131072 bytes)
[Bridge] Game unloaded
```

### 3. OnScreenKeyboard (OSK) — New Component
New self-contained modal QWERTY overlay. Files added (not yet wired to any screen):
- `include/ui/onscreen_keyboard.h`
- `src/ui/onscreen_keyboard.cpp`

**Features:**
- QWERTY layout, 5 rows including number/symbol row and action row
- Shift (one-shot) and Caps Lock (double-tap Shift)
- Masked input mode for password fields (shows `*`)
- X/Square = global backspace shortcut (no need to navigate to DEL)
- A/Cross = type key, B/Circle = cancel, D-pad = navigate
- Physical keyboard passthrough via SDL_TEXTINPUT
- Kenney PS button icons for footer hints (assets/osk/)
- Callback-based API: `open(prompt, maxLen, masked, callback)`

**Assets needed** (copy from Kenney pack to `assets/osk/`):
- `cross.png`, `circle.png`, `square.png`, `dpad.png`

**CMakeLists:** add `src/ui/onscreen_keyboard.cpp` to FRONTEND_SOURCES and copy block for `assets/osk/`

**Not yet wired to:** RA settings tab, Profile/Eden PIN entry

---

## Known Issues / Deferred

### Memory Card — Save State Interaction
Loading a save state after an in-game save can overwrite the card data in Beetle's SRAM buffer (save states include card state at snapshot time). Deferred — not critical for v0.5 testing. Fix when addressing: flush SRAM to temp before loading state, restore after.

### OmniSave — Memory Card Entry Not Interactive
Memory card entry shows in OmniSave but can't be loaded from. The card data persists correctly now; next step is implementing the load path in `omnisave_vault.cpp`.

### RA Notification — First Launch Timing
RA game notification occasionally doesn't appear on first game launch in a session. Appears to be a timing issue introduced by `loadSaveRAM()` running immediately after `m_ra->loadGame()`. Non-critical; monitor.

---

## Roadmap (Updated)

### Immediate Next (v0.5.x)
1. **OmniSave — Memory Card Load path** — wire up loading from the card entry in OmniSave; currently displays but is non-interactive
2. **Wire OSK to RA Settings Tab** — add username/password fields with OSK for RA login UI
3. **OmniSave — Save State interaction fix** — preserve card SRAM across state loads

### Near-term (v0.5.x → v0.6)
4. **RA Settings Tab** — dedicated tab with in-app login UI (OSK ready, just needs wiring)
5. **RA Unlock Timestamps in Trophy Vault**
6. **chdman integration**
7. **OmniSave — SpriteCard visuals** (per the function-first rule, deferred until load/save paths are solid)

### v1.0 Features
8. **Eden — HaackStation User Profile System**
   - Named for the developer's wife
   - Each user lives in their own "Garden" — isolated saves, memcards, states, screenshots, trophies, theme, controller config
   - Profile selector on launch
   - Optional PIN lock (numeric, uses OSK PIN mode — add to OSK later)
   - Child account mode: game filtering, read-only saves (can create new, not overwrite)
   - Implementation note: start by ensuring NO hardcoded path strings remain — all paths must go through a ProfileManager or equivalent before building the feature
   - Estimated: 4–6 focused sessions
9. **ReScore** (audio replacement)
10. **RetroAchievements official recognition** (reapply ~v1.0, pending July 2026 window)

---

## Architecture Notes

### Memory Card Ownership Model
HaackStation owns the memcard file entirely. Beetle PSX HW (`libretro` method) treats SRAM as a live buffer — it never reads or writes the file itself. The frontend is responsible for:
- `loadSaveRAM()` on game launch (disk → buffer)
- `flushSaveRAM()` on game stop, app shutdown, and every 30s during play (buffer → disk)

### Path Convention
All paths passed to the core (`setSavePath`, `activeCardPath`) must be **absolute**. Relative paths are silently mishandled by Beetle on Windows. Use `fs::absolute()` at the point of construction in `launchGame()`.

### OSK Integration Pattern (for future screens)
```cpp
// In screen header:
std::unique_ptr<OnScreenKeyboard> m_osk;

// In constructor:
m_osk = std::make_unique<OnScreenKeyboard>(renderer, theme, nav);

// Gate all input/update/render:
if (m_osk->isOpen()) { m_osk->handleEvent(e); return; }
if (m_osk->isOpen()) { m_osk->update(dt); return; }
// render OSK last (on top):
if (m_osk->isOpen()) m_osk->render();
```

### Eden — Path Isolation Strategy
When implementing Eden, the key architectural move is:
- Add `ProfileManager` class that owns `activePath()` — returns base path for the current profile
- Replace all hardcoded path roots (`"saves/"`, `"memcards/"`, `"media/"`) with `m_profileMgr->activePath() + "saves/"` etc.
- Do NOT build the UI until all paths go through ProfileManager — retrofitting path assumptions is the expensive part

---

## Session Commit Summary
```
git commit -m "fix(memcard): implement frontend-owned SaveRAM persistence + OSK foundation

- libretro_bridge: flushSaveRAM() atomic write, loadSaveRAM() buffer restore
- app: loadSaveRAM on launch, flushSaveRAM on stop/shutdown, 30s autosave
- memcard_manager: correct PS1 blank card format, stale card auto-rebuild
- controller_nav: X button → NavAction::OPTIONS (Square = Save Here)
- omnisave_vault: footer hints updated for X button mapping
- onscreen_keyboard: new QWERTY OSK component (not yet wired)"
```

---

## File Index (Session 24 Changes)
| File | Status |
|------|--------|
| `frontend/include/app.h` | Modified |
| `frontend/include/core_bridge/libretro_bridge.h` | Modified |
| `frontend/include/memcard_manager.h` | Modified |
| `frontend/include/ui/onscreen_keyboard.h` | **New** |
| `frontend/src/app.cpp` | Modified |
| `frontend/src/core_bridge/libretro_bridge.cpp` | Modified |
| `frontend/src/memcard_manager.cpp` | Modified |
| `frontend/src/ui/controller_nav.cpp` | Modified |
| `frontend/src/ui/omnisave_vault.cpp` | Modified |
| `frontend/src/ui/onscreen_keyboard.cpp` | **New** |
---

# HaackStation — Session 27 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.1-dev**
Windows build — MSVC via CMake.
Root: `C:\Users\digit\Documents\HaackStation\`
Source: `frontend/src/`, headers: `frontend/include/`

---

## Workflow Rules
- John prefers **fully drop-in ready files** — complete replacements, not snippets
- **Commit after every working session**
- Function-first rule: no visual polish until features work correctly

---

## Feature Names (locked)
- **HaackStack / GhostScan** — multi-disc library + deduplication
- **OmniSave / SpriteCard** — save state manager + memory card viewer
- **Trophy Vault** (per-game) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement
- **Eden** — user profile system (named for developer's wife)

---

## Session 27 — What Was Completed

### Favorites restored
Triangle/Y → NavAction::FAVORITE. Square/X stays OPTIONS. Game browser checks FAVORITE.

### NavAction canonical mapping
- CONFIRM → A/Cross, BACK → B/Circle, OPTIONS → X/Square, FAVORITE → Y/Triangle, MENU → Start

### OSK hold-scroll fixed
navigateWithClamp() — no wrap at all during held repeat. Hard stop at row edge.
Single press (navigate()) still wraps between rows as before.

### RA login notification on every connect
Queued from inside RAManager on both password AND token login success callbacks.
id=UINT32_MAX sentinel → green border, 5 sec duration.
Fires on app launch (token login) and on manual Settings login.

### RA unlock timestamps in Trophy Vault
AchievementInfo::unlockTime (time_t). Persisted in JSON cache.
Display: "Today" / "Yesterday" / "N days ago" / "Apr 12" / "Apr 12 2024"

### Quit crash fixed
m_ra explicitly shut down at top of HaackApp::shutdown() before renderer/window destroyed.
clearCallbacks() called first to prevent in-flight HTTP callbacks firing into dead memory.

### OSK polish (prior sessions, now stable)
Lowercase keys when unshifted. Physical keyboard flash highlight. Controller highlight
suppressed while typing from physical keyboard, restores on d-pad.

---

## CRITICAL BUG — Memory Card Bleed
Alundra save data appeared in Crash Bandicoot's save slots.
All games currently share one memory card — per-game card routing is broken/missing.
Fix this FIRST in Session 28 via OmniSave/MemCardManager per-game card selection.

---

## Files — Current State on Disk

| File | Location |
|------|----------|
| controller_nav.h/cpp | frontend/include/ui/, frontend/src/ui/ |
| game_browser.cpp | frontend/src/ui/ |
| onscreen_keyboard.h/cpp | frontend/include/ui/, frontend/src/ui/ |
| settings_screen.h/cpp | frontend/include/ui/, frontend/src/ui/ |
| ra_manager.h/cpp | frontend/include/, frontend/src/ |
| trophy_room.cpp | frontend/src/ui/ |
| app.h/cpp | frontend/include/, frontend/src/ |

---

## Git Commit

```
git add frontend/include/ui/controller_nav.h frontend/src/ui/controller_nav.cpp
git add frontend/src/ui/game_browser.cpp
git add frontend/include/ui/onscreen_keyboard.h frontend/src/ui/onscreen_keyboard.cpp
git add frontend/include/ui/settings_screen.h frontend/src/ui/settings_screen.cpp
git add frontend/include/ra_manager.h frontend/src/ra_manager.cpp
git add frontend/src/ui/trophy_room.cpp
git add frontend/include/app.h frontend/src/app.cpp
git commit -m "feat/fix: OSK polish, RA timestamps, favorites restored, quit crash, login notification"
git push
```

---

## Roadmap

### Session 28 — Priority Order
1. Memory card bleed fix — per-game card routing
2. OmniSave — Memory Card load path (card entry shows but can't load)
3. Rename RA tab to "Accounts", add ScreenScraper login

### Architecture — Tabs & Profiles
- "Accounts" tab: RA + ScreenScraper credentials
- "Profiles" tab (future Eden): per-user isolation
- Scraped artwork and badges are GLOBAL (not per-profile) — avoids duplicate scraping
- Per-profile: saves, states, memcards, screenshots only

### Notification ID Sentinels
- id == 0 → game-load (blue)
- id == UINT32_MAX → login success (green)
- any other id → achievement unlock (gold)

### Memory Card Model
- HaackStation owns .mcr files
- loadSaveRAM() on launch, flushSaveRAM() on stop/shutdown/every 30s
- BUG: all games sharing one card — next session priority

### v1.0
- Eden profile system (4-6 sessions)
- ReScore audio replacement
- RA official recognition reapply ~July 2026
---
# HaackStation — Session 28 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.1-dev**
Windows build — MSVC via CMake.
Root: `C:\Users\digit\Documents\HaackStation\`
Source: `frontend/src/`, headers: `frontend/include/`

---

## Workflow Rules
- John prefers **fully drop-in ready files** — complete replacements, not snippets
- **Commit after every working session**
- Function-first rule: no visual polish until features work correctly

---

## Feature Names (locked)
- **HaackStack / GhostScan** — multi-disc library + deduplication
- **OmniSave / SpriteCard** — save state manager + memory card viewer
- **Trophy Vault** (per-game) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement
- **Eden** — user profile system (named for developer's wife)

---

## Session 28 — What Was Completed

### RA Notification system fully overhauled — `ra_manager.cpp` / `ra_manager.h`

**Problem:** Login notification was queued immediately at boot (token auth fires before any
game loads), so it appeared solo for however long game loading takes — then disappeared or
behaved erratically when the game-load notification arrived alongside it.

**Solution — hold-and-inject pattern:**
- Both login callbacks (password + token) now store the login notification in
  `m_pendingLoginNotif` / `m_hasPendingLoginNotif` instead of calling `queueNotification`.
- `queueNotification()`: when id==0 (game-load) arrives, checks `m_hasPendingLoginNotif`
  and injects the login notification into the queue first with the same `showUntil`
  timestamp and fresh `slideAnim=0`. Both panels enter on the same frame, same timer.
- Login notification only ever shows once per session (correct — fires at boot token auth
  or manual login, not on every game load).

**Additional fixes made along the way:**
- `update()` slide-out bug: once `showUntil` passed, `slideAnim` was never driven to 0
  so notifications froze on screen permanently. Added `else` branch that actively
  decrements `slideAnim` after expiry. Erase condition `slideAnim <= 0.f` now fires.
- `unloadGame()`: removed `m_notifications.clear()` — hard-kill was cutting off
  in-progress animations between game loads. Notifications now expire via `showUntil`.
- `unloadGame()`: removed `rc_client_unload_game()` call. This was disrupting rcheevos
  internal async state, causing `rc_client_get_game_info()` to return null in the
  `begin_identify_and_load_game` callback on the second game load (game-load notification
  never fired after first game). `rc_client_unload_game` now only called in `shutdown()`
  immediately before `rc_client_destroy()`.
- Two-pass render: game-load (id==0) and achievement unlocks render first (bottom slots),
  login (id==UINT32_MAX) renders second (above them). Consistent with achievement slot.

### OSK held scroll — `onscreen_keyboard.cpp`
- `navigateWithClamp()`: removed `cancelHeld()` from UP/DOWN row boundary.
  `cancelHeld()` sets `m_holdCancelled=true` which blocks ALL held-repeat until
  button is released — user couldn't continue scrolling after hitting a row edge.
  Now silently stops at boundary; held state stays alive. LEFT/RIGHT still calls
  `cancelHeld()` at row edge (correct — prevents held L/R bleeding into adjacent rows).
- Note: no wrap-around during hold — hard stop at edges. Single press still wraps.
  John accepted this as good enough for now.

### Stray .lib files in project root — RESOLVED (no code change)
Seven files like `CUsersdigitDocumentsHaackStation...HaackAudio.lib` appeared in
project root from a garbled GitBash paste that treated a Windows `cp` command path
as a flat filename. Safe to delete — real `.lib` files are in `build/` subdirs.

### rcheevos dep line ending changes — SAFE
`deps/rcheevos/` files show as modified due to LF→CRLF normalization on Windows
plus one upstream addition: `RC_INVALID_VALUE = -40` in `rc_error.h`. Not authored
by us, safe to commit alongside frontend changes.

---

## Notification ID Sentinels (locked)
- `id == 0` → game-load (blue accent, `raBlue = {32, 144, 255}`)
- `id == UINT32_MAX` → login success (green accent, `{60, 200, 100}`)
- any other id → achievement unlock (gold accent, `{255, 215, 0}`)

---

## CRITICAL BUG — Memory Card Bleed (STILL OUTSTANDING)
Alundra save data appeared in Crash Bandicoot's save slots.
All games currently share one memory card — per-game card routing is broken/missing.
**Fix this FIRST in Session 29** via OmniSave/MemCardManager per-game card selection.

---

## Roadmap — Priority Order

### Session 29
1. **Memory card bleed fix** — per-game card routing (CRITICAL)
2. **OmniSave** — memory card load path (card entry shows but can't load)
3. **Rename RA settings tab to "Accounts"** + add ScreenScraper login fields to same tab

### Near-term
4. OmniSave / SpriteCard save state manager (main feature)
5. RA unlock timestamps in Trophy Vault (already have `unlockTime` in `AchievementInfo`,
   display logic and JSON persistence in place — verify rendering in Trophy Vault grid)
6. chdman integration (CHD hashing confirmed working — unblocks this)

### Long-term
7. Eden profile system (4–6 sessions) — per-user isolation for saves/states/memcards
8. ReScore audio replacement
9. RA official recognition reapply ~July 2026

---

## Architecture Notes

### RA Notification Hold Pattern
`ra_manager.h` private members:
```cpp
bool            m_hasPendingLoginNotif = false;
AchievementInfo m_pendingLoginNotif;
```
Login callbacks store here instead of calling `queueNotification`.
`queueNotification()` injects pending login notif when id==0 fires.

### Memory Card Ownership Model
HaackStation owns .mcr files. Beetle PSX HW treats SRAM as a live buffer.
- `loadSaveRAM()` on game launch (disk → buffer)
- `flushSaveRAM()` on game stop, app shutdown, every 30s during play (buffer → disk)
- **BUG**: all games share one card — per-game routing needed

### OSK Integration Pattern
```cpp
// In screen header:
std::unique_ptr<OnScreenKeyboard> m_osk;
// In constructor:
m_osk = std::make_unique<OnScreenKeyboard>(renderer, theme, nav);
// Gate all input/update/render:
if (m_osk->isOpen()) { m_osk->handleEvent(e); return; }
if (m_osk->isOpen()) { m_osk->update(dt); return; }
if (m_osk->isOpen()) m_osk->render(); // last, on top
```

### Path Convention
All paths passed to core must be **absolute**. Use `fs::absolute()` at construction
in `launchGame()`. Relative paths silently mishandled by Beetle on Windows.

### NavAction Canonical Mapping
- CONFIRM → A/Cross
- BACK → B/Circle
- OPTIONS → X/Square
- FAVORITE → Y/Triangle
- MENU → Start

### Accounts Tab Architecture
- Single "Accounts" tab: RA credentials + ScreenScraper credentials
- Scraped artwork and badges are GLOBAL (not per-profile)
- Per-profile (future Eden): saves, states, memcards, screenshots only

---

## Files Modified This Session

| File | Location | Change |
|------|----------|--------|
| `ra_manager.h` | `frontend/include/` | Added `m_hasPendingLoginNotif`, `m_pendingLoginNotif` |
| `ra_manager.cpp` | `frontend/src/` | Notification overhaul (see above) |
| `onscreen_keyboard.cpp` | `frontend/src/ui/` | OSK held scroll fix |

---

## Build
From `C:\Users\digit\Documents\HaackStation\` in Command Prompt:
```
cmake --build build --config Release
```
Clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```
---

# HaackStation — Session 29 Handoff

## Project
PS1 emulator frontend — C++/SDL2/libretro (Beetle PSX HW core).
**Version: 0.5.1-dev**
Windows build — MSVC via CMake.
Root: `C:\Users\digit\Documents\HaackStation\`
Source: `frontend/src/`, headers: `frontend/include/`

---

## Workflow Rules
- John prefers **fully drop-in ready files** — complete replacements, not snippets
- **Commit after every working session**
- Function-first rule: no visual polish until features work correctly

---

## Feature Names (locked)
- **HaackStack / GhostScan** — multi-disc library + deduplication
- **OmniSave / SpriteCard** — save state manager + memory card viewer
- **Trophy Vault** (per-game) / **Trophy Hub** (all-games overview)
- **ReScore** — audio replacement
- **Eden** — user profile system (named for developer's wife)

---

## Session 29 — What Was Completed

### PS1 Serial Detection from Disc Image — NEW FEATURE ✅

`GameEntry::serial` is now populated at scan time for all library entries by reading
SYSTEM.CNF directly from the disc image. No external library required — pure ISO9660
filesystem walk.

**Files changed:**
- `frontend/include/library/disc_formats.h` — added `static std::string readSerial(path)`
- `frontend/src/library/disc_formats.cpp` — full ISO9660 implementation (see below)
- `frontend/src/library/game_scanner.cpp` — calls `readSerial()` for all entries

**How it works (`DiscFormats::readSerial`):**
1. Detects format (BIN/CUE, CHD, ISO, M3U) via existing `detectFormat()`
2. For BIN/CUE: opens the `.bin`, reads PVD at sector 16, walks ISO9660 root
   directory to find SYSTEM.CNF LBA, reads and parses it
3. For CHD: uses libchdr to read sectors; same ISO9660 walk
4. For ISO: same as BIN but 2048-byte sectors, no header skip
5. For M3U: reads first disc line only (not all discs — prevents cross-contamination)
6. Regex: `BOOT2?\s*=[^\r\n]*?([A-Z]{2,4})[_A-Z]?(\d{3})[.](\d{2})`
   — handles both standard (`SLUS_001.23`) and no-separator (`SLUSP012.06`) formats

**Verified working for all 7 games in library:**
```
[Scanner] Serial detected: SLUS-01206 for Dragon Warrior VII    (BIN/CUE)
[Scanner] Serial detected: SCUS-94491 for Legend of Dragoon, The (CHD via M3U)
[Scanner] Serial detected: SLUS-00553 for Alundra               (CHD, non-standard SYSTEM.CNF LBA)
[Scanner] Serial detected: SLUS-00726 for Brave Fencer Musashi  (BIN/CUE)
[Scanner] Serial detected: SCUS-94263 for Bust A Groove         (BIN/CUE)
[Scanner] Serial detected: SLUS-01159 for Bust A Groove 2       (BIN/CUE)
[Scanner] Serial detected: SCUS-94900 for Crash Bandicoot       (BIN/CUE)
```

**Key implementation notes:**
- Alundra's SYSTEM.CNF is NOT at sector 23 — ISO9660 walk is essential
- Dragon Warrior VII uses `SLUSP012.06` format (no underscore) — regex handles with `[_A-Z]?`
- CHD PVD detection tries skip=24 then skip=32 (some encoders duplicate sub-header)
- BIN reader tries MODE2_RAW (skip=24) and MODE1_RAW (skip=16) automatically
- Error logs retained for failure cases: `[Serial] BIN PVD not found`, `[Serial] BIN open failed`, etc.

**Serial prefix reference:**
- `SCUS` = Sony Computer Entertainment US (first-party / SCE-published)
- `SLUS` = Sony Licensed US (third-party licensees)
- `SCES`/`SLES` = European PAL equivalent
- `SCPS`/`SLPS` = Japanese NTSC-J equivalent

---

### Per-Game Memory Card Routing — FIXED ✅

The memory card bleed bug (all games sharing one card) is now resolved.

**Root cause:** `app.cpp` always set `MemCardMode::SHARED` at init and never applied
the per-game override before `prepareSlot1()` was called. `GameEntry::serial` was also
always empty so even if the override code ran, it couldn't route correctly.

**Fix:** In `launchGame()`, a lightweight `PerGameSettings::load()` runs in the memcard
setup block *before* `prepareSlot1()`. It reads `memcard_slot` from the per-game cfg,
sets the correct `MemCardMode`, then `prepareSlot1()` routes to the right card.

**`revertPerGameSettings()`** now resets `MemCardMode` to `SHARED` on game exit
so the next game starts clean.

**`applyPerGameSettings()`** now receives `m_currentGameSerial` (was always `""` —
the "can be added later" comment is resolved).

**Per-game cfg format** (`saves/per_game/<SERIAL>.cfg`):
```
memcard_slot=1
```
Named after the game's serial, e.g. `saves/per_game/SLUS-00553.cfg` for Alundra.
HaackStation creates the `.mcr` file automatically on first launch if it doesn't exist.

**Expected launch log (working state):**
```
[PerGame] Loaded config: saves/per_game/SLUS-00553.cfg
[HaackStation] Memcard mode (per-game override): PER_GAME
[MemCard] Created new memory card: memcards/per_game/SLUS-00553_1.mcr
[MemCard] Slot 1: memcards/per_game/SLUS-00553_1.mcr
[Bridge] SaveRAM loaded ← ...memcards/per_game/SLUS-00553_1.mcr (131072 bytes)
...
[Bridge] SaveRAM flushed → ...memcards/per_game/SLUS-00553_1.mcr (131072 bytes)
```

**Design decision — make per-game the global default:**
Currently requires a per-game `.cfg` to activate PER_GAME mode. Next session should
switch the default in `app.cpp` from `MemCardMode::SHARED` to `MemCardMode::PER_GAME`
and remove the `overrideMemCard` toggle from the per-game settings screen (redundant).
This eliminates the need to manually create cfg files for every game.

---

## OmniSave — Virtual Memory Card Shelf (Design Clarification)

John's intended design (confirmed this session — do not lose this):

**In-game:** OmniSave shows per-game memory card + save states in 50/50 split panel.
Each game has its own isolated `.mcr` file. No shared card complexity.

**Outside game (future feature — "Card Shelf"):** A browsable screen where each game's
`.mcr` is presented as a physical PS1 memory card with its animated SpriteCard icon,
block count, and game title. Select a card → see all save blocks inside with icons.
This is the nostalgic save manager experience — "reaching into your memory card drawer."
Not a file manager, a visual collection. Build on solid foundations first.

**The unified feel:** Even though each game has its own `.mcr`, OmniSave presents
everything as a seamless interface. The per-file isolation is invisible to the user.

---

## OmniSave — Remaining Display Issues (functional, cosmetic)

1. **Entry names** — currently shows raw block ID string (e.g. `BASLUS-00062GRADIUS`).
   Should show clean game title from the MCR block's Shift-JIS title field, stripping
   the serial prefix, converting encoding. Fall back to game shelf title if unreadable.

2. **50/50 split layout** — memory card panel is currently ~40%, save state panel ~60%.
   Target is equal split.

3. **Animated SpriteCard icons** — MCR icon data is parsed (16×16, 1-3 frames, BGR555
   palette). Animation loop needs to be wired to the render pass. Palette entry 0 = transparent.

Priority order: entry names → 50/50 layout → animated icons.

---

## Files Modified This Session

| File | Location | Change |
|------|----------|--------|
| `disc_formats.h` | `frontend/include/library/` | Added `readSerial()` declaration |
| `disc_formats.cpp` | `frontend/src/library/` | ISO9660 serial detection implementation |
| `game_scanner.cpp` | `frontend/src/library/` | Calls `readSerial()`, logs detections |
| `app.cpp` | `frontend/src/` | Per-game memcard mode applied before `prepareSlot1()`; serial passed to `applyPerGameSettings()`; `revertPerGameSettings()` resets mode |

---

## Git Commit

```
git add frontend/include/library/disc_formats.h
git add frontend/src/library/disc_formats.cpp
git add frontend/src/library/game_scanner.cpp
git add frontend/src/app.cpp
git commit -m "feat(serial+memcard): PS1 serial detection from disc + per-game card routing

Serial detection:
- DiscFormats::readSerial() walks ISO9660 root dir to find SYSTEM.CNF
  regardless of LBA (fixes Alundra and other non-standard layouts)
- Handles BIN/CUE (MODE2_RAW + MODE1_RAW), CHD (libchdr), ISO, M3U
- Regex handles standard (SLUS_001.23) and no-separator (SLUSP012.06) formats
- GameEntry::serial populated for all library entries at scan time
- M3U reads first disc only to prevent cross-contamination

Per-game memcard routing:
- app.cpp: apply per-game memcard mode before prepareSlot1() so routing
  uses correct card path from the start
- revertPerGameSettings() resets memcard mode to SHARED on game exit
- applyPerGameSettings() now receives m_currentGameSerial (was always empty)"

git push
```

---

## Roadmap — Priority Order

### Session 30 — Start Here
1. **Make PER_GAME the global default** — switch `app.cpp` init from `SHARED` to `PER_GAME`,
   remove `overrideMemCard` toggle from per-game settings screen. All games get isolated
   cards automatically. No manual cfg files needed.
2. **OmniSave — entry display names** — decode Shift-JIS title from MCR block header,
   strip serial prefix, fall back to game shelf title
3. **OmniSave — 50/50 layout fix**
4. **OmniSave — animated SpriteCard icons** in left panel
5. **OmniSave — memory card load path** (card entry shows but can't load from it)

### Near-term
6. **Rename RA settings tab to "Accounts"** + add ScreenScraper login fields
7. **RA unlock timestamps** — verify Trophy Vault grid rendering (data already in place)
8. **chdman integration**

### Long-term
9. **Eden profile system** (4–6 sessions)
10. **ReScore** audio replacement
11. **RA official recognition** reapply ~July 2026
12. **OmniSave Card Shelf** — global memory card browser (outside-game feature)
13. **OmniSave Hub** — all-games save overview (like Trophy Hub)

---

## Architecture Notes

### Serial Detection — Key Facts
- `DiscFormats::readSerial(path)` is the public entry point
- ISO9660 PVD is at logical sector 16 of the data track (guaranteed by spec)
- Root directory LBA is at PVD offset 156+2 (little-endian uint32)
- SYSTEM.CNF LBA read from root directory walk — do NOT assume sector 23
- CHD uses libchdr; BIN uses std::ifstream with `(lba * sectorSize + skip)` seek
- M3U: read first disc line only, call `readSerial()` recursively

### MemCardManager — Post-Session 29 State
- `MemCardMode::SHARED` — still the init default (change to PER_GAME next session)
- `MemCardMode::PER_GAME` — routes to `memcards/per_game/<SERIAL>_1.mcr`
- `prepareSlot1(serial)` — creates blank 128KB PS1-format card if missing
- `activeCardPath(serial)` — returns the full path for the current mode/serial
- `setMode(mode)` — switches mode; must be called before `prepareSlot1()`
- Mode is reset to SHARED in `revertPerGameSettings()` on game exit

### Per-Game Settings Flow (launchGame order — critical)
```
1. PerGameSettings::load(serial, path)    // check for overrides
2. m_memCards->setMode(mode)             // apply memcard mode BEFORE prepareSlot1
3. m_memCards->prepareSlot1(serial)      // create/locate the .mcr file
4. m_core->setSavePath(saveDir)          // redirect core save dir (absolute path)
5. m_core->loadGame(path)               // core reads save dir here
6. m_core->loadSaveRAM(activeCardPath)  // copy disk → core SRAM buffer
```

### Path Convention
All paths to core must be **absolute**. Use `fs::absolute()`. Relative paths
silently mishandled by Beetle PSX HW on Windows.

### NavAction Canonical Mapping
- CONFIRM → A/Cross
- BACK → B/Circle
- OPTIONS → X/Square
- FAVORITE → Y/Triangle
- MENU → Start

### Notification ID Sentinels
- `id == 0` → game-load notification (blue)
- `id == UINT32_MAX` → login success (green)
- any other id → achievement unlock (gold)

### PS1 .mcr Format Quick Reference
```
File: 128KB (131072 bytes)
Header magic: bytes 0-1 = 'M','C'
Directory frames: offset 128, 128 bytes each, 15 usable slots
  byte 0:    allocation state (0x51=first, 0x52=mid, 0x53=last, 0xA0=free)
  bytes 4-7: file size (little-endian uint32)
  bytes 10-21: product code (ASCII, e.g. "BASLUS-00553ALUND")
  bytes 64-127: display title (Shift-JIS)
Save data block: (blockIndex+1) * 8192 bytes from file start
  bytes 0-1:  'S','C' magic
  byte 2:     icon flag (0x11=1frame, 0x12=2frames, 0x13=3frames)
  bytes 4-35: palette (16 × BGR555, 2 bytes each) — index 0 = transparent
  bytes 128+: icon pixel data (128 bytes per frame, 4bpp)
Note: .srm written by Beetle PSX HW is identical format to .mcr
```

### Version String
Hardcoded in `settings_screen.cpp` — NOT driven by CMake version.h.in.
Bump to `0.5.2-dev` next session once per-game default is confirmed working.

---

## Build Instructions
From `C:\Users\digit\Documents\HaackStation\` in Command Prompt:
```
cmake --build build --config Release
```
Clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```
CMake configure should show:
```
-- libchdr found — CHD hashing enabled
-- rcheevos sources: C:/... (long list of .c files)
```
---
# HaackStation Session 30 Handoff

## Completed This Session

### Item 1 — PER_GAME Memcard Default ✅ Committed
- `app.cpp`: init, `launchGame`, and `revertPerGameSettings` all set `MemCardMode::PER_GAME`
- Removed `overrideMemCard` / `memCardSlot` fields from `per_game_settings.h` and `per_game_settings.cpp`
- Removed dead memcard override log from `applyPerGameSettings`
- Version bumped to `0.5.2-dev` in `settings_screen.cpp`

### Item 2 — OmniSave Card Load Action ✅ Committed
- `handleMemCardNav`: CONFIRM sets `m_wantsCardReload = true` + `m_wantsClose = true`
- `consumeCardReload()` public accessor added to `omnisave_vault.h`
- `m_wantsCardReload` private member added to `omnisave_vault.h`
- `app.cpp` close handler: flush → reload SRAM when `consumeCardReload()` returns true
- Footer hint updated to show `[A] Reload` on MEMCARD panel
- Verified in console: flush → reload sequence working correctly

### Item 3 — SRAM Flush Guard on State Load ✅ Committed
- `setSramCallbacks(flush, reload)` added to `OmniSaveVault` public interface
- `m_sramFlush` / `m_sramReload` `std::function` members added to private data section
- `#include <functional>` added to header
- `doLoadAction`: flushes SRAM before `loadState()`, reloads card after successful load
- Callbacks wired once in `app.cpp` init after `make_unique`
- All three `open()` call sites restored correctly (F5, F7, in-game menu)
- Verified in console: flush → load → reload sequence working correctly

### Item 4 — CMakeLists Build Output Path ✅ Committed
- `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")` added to root `CMakeLists.txt`
- **Note:** Currently landing in `build/Release/Release` due to double-nesting with VS generator — needs investigation next session

### Regression Fix — Game Shelf Left/Right Scroll Clamping ⚠️ Partially Working
- Fix was written and committed to `game_browser.cpp`
- `!isRepeat` guard added to LEFT/RIGHT wrap branches to mirror UP/DOWN behaviour
- **Controller:** Clamps correctly at end of list, but scrolls through entire list instead of stopping at end of each row as intended
- **Keyboard:** No clamping in any direction at all
- Needs revisiting next session — `isRepeat` guard logic may not be matching how keyboard repeat events are actually fired

### Scraper Fix — ScreenScraper Serial Lookup ✅ Committed
- `buildApiUrl`: serial now sent as `serialnum` parameter instead of `romnom`
- Game name always sent as `romnom` hint alongside serial
- ScreenScraper had changed behaviour — passing PS1 serial as `romnom` was returning wrong cross-platform results (Saturn, PS2 art for PS1 games)
- All 7 games re-scraped successfully after fix

---

## Known Issues Carrying Forward

| Issue | Notes |
|-------|-------|
| `build/Release/Release` double-nesting | CMake generator + `RUNTIME_OUTPUT_DIRECTORY` combining — investigate root CMakeLists |
| CMake 4.3.0-rc3 installed (auto-updated) | Causes clean rebuild failures — downgrade to stable 3.31.x when convenient |
| `memcards/shared/` folder | Safe to delete — nothing writes there anymore with PER_GAME as default |

---

## Next Session Priorities

### 1. Game Shelf Clamping Fix (High Priority)
Revisit `game_browser.cpp` `moveSelection()`. Two distinct problems:

- **Keyboard:** `isRepeat` flag may never be true for keyboard events — keyboard repeat may be firing as fresh presses rather than repeat events, meaning the clamp branch is never reached
- **Controller row-end behaviour:** Should stop at the last game in the current row on hold, then require a fresh press to advance to the next row — currently ignores row boundaries and only clamps at end of entire library

Upload `game_browser.cpp` and the keyboard/controller input handler so repeat event detection can be traced end to end.

### 2. Remaining Roadmap Items
Continue per `HAACKSTATION_ROADMAP.md`

---

# HaackStation Session 31 Handoff

## Completed This Session

### Game Shelf Clamping Fix ✅ Committed
- `handleEvent`: was always passing `isRepeat=false` to `moveSelection` — keyboard clamping never worked
- Fixed by detecting `e.key.repeat != 0` and passing that as `isRepeat`
- `moveSelection` LEFT: added `cancelHeld()` at row start on hold — cursor now stops at row boundary, requires fresh press to cross
- `moveSelection` RIGHT: same treatment — `cancelHeld()` at row end on hold, fresh press required to advance to next row
- Files: `frontend/src/ui/game_browser.cpp`

### OmniSave Entry Display Names ✅ Committed
- Serial prefix stripping added to `parseMcr` — strips `BA+serial` prefix (e.g. `BASLUS-00553`) and plain serial prefix from Shift-JIS decoded title
- Trims leading spaces after strip
- Title priority: decoded SJIS → shelf game title → product code
- Files: `frontend/src/ui/omnisave_vault.cpp`

### OmniSave 50/50 Layout Fix ✅ Committed
- `DIVIDER_X_PC`: 42 → 50 so both panels get equal screen width
- Files: `frontend/include/ui/omnisave_vault.h`

### SpriteCard Icons — Partially Working ⚠️ Not Committed
- Animation ✅ working
- Size bumped to 64px ✅
- Nearest-neighbour scaling added via `SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest)` ✅
- **Colors still wrong** — see notes below

---

## Known Issues Carrying Forward

| Issue | Notes |
|-------|-------|
| SpriteCard icon colors | See detailed notes below — this is the main outstanding item |
| `build/Release/Release` double-nesting | Use `CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE` instead of `CMAKE_RUNTIME_OUTPUT_DIRECTORY` in root CMakeLists.txt — one line fix |
| `memcards/shared/` folder still created | Something touches shared path on init — needs investigation in `memcard_manager.cpp` + `app.cpp` |
| CMake 4.3.0-rc3 installed | Causes clean rebuild failures — downgrade to stable 3.31.x when convenient |

---

## SpriteCard Color Issue — Detailed Notes for Next Session

### What works
- Python PIL decode of the raw MCR file produces correct golden/warm Alundra icon
- DuckStation MCD and HaackStation MCR have **identical raw palette data** — not a save corruption issue
- Animation, sizing, and nearest-neighbour scaling all working correctly
- Icon frame data is being read from the correct block offsets

### What we know about the raw data
- PS1 memory card palette entries are 16-bit little-endian BGR555
- **Bit 15 of each palette entry is a semi-transparency flag** — PS1 hardware uses this for blending. Most renderers ignore it for icon display but it may explain why we only see ~4 apparent colors instead of 16
- Correct channel extraction confirmed via Python: **R = bits 10-14, G = bits 5-9, B = bits 0-4**
- Correct expanded palette[1] value: `R=214, G=231, B=16, A=255` (warm gold)
- 5-to-8 bit expansion: `(x << 3) | (x >> 2)` — confirmed correct

### What's been tried (all wrong in SDL)
Every combination of channel extraction order and SDL pixel format has been attempted. The Python output is correct but something in the SDL texture upload path is misinterpreting the bytes.

### Suggested approach for next session
- Try creating an `SDL_Surface` from the raw pixel data using `SDL_CreateRGBSurfaceFrom` with explicit masks, then convert to texture via `SDL_CreateTextureFromSurface` — this bypasses the pixel format naming confusion entirely
- Explicit masks for correct output: `Rmask=0xFF000000, Gmask=0x00FF0000, Bmask=0x0000FF00, Amask=0x000000FF` (adjust based on what Python confirms)
- Upload `omnisave_vault.cpp` at start of session

### Current state of `buildIconTexture` and palette decode
Leave these alone until next session — do not commit the partial SpriteCard color work. The animation and size changes are already in the working build but the color code is still in flux.

---

## Next Session Priorities

### 1. SpriteCard color fix (carry forward — high priority)
See detailed notes above. Try `SDL_CreateRGBSurfaceFrom` approach.

### 2. OmniSave load/delete confirm dialog (item 8)
`m_confirmPending` overlay before `doLoadAction()` or delete fires. "Load this save? [A] Confirm / [B] Cancel." Same for delete. Self-contained in `omnisave_vault.cpp`.

### 3. OmniSave memory card entry delete (item 9)
Rewrite MCR directory frame allocation chain. Currently scaffolded as TODO. Self-contained.

### 4. OmniSave save state copy/branch (item 10)
"Branch from this point" — copy slot N to a new slot.

### 5. Accounts tab (item 11)
Rename current RA tab to "Accounts." ScreenScraper credentials at top, RA credentials below. OSK already built and stable.

---

## Version String
`0.5.2-dev` — in `settings_screen.cpp`. Bump to `0.5.3-dev` once SpriteCard colors are confirmed working.

---

## Build Instructions
From `C:/Users/digit/Documents/HaackStation/` in Command Prompt:
```
cmake --build build --config Release
```
Clean rebuild:
```
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```
CMake configure should show:
```
-- libchdr found — CHD hashing enabled
-- rcheevos sources: C:/... (long list of .c files)
```
---

# HaackStation — Session 32 Handoff
_Prepared at end of Session 31_

---

## What We Completed This Session

### SpriteCard Icon Decode — FULLY FIXED 🎉
After many sessions of being wrong, the PS1 memory card icon decode is now
correct and verified against real MCR files (Alundra, Crash Bandicoot, Bust A
Groove, Dragon Warrior VII all render perfectly).

**Root causes found and fixed:**
1. **Palette offset wrong** — was reading from offset 4 (into the Shift-JIS
   title string). Correct offset is **96** (after SC magic + flag + title_block_count
   + 92-byte title field).
2. **Pixel offset wrong** — was using 320. Correct is **128** (immediately
   after the 32-byte palette).
3. **Channel order wrong** — PS1 RGB555 stores **R at bits 0–4**, G at 5–9,
   B at 10–14. Previous code had R and B swapped.
4. **Nibble order wrong** — **low nibble is first pixel**, high nibble is
   second. Previous code had this backwards.
5. **Transparency** — STP bit (bit 15) distinguishes opaque black (STP=1)
   from transparent black (STP=0). Pure black with STP=0 → alpha=0.

**Verified save data block layout:**
```
Offset 0–1:   "SC" magic
Offset 2:     icon display flag (0x11/0x12/0x13 = 1/2/3 frames)
Offset 3:     title block count
Offset 4–95:  save title (92 bytes, Shift-JIS, null-padded)
Offset 96–127: palette (32 bytes, 16 × RGB555 little-endian)
Offset 128+:  icon frames (128 bytes each, 4bpp, lo-nibble = first pixel)
```

### CRT Scanline Overlay — NEW FEATURE ✨
Added a CRT scanline effect to SpriteCard icons only (not the rest of the UI).
- One semi-transparent dark horizontal line per source pixel row
- Alpha = 40 (subtle — user tuned from 80 down)
- Respects alpha transparency — doesn't draw over transparent pixels
- Clipped to icon bounds only — rest of UI stays crisp
- Nobody else does this — score one for HaackStation!

**Implementation:** In `renderMemCardPanel()` in `omnisave_vault.cpp`, after
`SDL_RenderCopy` for the icon texture, draws 1px rects every `ICON_SIZE /
MCR_ICON_H` pixels vertically over the icon rect using `SDL_BLENDMODE_BLEND`.

---

## Current File State

`omnisave_vault.cpp` — clean, no debug code, all fixes applied, scanlines
included. Delivered as final file at end of session.

**Key constants in omnisave_vault.cpp:**
```cpp
static constexpr int MCR_ICON_PAL_OFF  = 96;   // palette offset in data block
static constexpr int MCR_ICON_PIX_OFF  = 128;  // first frame pixel offset
```

---

## Issues Noted / To Investigate Next

### 1. Memory Card Disappears After Disc Swap
**Symptom:** Dragon Warrior VII (multi-disc) — memory card entry shows
correctly after first disc session. After swapping disc, exiting, and
re-entering, the memory card no longer shows in OmniSave.

**Likely cause:** Core re-initializes on disc swap and looks for `.mcr` path
based on new disc serial, losing the previous card association.

**Resolution:** Will be addressed naturally when implementing the proper disc
swap feature (open tray / swap / close tray behavior — already on roadmap).
Do not chase this separately — log it and wait for the disc swap session.

### 2. Live In-Game Save Detection + Toast Notification
**Current behavior:** SRAM is flushed to disk periodically and on game exit.
User has no indication when an in-game save actually happened.

**Desired behavior (like DuckStation/other emulators):**
- Detect when the game actually writes to the memory card during gameplay
- Flush SRAM to disk immediately at that moment
- Show a brief toast notification: "Memory Card saved" (or similar)
- OmniSave memory card panel should reflect the new save without requiring
  a game exit/reload

**Implementation approach:**
- Compare SRAM checksum every ~2 seconds during gameplay (tiny buffer, cheap)
- On checksum change → immediate flush to disk → trigger toast notification
- Periodic timed flush stays as a safety net failsafe
- On next OmniSave open, `loadMemCardEntries()` will naturally pick up the
  new data since it re-parses the `.mcr` file fresh each time

**This is a meaningful UX improvement** — implement in an upcoming session
dedicated to memory card polish. Suggest calling it the "LiveCard" feature.

---

## Roadmap Reminder (Near-Term Priorities)

From HAACKSTATION_ROADMAP.md, next priorities after this session:
1. RetroAchievements dedicated settings tab (needs on-screen keyboard / text
   entry implementation)
2. OmniSave & SpriteCard save system continued polish (LiveCard detection
   above is part of this)
3. RA unlock timestamps in Trophy Vault
4. chdman integration
5. Disc swap / tray emulation (will also fix the memory card disappear issue)

---

## Version
Currently at v0.5.1-dev, Session 31 complete / Session 32 starting.
Version string location: `settings_screen.cpp` → Settings > About tab.

---

## Notes for Next Claude Instance
- Always ask for `omnisave_vault.cpp` and relevant `.mcr` files before
  touching icon decode code — the binary analysis approach (Python script
  dumping raw bytes) is what finally cracked this after many failed attempts
  at guessing offsets from documentation alone.
- The scanline alpha of 40 is user-tuned and deliberate — don't change it
  without asking.
- `buildIconTexture()` uses `SDL_CreateRGBSurfaceFrom` with explicit channel
  masks (not a named SDL pixel format) — this is intentional to avoid
  platform endianness ambiguity.
---
# Session 33 Handoff & Roadmap Additions

## Session 33 Summary
Completed work:
- LiveCard SRAM detection + smart toast notifications (memory card + trophy screenshot)
- OmniSave confirm dialogs (load, delete, overwrite, card reload) — colour-coded borders
- MCR entry delete — full PS1 block chain rewrite, XOR checksum, atomic write, SRAM sync
- Card Time Machine snapshot + card screenshot wired into LiveCard detection

## Confirmed Working This Session
- Memory card save toast fires correctly on user saves only (silent on 30s safety flush)
- Trophy screenshot toast fires on RA unlock
- All three OmniSave confirm dialogs render and function (load=blue, overwrite=amber, delete=red)
- MCR entry delete correctly frees block chain and recomputes checksums

## Known Issues / Polish Notes (not blocking)
- Memory card toast text overflows its box — needs `toastW` value bumped (polish phase)
- Trophy screenshot toast should fire from same position as manual screenshot toast (continuity)
- False LiveCard toast on game load — fix is resetting `m_sramPollTimer = 0` after `loadSaveRAM`
  in `launchGame()` to give Beetle time to normalize SRAM before first poll

---

## Roadmap Additions (new items from Session 33 discussion)

### NEW: Card Time Machine — Restore UI (OmniSave session)
**Priority: High — data is already accumulating as of Session 33**
The snapshot engine is live. Need to build the restore UI in OmniSave.

Layout: Third panel or dedicated "Card History" screen accessible from the
memcard panel (e.g. Y/Triangle = "View History").

Features needed:
- Scrollable list of timestamped .mcr snapshots from `memcards/history/<STEM>/`
- Each entry shows: timestamp, SpriteCard icons parsed from that snapshot,
  game frame thumbnail (paired .png from same timestamp)
- Select + A = restore: copy snapshot → active card path (atomic), flush+reload
  core SRAM, reload OmniSave memcard panel
- Before restore: snapshot CURRENT card one more time so nothing is ever lost
- Retention display: "Showing 20 of 20 snapshots (oldest: May 3)"

Folder pairing (already on disk after Session 33):
```
memcards/history/SCUS-94900_1/
    2026-05-08_21-04-33.mcr   ← card state
    2026-05-08_21-04-33.png   ← game frame at save moment
```

### NEW: Undo Save State (panic button)
**Priority: Medium**
Before any save state WRITE (doSaveAction), snapshot current state to a hidden
`saves/states/<game>/.undo.state` slot. Single slot, always overwritten.
In OmniSave save states panel: add "Undo Last Save" option (only visible when
.undo slot exists). Restores state, deletes .undo slot.
Different from branching — this is a 1-step Ctrl+Z specifically for accidental
save overwrites.

### NEW: Crash Recovery "Resume from N minutes ago" prompt
**Priority: Low — auto-save slot already exists, just needs UX**
On game launch, if an auto-save slot exists that is newer than the last clean
quit timestamp, prompt: "Resume from [time]? [A] Yes [B] No"
The quit timestamp can be written to `saves/states/<game>/last_quit.txt`
in stopGame().

### NEW: OSK for memory card rename / description
**Priority: Medium — OSK already built, just needs wiring**
In OmniSave memcard panel, Y/Triangle = "Rename / Add Note"
Opens OSK overlay pre-filled with current display name.
Saves to sidecar: `memcards/per_game/SCUS-94900_1.meta.json`
  { "displayName": "My Crash Playthrough", "description": "Pre-warp room" }
OmniSave header shows custom name if exists, falls back to serial-derived name.
Underlying .mcr filename never changes.

---

## Near-Term Priority Queue (updated)
1. RetroAchievements dedicated settings tab (OSK wiring for credentials)
2. OmniSave — save state branching (copy slot N to new slot)
3. OmniSave — Card Time Machine restore UI (data accumulating now)
4. RA unlock timestamps in Trophy Vault (DONE — already implemented)
5. chdman integration
6. Accounts tab rename (RA + ScreenScraper)
7. RA game-load notification toggle
8. Hardcore/Softcore badge in Trophy Vault
9. RA feature toggle (raEnabled bool)
10. OSK memory card rename/description
11. Undo Save State (panic button)
12. Crash recovery resume prompt
13. Disc swap / tray emulation

---

## iOS / Cross-Platform Notes
Discussed targeting Android, Linux, Mac, and potential iOS.

**Mac → iOS:** SDL2 does NOT bridge Mac → iOS automatically. They are separate
build targets. However:
- The SDL2 codebase is the same — iOS is a supported SDL2 platform
- The main work is: Xcode project setup, iOS-specific input handling (touch →
  virtual d-pad or controller), file system sandboxing (iOS has strict path
  restrictions, saves need to go through iOS document directories), and
  App Store compliance (JIT/dynamic recompilation may be restricted on iOS
  without special entitlements — this is the biggest unknown for a libretro
  core on iOS)
- Beetle PSX HW specifically: uses OpenGL — needs to be ported to Metal or
  OpenGL ES for iOS. Non-trivial.
- Recommend: get Android working first (SDL2 Android support is mature and
  the power-loss / UberSave concerns Gemini mentioned are most relevant there),
  then evaluate iOS separately.
- Do NOT assume Mac build = iOS build. Plan it as a separate platform port.

**UberSave / continuous auto-save:**
Discussed but largely covered by existing systems:
- Rewind buffer = continuous in-RAM state capture (already working)
- 30s safety flush = crash protection for memory card (already working)
- LiveCard snapshots = Time Machine history (wired Session 33)
- Auto-save slot = coarse periodic state capture (already working)
- Undo Save State (new roadmap item above) = panic button for accidental overwrites
No separate "UberSave" system needed — document this for clarity.
---
# HaackStation — Session 34 Handoff
*Generated end of Session 33*

---

## Current Version
**v0.5.1-dev** → bump to **v0.5.2-dev** after Session 33 files are confirmed working.
Version string lives in `settings_screen.cpp` under Settings > About tab. One line change.

---

## What Was Built in Session 33

### 1. Card Time Machine — full restore UI
- Y/Triangle from the Memory Card panel opens a full-screen Time Machine history list
- Each row: game frame thumbnail + full date/time ("May 08, 2026  21:04") + relative age ("3 days ago" in purple) + save count + SpriteCard mini-icons parsed from that snapshot
- A = restore (purple confirm dialog) → flush → atomic copy → sramReload → list refreshes
- B = back to Memory Card panel (does NOT close OmniSave)
- No safety copy on restore — the history list itself is the safety net (Apple Time Machine model). Every LiveCard snapshot remains untouched regardless of what you restore.
- Uses `NavAction::FAVORITE` for Y/Triangle (how that button is mapped in this project)

### 2. False LiveCard toast fix
- `m_sramPollTimer` and `m_sramChecksum` now reset to 0 after `loadSaveRAM` in `launchGame()`
- Prevents the card load itself from being detected as a "save event" and firing a false toast

### 3. OmniSave receives active card path
- `m_omniSave->setActiveCardPath(m_activeCardPath)` called before every `open()` call
- Required for Time Machine restore to know where the live card lives

### 4. Full timestamp + relative age display
- Time Machine entries show "May 08, 2026  21:04" (unambiguous, full year)
- Purple relative age label below: "Today", "Yesterday", "3 days ago", "2 weeks ago", etc.
- Confirm dialog shows both: "May 08, 2026  21:04  (3 days ago)"

---

## Files To Apply (Session 33 — still pending)

These files are downloaded and ready. Apply before starting Session 34.

| File | Action |
|------|--------|
| `omnisave_vault.h` | Drop-in replacement |
| `omnisave_vault.cpp` | Drop-in replacement |
| `session33_patch_and_notes.txt` | 3 find-and-replace changes to `app.cpp` |

### app.cpp changes (from patch file):
1. Reset `m_sramPollTimer = 0` and `m_sramChecksum = 0` after `loadSaveRAM` in `launchGame()`
2. Add `m_omniSave->setActiveCardPath(m_activeCardPath)` before the `open()` call in `processInGameMenuActions()`
3. Same setActiveCardPath call before F5 and F7 keyboard opens in `handleEvents()`

---

## Git Commit Command (after applying and testing Session 33 files)

```bash
git add -A
git commit -m "Session 33: Card Time Machine restore UI + LiveCard toast fix

- OmniSave: Time Machine panel (Y from memcard panel)
  - Full-screen history list, newest-first
  - Full date + time + relative age (Today / 3 days ago / etc.)
  - SpriteCard mini-icons parsed from each snapshot
  - Game frame thumbnails paired by filename stem
  - Restore via A + purple confirm dialog (Apple Time Machine model -
    no safety copy, history itself is the safety net)
- OmniSave: removed redundant pre-restore safety snapshot logic
- app.cpp: fixed false LiveCard toast on game load
- app.cpp: pass activeCardPath to OmniSave before open()"
```

---

## Complete Current Feature Set

### Core Emulation
- Beetle PSX HW via libretro — software + hardware renderer
- Fast Boot (skip BIOS logo) — toggleable in settings
- Per-game settings (resolution, renderer overrides)
- Multi-disc support via M3U playlists with disc memory (remembers last disc)
- chdman-ready file structure (integration not yet built)

### Input
- Full controller support (SDL2 GameController API)
- Keyboard fallback mapping
- Custom input remapping screen
- Fast forward (hold R2 or F) with configurable multiplier (2x/4x/6x/8x)
- Rewind (hold L2 or backtick) with 10-second buffer
- Turbo mode (R1+R2 hold toggle) with configurable speed

### Save System — OmniSave Vault
- Split-panel UI: Memory Card (left) + Save States (right)
- **Memory Card panel:**
  - SpriteCard animated icons (4bpp decoded, 4fps animation, CRT scanline overlay)
  - Shift-JIS title decoding
  - Delete entry (full PS1 block chain rewrite + XOR checksum)
  - Reload card from disk (confirm dialog)
  - Y = open Time Machine
- **Save State panel:**
  - Thumbnail grid with auto/slot labels
  - Load, save, overwrite, delete with confirm dialogs
  - Color-coded confirm borders (blue=safe, amber=overwrite, red=delete, purple=restore)
- **Card Time Machine panel:**
  - Auto-populated by LiveCard detection
  - Full timestamp + relative age + SpriteCard mini-icons + game frame thumbnails
  - Restore = atomic copy → sramReload, no safety copy needed
- **LiveCard detection:**
  - CRC-32 checksum polling every N ms
  - Auto-flush + snapshot + screenshot on save detected
  - Toast notification (cyan-blue, bottom-left)
  - False toast on game load: FIXED (Session 33)
- **SRAM protection:**
  - flush/reload callbacks wired from app.cpp
  - Flush before state load, reload after
  - Full flush on game exit

### Scraping & Media
- ScreenScraper integration (username/password in settings)
- Box art, disc art, screenshot scraping
- Auto M3U generation for multi-disc games

### RetroAchievements
- Full RA integration (own dedicated settings tab — OSK already working)
- Trophy Hub (global achievements overview, persistent across sessions)
- Trophy Room (per-game achievement list)
- Auto-screenshot on trophy unlock
- Trophy screenshot toast (gold, bottom-left)
- Cached achievement data (works cold start)

### UI / Frontend
- Game browser shelf with cover art
- Dynamic column count based on library size
- Favorites (bookmark badge, Y button toggle)
- Play history (recently played shelf, session timer)
- Game Details panel (cover, playtime, trophy strip, per-game settings)
- In-game menu (resume, OmniSave, disc swap, quit)
- Settings screen (all options, RA tab, scraping, remapping)
- Splash screen
- Theme engine (deep navy + red accent, font system, all UI primitives)
- Screenshot system (clean framebuffer capture, UI capture)
- Screenshot toast (green, top-left)
- Fullscreen toggle (F11)
- Per-game settings screen

### Other
- Disc memory (remembers last disc for multi-disc games across sessions)
- Rewind manager (state capture, stepBack)
- Input map (save/load custom mappings)

---

## Known Polish Items (non-blocking)

- Memory card toast text overflows its box slightly — bump `toastW` in `renderMemcardToast()` in `app.cpp`
- Trophy screenshot toast position inconsistency vs manual screenshot toast (minor)

---

## Priority Queue — Next Sessions

### Session 34 — Soft Reset + Memory Card Hot-Swap
**Soft reset:**
- Add `SOFT_RESET` to `InGameMenuAction` enum
- Wire `retro_reset()` call in `LibretroBridge`
- Add "Soft Reset" option to in-game menu alongside disc swap
- Confirm dialog (amber — it's disruptive but not destructive)

**Memory card hot-swap (same session, natural pairing):**
- In-game menu: "Memory Card" option opens a card picker
- Shows all per-game cards + shared card option
- Select → flush current card → load new card → toast confirmation
- Note in toast: "Soft reset recommended if game doesn't recognize new card"
- Pairs perfectly with Time Machine: restore snapshot → hot-swap → soft reset = full workflow

**Why these together:** Both are core-level in-game menu operations, code is already in the right place (LibretroBridge + in-game menu), and hot-swap sets up the Time Machine integration endpoint.

### Session 35 — RA/Accounts Tab Rename
- Rename the existing RA settings tab
- Covers both RetroAchievements and ScreenScraper credentials cleanly
- Quick session, ~30 minutes
- NOTE: OSK is already built and working — this is purely a rename/reorganization

### Session 36 — OmniSave Save State Branching
- Copy save slot N to a new slot (branch workflow)
- UI addition to existing save state grid in OmniSave right panel
- Useful for "I want to try both paths from this point"

### Session 37 — chdman Integration
- Convert .bin/.cue → .chd from within HaackStation
- Keeps ROM folder tidy without leaving the app
- Background process with progress indicator

### Sessions 38+ — The Big Features
These are the sessions you're most excited about. Each is a full session minimum:
- **Upscaling controls** — per-game resolution override UI (backend already wired via core options)
- **Audio replacement** — per-game audio track replacement system
- **AI upscaling integration** — texture/image enhancement pipeline
- **Translation layer** — per-game text translation system
- **Theme system** — multiple named themes (PS1 Blue, SNES, Genesis, Dark Navy default)
  - PS1 theme colors documented in `haackstation_color_reference.txt`

---

## Important Technical Notes for New Sessions

### Memory Card Architecture
- Per-game mode: each serial gets its own `<SERIAL>_1.mcr`
- History snapshots: `memcards/history/<SERIAL>_1/YYYY-MM-DD_HH-MM-SS.mcr` + paired `.png`
- Active card path stored in `m_activeCardPath` (app.cpp) AND passed to OmniSave via `setActiveCardPath()`
- `sramFlush` = `m_core->flushSaveRAM(m_activeCardPath)`
- `sramReload` = `m_core->loadSaveRAM(m_activeCardPath)`
- CARD_HISTORY_MAX controls retention (pruning in `snapshotCardHistory()` in app.cpp)

### NavAction Mappings (controller_nav.h)
- `CONFIRM` = A / Cross
- `BACK` = B / Circle
- `OPTIONS` = X / Square
- `MENU` = Start
- `FAVORITE` = Y / Triangle ← used for Time Machine in OmniSave memcard panel
- `SHOULDER_L` = L1
- `SHOULDER_R` = R1

### OmniSave Panel Flow
- Default focus: SAVESTATES
- L1/R1 switches between MEMCARD and SAVESTATES
- FAVORITE (Y) from MEMCARD → TIMEMACHINE (full screen takeover)
- BACK from TIMEMACHINE → MEMCARD (not close)
- BACK from MEMCARD or SAVESTATES → close vault

### AppState for OmniSave
- `AppState::OMNISAVE_VAULT` — handled in update() and render()
- Returns to `IN_GAME` if core has game loaded, `GAME_BROWSER` otherwise
- `m_inputCooldownUntil` set to SDL_GetTicks() + 300 on close to prevent button bleed

### Color Reference
Full color reference with hex values for all UI elements, overlays, and theme suggestions
saved to `haackstation_color_reference.txt` — use this when designing custom icons.

---

## File Map (key files)

| File | Purpose |
|------|---------|
| `app.cpp` / `app.h` | Main application loop, game launch, all state management |
| `omnisave_vault.cpp` / `.h` | OmniSave UI — memcard, save states, Time Machine |
| `memcard_manager.cpp` / `.h` | Memory card path management, slot preparation |
| `save_state_manager.cpp` / `.h` | Save state read/write, thumbnail capture |
| `libretro_bridge.cpp` / `.h` | Core interface — loadGame, runFrame, SRAM, reset |
| `theme_engine.cpp` / `.h` | All colors, fonts, UI drawing primitives |
| `controller_nav.cpp` / `.h` | NavAction enum, input processing, held-repeat |
| `ingame_menu.cpp` / `.h` | In-game overlay menu |
| `ra_manager.cpp` / `.h` | RetroAchievements integration |
| `settings_screen.cpp` / `.h` | Settings UI (version string in About tab) |
| `game_browser.cpp` / `.h` | Shelf, cover art, selection |
| `trophy_hub.cpp` / `.h` | Global trophy overview |
| `trophy_room.cpp` / `.h` | Per-game achievement list |

---

## Dev Diary Note
All code written by Claude (Anthropic). Design, vision, feature decisions, testing,
and direction by the project author. Documented honestly as human-AI collaboration.
Transcripts of all sessions saved for future dev diary writeup at v1.0.

---
*End of Session 33 Handoff*
---
These are 2 more ideas I had I talked with Claude about - this is Claude's response - hopefully you can add this in where it goes!

Input-aware button prompts (keyboard vs controller):
This is a polish feature but a really impactful one — nothing breaks immersion faster than seeing "Press A" when you're using a keyboard. The right approach is having ControllerNav track the last input device used and expose a flag like isUsingController(). Then ThemeEngine::drawButtonHint checks that flag and swaps between controller glyphs ("A", "B", "L1") and keyboard equivalents ("Enter", "Esc", "Q/E"). L1/R1 not working outside games is a separate but related issue — some NavActions probably just aren't being processed in certain states. Both get fixed in the same session naturally. I'd put this in the polish phase, probably after the big features but before v1.0. It touches a lot of files but none deeply.

Trophy screenshot pop-up in Trophy Room:
This is a great idea and yes, I don't think you've asked before — it's on record now! When you select an earned trophy, a modal shows: your screenshot, trophy title and description, unlock requirements, points value, rarity/completion percentage (RA provides this), and hardcore vs softcore status. This fits perfectly into the Trophy Room polish pass. RA's API already returns rarity data so it's mostly UI work. I'd slot this into the same session as general Trophy Room polish.

---

# HAACKSTATION — SESSION 34 / 34b HANDOFF
*Date: May 16, 2026*
*Version at close: v0.5.2-dev*
*Sessions covered: 34 (Soft Reset + Hot-Swap) and 34b (OmniSave card rework + bug fixes)*

---

## Current Build Status

**VERIFIED WORKING (screenshots confirmed)**
- Soft Reset — amber confirm dialog, fires correctly, no audio glitch
- Memory card hot-swap — OmniSave card switcher pills render correctly
- Card Chronicle — renamed from "Time Machine", header and footer correct
- Card pill strip — Card 1 active (red highlight + bullet), Card 2/3 shown
- OmniSave sub-label — shows "Alundra — Card 1" correctly
- Y/Triangle → Card Chronicle navigation working
- Settings About tab — v0.5.2-dev confirmed
- Multi-disc (Legend of Dragoon) — still working

**KNOWN BUGS CARRIED INTO SESSION 35**

1. **False memcard toast on game boot (intermittent)**
   - Root cause: Hunk D (prime `m_sramChecksum` after `loadSaveRAM` in `launchGame()`)
     and Hunk E (`m_cardSwapToast = false` on launch) did not apply cleanly.
   - Fix is simple — see Session 35 priority queue below.
   - Affects some games on first boot, not all. LoD reliably triggers it.

2. **Rewind stops working after Soft Reset**
   - Root cause: `processInGameMenuActions()` SOFT_RESET branch calls
     `m_rewind->reset()` but never calls `m_rewind->init(m_core.get())`
     afterward. Buffer clears but never refills because core attachment is lost.
   - Fix is one added line — see Session 35 priority queue below.
   - Workaround: close and relaunch the game. Rewind works normally otherwise.

3. **Settings About tab doesn't scroll in windowed mode at small sizes**
   - Low priority. Content overflows when window height < about panel height.
   - No scroll input handling on that panel.
   - Fix: add scroll support to Settings About tab (clamp content, D-pad scrolls).

---

## Files Changed This Session

**Drop-in replacements (whole file):**
- `frontend/include/core_bridge/libretro_bridge.h` — added `softReset()` + `m_retro_reset` pointer
- `frontend/src/core_bridge/libretro_bridge.cpp` — LOAD_SYM for retro_reset, softReset() impl
- `frontend/include/ingame_menu.h` — MEMCARD_SWAP/CARD_PICKER removed; SOFT_RESET added; clean
- `frontend/src/ingame_menu.cpp` — card picker fully removed; soft reset confirm dialog added
- `frontend/include/ui/omnisave_vault.h` — GameCardSlot struct; CHRONICLE panel; consumeCardSwap()

**Patched files (str_replace hunks):**
- `frontend/src/ui/omnisave_vault.cpp` — loadGameCardSlots(), doSwapCard(), card pill strip
  rendering, TIMEMACHINE→CHRONICLE rename throughout, footer hints updated
- `frontend/src/app.cpp` — SOFT_RESET + MEMCARD_SWAP handlers; OMNISAVE_VAULT update block
  now handles consumeCardSwap(); launchGame card picker block removed

**app.h addition (applied):**
- `bool m_cardSwapToast = false;` near `m_trophyShotToastUntil`

---

## Architecture Notes

### InGameMenuAction enum (current)
```cpp
enum class InGameMenuAction {
    NONE,
    RESUME,
    OPEN_OMNISAVE,   // all save features including card swap
    SOFT_RESET,      // retro_reset() with amber confirm
    CHANGE_DISC,
    QUIT_TO_SHELF,
};
```
Memory card management was intentionally removed from the in-game menu.
OmniSave is the single entry point for all card operations.

### OmniPanel enum (current)
```cpp
enum class OmniPanel { MEMCARD, SAVESTATES, CHRONICLE };
// CHRONICLE = formerly TIMEMACHINE
```

### Card swap flow
1. User opens OmniSave → Memory Card panel
2. L/R navigates card pills (Card 1 •, Card 2, Card 3)
3. A on inactive card → amber "Swap to X?" confirm (SWAP_CARD action)
4. Confirm → `doSwapCard()` sets `m_pendingSwapPath`, sets `m_wantsClose = true`
5. `app.cpp` OMNISAVE_VAULT update block: `consumeCardSwap()` returns path
6. App: flush old card → switch `m_activeCardPath` → load new card → rewind reinit
7. Toast: "Card swapped — soft reset recommended" (4 seconds)

### GameCardSlot struct (new)
```cpp
struct GameCardSlot {
    std::string path;        // absolute .mcr path
    std::string displayName; // "Alundra — Card 1"
    int         slotNumber;  // 1-based suffix from filename
    bool        isActive;    // currently loaded in core
};
```
Populated by `loadGameCardSlots()` in OmniSave `open()`.
Filters `memcards/per_game/` by current game serial prefix.

---

## Session 35 Priority Queue

### IMMEDIATE — apply before anything else

**Fix 1: Rewind reinit after Soft Reset**
File: `frontend/src/app.cpp`
In `processInGameMenuActions()`, SOFT_RESET branch:

```cpp
// FIND:
        if (m_rewind) m_rewind->reset();

// REPLACE WITH:
        if (m_rewind) {
            m_rewind->reset();
            m_rewind->init(m_core.get());  // re-attach so buffer refills
        }
```

**Fix 2: Prime sramChecksum after loadSaveRAM in launchGame()**
File: `frontend/src/app.cpp`
In `launchGame()`, after the `loadSaveRAM` call:

```cpp
// FIND:
        if (!m_activeCardPath.empty())
            m_core->loadSaveRAM(m_activeCardPath);

// REPLACE WITH:
        if (!m_activeCardPath.empty()) {
            m_core->loadSaveRAM(m_activeCardPath);
            // Prime checksum so first poll doesn't fire a false LiveCard toast
            m_sramChecksum  = m_core->getSramChecksum();
            m_sramPollTimer = 0;
        }
```

**Fix 3: Reset m_cardSwapToast on game launch**
File: `frontend/src/app.cpp`
In `launchGame()`, near where `m_sessionStartTime` is set:

```cpp
// FIND:
        m_sessionStartTime = time(nullptr);

// REPLACE WITH:
        m_sessionStartTime = time(nullptr);
        m_cardSwapToast    = false;
```

---

### NEXT SESSION CANDIDATES (in rough priority order)

1. **Services tab** (formerly RetroAchievements tab)
   - Rename tab to "Services"
   - Each service (RA, ScreenScraper) as an expandable card within the tab
   - RA card: username, password/token, hardcore mode toggle (future), status
   - ScreenScraper card: username, password, region preference (for EU/JP users)
   - Design: selectable cards that expand in-place, not separate sub-screens
   - Files: `settings_screen.h/.cpp`

2. **Settings About tab scroll fix**
   - Add scroll input handling so content is reachable at small window sizes
   - File: `settings_screen.cpp` — About tab render/nav section

3. **chdman integration**
   - Tools section in Settings: batch compress entire ROM folder
   - Per-game option in Game Details Panel (right-click/Options on a game)
   - Restore flow: re-extract ISO from CHD, delete CHD (user-initiated undo)
   - Files: new `chdman_manager.h/.cpp`, `settings_screen.cpp`, `game_details_panel.cpp`

4. **Memory card operations (New / Copy / Delete)**
   - "+" in card pill strip → create blank Card N for current game
   - Options button on selected card → Copy / Delete menu
   - Delete: red confirm dialog, removes .mcr file
   - Copy: duplicates .mcr with next available slot number
   - Files: `omnisave_vault.h/.cpp`

5. **OmniSave: RA unlock timestamps in Trophy Vault** (was marked pending verify)

6. **Bottom-right message center** (new — session 35 discussion item)
   - Reserved for: trophy unlock popup, session playtime reminders, battery status
   - All opt-in
   - SDL2 `SDL_GetPowerInfo()` for system battery
   - Controller battery: platform-dependent, query what's available

7. **Trophy unlock popup**
   - Bottom-right, gold border, trophy icon, game + achievement name
   - ~4 second slide-in, auto-dismiss
   - Research: PS4/RetroArch/Playnite styles for reference

---

## Design Decisions Logged

**Naming**
- "Time Machine" → **Card Chronicle** (confirmed, working in UI)
- "RetroAchievements tab" → **Services tab** (decided this session)
- Bottom-right corner reserved for trophy/message center notifications

**Colour language (confirmed and in use)**
- Red `#DC5050` — destructive (delete)
- Amber `#DC8C00` — disruptive/warning (soft reset, overwrite, card swap)
- Gold `#D2AA32` — RetroAchievements, trophy
- Green `#3CB43C` — success, screenshots
- Cyan-Blue `#3CA0DC` — memory card saves, informational confirms
- Violet `#A050DC` — rewind, Card Chronicle, time-travel actions
- Bright Green `#3CC850` — turbo badge

**Icon/asset planning (decided this session)**
- Format: PNG, always. Alpha channel required.
- Master sizes: 128×128 (menu icons), 64×64 (toast icons), 256×96 (badges)
- Style decision pending: pixel art vs flat vector (leaning pixel art for PS1 theme)
- Rewind fill graphic: two-layer reveal via SDL_RenderSetClipRect()
  ("empty" texture base, "full" texture clipped to buffer fill %)
- Badge animations: sprite sheet for rewind reel spin; SDL_RenderCopyEx for
  FF pulse; opacity pulse for turbo "locked on" feel

**chdman design (decided this session)**
- Tools menu: batch operations on whole library
- Game Details Panel: per-game compress/restore option
- Restore = re-extract ISO + delete CHD (user-initiated, not surprising data loss)
- Move-to-folder approach (already in codebase) for ISO handling

**Guide system (decided this session)**
- `ps1_guide_index.json` ships with app, maps serials to Internet Archive URLs
- Pre-populate top ~50 PS1 games by historical sales
- Not bundling guide text — linking/fetching only (no ToS issues)
- On-demand download + local cache in `guides/<serial>/`
- Sessions: S37 (infrastructure + text viewer), S38 (in-game guide menu item)

**Memory card management scope**
- Current: card switcher (swap/reload), card Chronicle
- Next: New card, Copy card, Delete card
- Future: individual save browsing, copy/delete individual saves, rename (OSK)
- Individual save management requires MCR block parser — dedicated session

**Eden System / Accounts**
- "Services" tab = external services (RA, ScreenScraper, cloud sync future)
- "Accounts" reserved for future local Eden user profiles
- No collision between the two concepts

---

## Roadmap Additions (logged this session)
- Manuals via ScreenScraper PDF (S36)
- Strategy guide viewer with GameFAQs/Archive fetch (S37-38)
- In-game guide menu item (S38)
- Bottom-right message center / session playtime / battery (TBD)
- Trophy unlock popup (TBD, research first)
- ScreenScraper region/language preference (post-v1 localisation)

---

## What v0.6 Looks Like
Based on current trajectory, v0.6 will include:
- Services tab (RA + ScreenScraper credentials, region)
- chdman integration (batch + per-game)
- Memory card New/Copy/Delete
- Manual PDF viewer (MuPDF)
- Strategy guide viewer
- Trophy unlock popup
- Message center (playtime, battery)
- Settings About scroll fix

---

*End of Session 34/34b handoff.*
*Next session: start with the three immediate bug fixes above, verify compile,*
*then pick the next item from the priority queue.*

---

# HaackStation — Session 36 Handoff
*Date: May 2026 · Version: v0.5.2-dev · Prepared for Session 37*

---

## Session 36 Summary

Session 36 was primarily a **bug-fixing and stabilization session** focused on:
- Tracing and partially fixing the disc-swap LiveCard false toast issue
- Fixing the disc select nav bar layout pop (footer now always reserved)
- Roadmap and handoff doc cleanup (this document)

No new features were added this session.

---

## Current Build State

- **Version:** `v0.5.2-dev` (string in `settings_screen.cpp`, Settings > About tab)
- **Memcard mode:** `PER_GAME` (default, set in `app.cpp` init)
- **Build output:** `build/Release/Release/` (double-Release is known, low priority)
- **Compiles cleanly:** Yes, after orphan brace fix in `app.cpp` around line 1549

---

## What Works Solidly

- OmniSave Vault: memory card left panel, save states right panel, 50/50 layout
- SpriteCard animated icons (16×16, ~4fps, palette entry 0 transparent)
- Card Chronicle (Time Machine): snapshots accumulating correctly with paired screenshots
- Memory card entry delete (MCR directory chain rewritten)
- Confirm dialog before all destructive actions
- Disc select menu: cover slide + fan animation, nav bar no longer pops
- LiveCard detection: works correctly for normal in-game saves
- Multi-disc: disc select menu, disc memory (resumes on last disc played)
- RetroAchievements: login, achievement tracking, Trophy Hub, Trophy Room
- Play history, session time tracking
- Rewind (300 slots × 16MB), fast forward (4x hold), turbo mode

---

## Known Issue: Disc Swap LiveCard Toast

**Status:** Still firing false toasts on disc swap. Not fixed this session.

**Root cause (confirmed):** `OmniSaveVault` has a registered `loadSaveRAM` callback (app.cpp line ~195) that fires after disc load completes. This second SRAM load happens *after* `m_sramChecksum` is captured in the CHANGE_DISC handler, making the stored checksum stale. The poll then sees a mismatch on the next tick and fires a toast.

**Current mitigation in code:** `m_sramSettleUntil = SDL_GetTicks() + 3500` cooldown after disc swap — suppresses the poll for 3.5 seconds. Helps but doesn't fully eliminate it because the OmniSave callback timing is variable.

**Permanent fix:** When disc swap moves to open-tray/close-tray (no full core reload), the SRAM never changes during a swap and this entire issue disappears. **Do not spend more time on this — wait for the tray implementation.**

**Relevant members in `app.h`:**
```cpp
bool   m_suppressSramPoll = false;
Uint32 m_sramSettleUntil  = 0;
```

---

## Files Changed This Session

| File | Change |
|------|--------|
| `frontend/src/app.cpp` | Orphan brace fix ~line 1549; disc swap SRAM reset with settle timer; resume-on-disc SRAM reset; `m_cardSwapToast = false` in launchGame() |
| `frontend/include/app.h` | Added `m_suppressSramPoll`, `m_sramSettleUntil` |
| `frontend/src/ingame_menu.cpp` | Footer height always reserved (56px); footer always rendered (blank during animation phases) |

---

## Session 37 — Start Here

**Item 10: OmniSave — save state copy / branch**

"Branch from this point" — copy save state slot N to a new slot. Self-contained change in `omnisave_vault.cpp`. The UI already has the slot grid; this adds a copy action (hold Y or dedicated button) that duplicates the selected slot to the next empty one and shows a brief confirmation.

After that, the queue is:
- **10.1** Save import (watched `memcards/import/` folder)
- **11** Accounts tab (rename RA tab, add ScreenScraper credentials)
- **12** RA unlock timestamps (verify rendering in Trophy Vault)
- **14** HC/SC badge in Trophy Vault

See roadmap for full details.

---

## Architecture Reminders for Session 37

**OmniSave entry points:**
- Launched from in-game menu via `InGameMenuAction::OPEN_OMNISAVE`
- `AppState::OMNISAVE_VAULT` in app.cpp handles rendering + input
- `OmniSaveVault` in `omnisave_vault.h/.cpp`
- `MemCardManager` in `memcard_manager.h/.cpp` owns card parsing/writing

**Key paths (all relative to `build/Release/Release/`):**
- Per-game cards: `memcards/per_game/<SERIAL>_1.mcr`
- Card history: `memcards/history/<SERIAL>_1/<timestamp>.mcr` + `.png`
- Save states: `saves/states/<Game Title>/`
- RA cache: `saves/ra_achievements_<ID>.json`
- Play history: `saves/play_history.json`
- Disc memory: `saves/disc_memory.json`

**Version string location:** `settings_screen.cpp` → Settings > About tab

---

## Git Commit for Session 36

Paste into Git Bash from your project root:

```bash
cd /c/Users/digit/Documents/HaackStation

git add frontend/src/app.cpp
git add frontend/include/app.h
git add frontend/src/ingame_menu.cpp
git add HAACKSTATION_ROADMAP-v36.md
git add HANDOFF_SESSION36.md

git status

git commit -m "fix: disc swap SRAM settle timer, footer layout, session 36 stabilization

- Add m_sramSettleUntil 3.5s cooldown after disc swap to suppress false LiveCard toasts
- Add m_suppressSramPoll flag (in place for future use)
- Fix orphan closing brace in app.cpp ~line 1549 (duplicate SRAM prime block)
- Reset m_cardSwapToast = false in launchGame() after session timer
- ingame_menu: always reserve 56px footer height in renderDiscSelect()
- ingame_menu: always render footer (blank during animation, populated at SETTLED)
- Roadmap updated to v3 (sessions 1-9 marked complete, next = item 10)
- Session 36 handoff doc added"

git push origin main
```

---

## Roadmap File

The updated roadmap is `HAACKSTATION_ROADMAP-v36.md`. Sessions 1–9 are marked complete. Next session starts at item 10. The old `HAACKSTATION_ROADMAP-updated.md` can be deleted or archived.

---

*Session 36 complete. Next: Session 37 — OmniSave save state copy/branch (item 10).*
