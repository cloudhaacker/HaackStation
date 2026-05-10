# HaackStation

<div align="center">

![HaackStation Logo](assets/icons/HaackStation_Logo.png)

**The premier PlayStation 1 frontend — user-friendly, feature-rich, free forever.**

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-informational)](https://github.com/cloudhaacker/HaackStation)
[![Core: Beetle PSX HW](https://img.shields.io/badge/Core-Beetle%20PSX%20HW-red)](https://github.com/libretro/beetle-psx-libretro)
[![AI Assisted](https://img.shields.io/badge/AI%20Assisted-Claude%20(Anthropic)-purple)](https://anthropic.com)

*Built on Beetle PSX HW / Mednafen via the libretro interface*

</div>

---

## What Is HaackStation?

HaackStation is an open source PlayStation 1 emulator frontend built on top of Beetle PSX HW. It wraps a proven, cycle-accurate emulation core in a modern, controller-first interface — not a fork of RetroArch, not a skin, but a fully hand-rolled UI and feature set built from scratch around the libretro API.

**Controller-first** — the entire UI is navigable with a gamepad. No mouse required. Every screen shows context-sensitive button hints so you're never guessing.

**Free forever** — GPLv2. No paywalls, no paid tiers, no exceptions.

**Openly AI-assisted** — developed with Claude (Anthropic AI) writing code under the direction of the project author. See [AI Assistance](#ai-assistance) below.

---

## Screenshots

| Game Shelf | Game Details Panel |
|:---:|:---:|
| ![Game Shelf](docs/screenshots/game_shelf.png) | ![Details Panel](docs/screenshots/details_panel.png) |

| RetroAchievements | Details — Dragon Warrior VII |
|:---:|:---:|
| ![RA Notification](docs/screenshots/ra_notification.png) | ![Dragon Warrior](docs/screenshots/details_dragon_warrior.png) |

---

## Feature Status

| Feature | Status |
|---------|--------|
| Game shelf with spring-scroll animations | ✅ Done |
| Cover art display (scraped from ScreenScraper.fr) | ✅ Done |
| Multi-disc support with GhostScan deduplication | ✅ Done |
| Controller navigation (Xbox/PS controllers) | ✅ Done |
| Keyboard navigation (conflict-free layout) | ✅ Done |
| Settings screen (6 tabs, all wired) | ✅ Done |
| Game Details Panel (screenshots, L1/R1 cycle) | ✅ Done |
| ScreenScraper.fr scraper (cover, back cover, screenshots) | ✅ Done |
| Save states with thumbnail screenshots | ✅ Done |
| In-game menu (Start+Y) | ✅ Done |
| RetroAchievements — full rcheevos rc_client integration | ✅ Done |
| RA — CHD file hashing via libchdr | ✅ Done |
| RA — per-game achievement persistence across sessions | ✅ Done |
| RA — Trophy Vault (per-game achievement grid) | ✅ Done |
| RA — Trophy Hub (all-games overview, alphabetical) | ✅ Done |
| RA — unlock notifications with badge images | ✅ Done |
| Fast Boot (skip PS1 BIOS logo) | ✅ Done |
| Fast Forward (hold R2 / F key, 2×–8×) | ✅ Done |
| Play history (recently played tracking) | ✅ Done |
| Rewind (hold L2) | ✅ Done |
| Memory card manager (shared + per-game) | ✅ Done |
| Input remapping UI | ✅ Done |
| Mouse cursor auto-hide in fullscreen | ✅ Done |
| OmniSave & SpriteCard (save state manager) | 🔨 In Progress |
| ReScore (in-game audio replacement) | 🔨 In Progress |
| chdman integration (in-app CHD conversion) | 🔨 In Progress |
| RA achievement unlock submissions | ⏳ Pending RA approval (~July 2026) |
| Trophy Room visual polish pass | 📋 Planned |
| Text input dialog (on-screen keyboard) | 📋 Planned |
| Favorites system | 📋 Planned |
| Color themes | 📋 Planned |
| Ambient music player | 📋 Planned |
| Android / Ayn Thor APK | 📋 Phase 5 |
| OpenGL hardware renderer (PGXP, upscaling) | 📋 Phase 5 |
| Netplay | 📋 Phase 5 |

---

## Named Features

### 📚 HaackStack with GhostScan
The library manager. Scans directories for BIN/CUE and CHD files, deduplicates multi-disc sets via **GhostScan** (Disc 2 doesn't clutter the shelf as a separate entry), and presents a clean alphabetical game browser. Auto-generates M3U playlists for multi-disc games.

Supported naming patterns: `(Disc N)`, `(Disk N)`, `(CD N)`, `Disc N`, `CD N`

### 🏆 Trophy Vault & Trophy Hub
Full RetroAchievements integration powered by rcheevos 12.3.

**Trophy Vault** — per-game achievement browser with a badge grid, filter tabs (All / Unlocked / Locked via L1/R1), progress bar, and a detail strip showing the selected achievement's full badge, description, and point value. Greyscale/locked badge variants generated automatically if only the colour badge is cached.

**Trophy Hub** — all-games overview sorted alphabetically (matches the shelf). Shows cover art, unlock progress, recent badge strip (up to 5 most recent unlocks), and a global completion bar across your entire collection. Drill into any game's Trophy Vault directly. Footer: *Powered by RetroAchievements*.

Both BIN/CUE and CHD formats hash correctly for RA identification. Achievement data persists between sessions in `saves/ra_achievements_<gameId>.json` so trophies load instantly even when no game is running.

### 🎮 OmniSave & SpriteCard *(in progress)*
The save state manager. OmniSave handles the slots; SpriteCard will render animated PS1 memory card sprite previews so you can see what's in each slot at a glance.

### 🎵 ReScore *(in progress)*
Audio replacement — swap a game's original soundtrack with custom audio.

---

## Building from Source

### Requirements

- Windows 10/11
- Visual Studio 2022+ Build Tools (MSVC)
- CMake 3.20+
- Git

### Dependencies (place in `deps/`)

```
deps/
  SDL2/          — SDL2 VC development libraries
  SDL2_ttf/      — SDL2_ttf VC development libraries
  SDL2_image/    — SDL2_image VC development libraries
  rcheevos/      — clone from github.com/RetroAchievements/rcheevos
  libchdr/       — clone from github.com/rtissera/libchdr (CHD hashing)
  soundtouch/    — SoundTouch audio library (ReScore)
```

**SDL2 header fix (required):** The SDL2 VC zip puts headers directly in `include/`. Create `include/SDL2/` subfolders and move the headers in — the code uses `#include <SDL2/SDL.h>` style includes.

### Build

```cmd
cmake -B build -S .
cmake --build build --config Release
```

CMake should confirm:
```
-- libchdr found — CHD hashing enabled
-- rcheevos sources: ...
```

Clean rebuild:
```cmd
rd /s /q build
cmake -B build -S .
cmake --build build --config Release
```

Output: `build/frontend/Release/HaackStation.exe`

---

## Runtime Layout

```
HaackStation.exe
core/
  mednafen_psx_hw_libretro.dll    ← Beetle PSX HW core
bios/
  scph1001.bin                    ← PS1 BIOS (US, recommended)
  scph5500.bin                    ← PS1 BIOS (JP, optional)
  scph5501.bin                    ← PS1 BIOS (US v2, optional)
  scph5502.bin                    ← PS1 BIOS (EU, optional)
assets/
  fonts/zrnic.otf
  icons/HaackStation_Logo.png
saves/
  trophy_hub.json                 ← all-games trophy summary
  ra_achievements_<id>.json       ← per-game achievement cache
memcards/
  shared/
  per_game/
media/
  badges/                         ← RA badge images (auto-downloaded)
  <game_title>/
    cover.*
    screenshots/
```

> **BIOS files are not included** and are never distributed with this project. Supply your own, legally dumped from hardware you own.

---

## Configuration

Config file: `%APPDATA%\HaackStation\haackstation.cfg`

```ini
[General]
roms_path=C:\Emulation\roms\psx
bios_path=
fullscreen=false
vsync=true
show_fps=false

[Emulation]
fast_boot=false
fast_forward_speed=1    ; 0=2x  1=4x  2=6x  3=8x

[Video]
renderer=0
internal_res=1
shader=0

[Audio]
audio_volume=100

[Scraper]
ss_user=your_screenscraper_username
ss_password=your_screenscraper_password

[RetroAchievements]
ra_user=your_ra_username
ra_api_key=              ; auto-populated after first login
ra_password=             ; used for initial login only, cleared after token saved
ra_hardcore=false
```

---

## Keyboard Controls

The keyboard layout has no conflicts — WASD is reserved exclusively for in-game PS1 buttons.

### On the Game Shelf
| Key | Action |
|-----|--------|
| Arrow Keys | Navigate |
| X | Launch game |
| Enter | Open Settings |
| F2 | Open Game Details |
| Escape | Quit |
| F11 | Toggle Fullscreen |

### In-Game
| Key | PS1 Button |
|-----|-----------|
| Arrow Keys | D-pad |
| X | Cross (×) |
| Z | Circle (○) |
| A | Square (□) |
| S | Triangle (△) |
| Q | L1 |
| W | R1 |
| E | L2 |
| R | R2 |
| Enter | Start |
| Space | Select |
| F (hold) | Fast Forward |
| F1 | In-game Menu |
| Escape | Quit to Shelf |

### In Game Details Panel
| Key | Action |
|-----|--------|
| Arrow Keys | Navigate |
| Page Up / Page Down | Cycle screenshots |
| X | Select |
| Z | Close panel |

---

## Cover Art & Screenshots

HaackStation scrapes from [ScreenScraper.fr](https://www.screenscraper.fr) — a free community database. Register a free account for higher rate limits.

**To scrape:** Settings → General → Scrape Game Art. Enter your ScreenScraper credentials in the config first.

**Manual screenshots:** Drop any `.jpg` or `.png` into `media/screenshots/[game title]/` and they appear in the Details Panel immediately. Cycle them with L1/R1 from any navigation state within the panel.

---

## RetroAchievements

Full rcheevos 12.3 integration using the rc_client API. Login, ROM hashing (BIN/CUE and CHD), achievement tracking, badge downloads, and unlock notifications all work. The game-load notification shows the game's RA icon; achievement unlock notifications show the badge image with title and point value.

**Current limitation:** Unlock submissions are blocked server-side pending official RA emulator recognition. Applied and waiting on the 6-month public availability requirement (~July 2026). Infrastructure is complete — unlocks activate automatically once approved.

Enter credentials in Settings → RetroAchievements. Session token is saved after first login; the password field can then be cleared.

---

## Supported Formats

| Format | Support |
|--------|---------|
| BIN/CUE | ✅ Full |
| CHD | ✅ Full (including RA hashing) |
| ISO | ✅ Full |
| M3U | ✅ Full (multi-disc playlists) |
| PBP | ❌ Not supported |

---

## Project Structure

```
HaackStation/
├── frontend/
│   ├── include/
│   │   ├── ui/           — game browser, trophy hub/room, settings, nav
│   │   ├── library/      — game scanner, disc formats
│   │   ├── renderer/     — theme engine
│   │   └── core_bridge/  — libretro bridge
│   └── src/              — implementation (mirrors include/)
├── deps/                 — third-party libraries (not committed)
├── assets/               — fonts, icons, logo
└── docs/                 — documentation and dev diary
```

---

## AI Assistance

HaackStation is openly AI-assisted. Full accounting:

- **Vision, design, testing:** John Haack (project author)
- **Frontend code:** Claude (Anthropic AI), directed by the author
- **Emulation core:** Beetle PSX HW by the libretro team (unmodified)
- **Logo/artwork:** Google Gemini AI, directed and refined by the author
- **Font (Zrnic):** Apostrophic Labs — [dafont.com/zrnic.font](https://www.dafont.com/zrnic.font)

Every design decision, feature request, and architectural choice came from the project author. The AI writes code under direction. This is disclosed openly because transparency about tooling is the right thing to do.

The full development story — build battles, design decisions, and the honest grind of getting CHD hashing working — is documented in the [Development Diary](docs/HaackStation_DevDiary_Complete.docx).

---

## Credits

**Beetle PSX HW / Mednafen** — the libretro team and all Mednafen contributors. Every accurate frame HaackStation renders is their achievement.

**Libraries:** SDL2, SDL2_ttf, SDL2_image (zlib) · rcheevos 12.3 (MIT) · libchdr (BSD) · SoundTouch (LGPL)

To the emulation community, to RetroAchievements for building something worth achieving in, and to everyone who has kept PlayStation 1 gaming alive for 30 years.

---

## Legal

Licensed under the **GNU General Public License v2.0**, inherited from Beetle PSX HW.

PlayStation is a registered trademark of Sony Interactive Entertainment LLC. HaackStation is not affiliated with Sony Interactive Entertainment.

BIOS files and game ROMs are never distributed with this project.
