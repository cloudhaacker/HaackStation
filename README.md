# HaackStation 🎮

**A PlayStation 1 emulator frontend built for the community.**

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Android-lightgrey)]()
[![Status](https://img.shields.io/badge/status-Early%20Development-orange)]()

---

## What is HaackStation?

HaackStation is an open-source PlayStation 1 emulator frontend built on the
**Beetle PSX HW** libretro core — one of the most accurate PS1 emulation cores
ever written. The goal is a polished, controller-first experience with modern
features that the emulation community has been asking for.

**Runs on:** Windows, Linux, Android (Ayn Thor optimized)

**Plays:** ISO, BIN/CUE, CHD, M3U (multi-disc)

---

## Features (Planned / In Progress)

| Feature | Status |
|---|---|
| Controller-first UI (game shelf style) | 🔨 In Progress |
| ISO, BIN/CUE, CHD, M3U support | 🔨 In Progress |
| Texture replacement packs | 🔨 In Progress |
| On-the-fly audio replacement | 🔨 In Progress |
| Shader pack manager (RetroArch compatible) | 🔨 In Progress |
| AI texture upscaling (NCNN) | 📋 Planned |
| Netplay (rollback) | 📋 Planned |
| Android / Ayn Thor APK | 📋 Planned |
| Libretro core distribution | 📋 Planned |

---

## Accuracy

HaackStation inherits its emulation accuracy entirely from **Beetle PSX HW**.
This means games known for timing sensitivity run correctly:

- ✅ Bust A Groove (SPU interrupt timing)
- ✅ Valkyrie Profile (DMA quirks)
- ✅ Tales of Phantasia (CD seek behavior)
- ✅ Final Fantasy IX (GPU draw cycles)

---

## Credits & Transparency

**HaackStation is a community project.** Here is exactly what it is and isn't:

- **Emulation core:** Beetle PSX HW by the libretro team — all PS1 accuracy
  credit belongs entirely to them. This project would not exist without their work.
- **Project direction:** Initiated and directed by the HaackStation author.
- **Frontend & feature code:** Written with the assistance of Claude (Anthropic AI),
  directed and tested by the project author.
- **AI disclosure:** This project was developed using AI-assisted coding tools.
  We believe in being transparent about this. The human behind the project
  conceived the vision, made every design decision, and does all testing.

This is an honest experiment in what community members with vision but limited
coding experience can build with modern AI tools. We think that story is worth
telling openly.

---

## Assets & Attributions

| Asset | Credit | Source |
|---|---|---|
| Project logo | Generated with Google Gemini, directed by the project author | AI-generated |
| UI typography | Zrnic font by Apostrophic Labs | [DaFont](https://www.dafont.com/zrnic.font) |
| Emulation core | Beetle PSX HW team & libretro contributors | [GitHub](https://github.com/libretro/beetle-psx-libretro) |
| Frontend code | Claude (Anthropic AI) | AI-assisted |

**Font licence note:** Zrnic is listed as free for personal use on DaFont.
If HaackStation is ever distributed commercially the font licence should be
reviewed. For open-source community distribution it is used in good faith
with full credit given here.

---

## License

**GPLv2** — inherited from Beetle PSX HW.

You are free to use, study, modify and redistribute this software under the
terms of the GNU General Public License version 2. See LICENSE for details.

You may NOT use this project to distribute copyrighted BIOS files or game ROMs.

---

## Building

### Requirements
- CMake 3.20+
- SDL2
- Git

### Windows (Visual Studio / MSVC)
```bash
git clone https://github.com/cloudhaacker/HaackStation
cd HaackStation
git submodule update --init --recursive
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Linux
```bash
sudo apt install cmake libsdl2-dev libpng-dev libvorbis-dev
git clone https://github.com/cloudhaacker/HaackStation
cd HaackStation
git submodule update --init --recursive
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## BIOS

A PlayStation BIOS file is required to run games. HaackStation cannot
distribute BIOS files as they are copyrighted by Sony. Dump your own from
a PS1 console you own.

Place your BIOS in: `%APPDATA%\HaackStation\bios\` (Windows)
or `~/.config/haackstation/bios/` (Linux)

---

## Community

- Issues and suggestions: GitHub Issues
- Development diary: See `docs/HaackStation_DevDiary.docx`

---

*HaackStation is not affiliated with Sony Interactive Entertainment.*
*PlayStation is a registered trademark of Sony Interactive Entertainment LLC.*
