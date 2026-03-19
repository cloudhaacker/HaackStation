//  HaackStation — PlayStation 1 Emulator Frontend
//  Built on Mednafen/Beetle PSX HW (libretro core, GPLv2)
//
//  Project:     github.com/cloudhaacker/HaackStation
//  Core credit: Beetle PSX HW / Mednafen — libretro team
//  Logo:        Generated with Google Gemini
//  Font:        Zrnic by Apostrophic Labs (dafont.com/zrnic.font)
//  Code:        Claude (Anthropic AI), directed by the project author
//  License:     GPLv2

#include "app.h"
#include <SDL2/SDL.h>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#else
int main(int /*argc*/, char* /*argv*/[]) {
#endif
    try {
        HaackApp app;
        return app.run();
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "HaackStation — Fatal Error",
            e.what(),
            nullptr
        );
        std::cerr << "[HaackStation] Fatal: " << e.what() << "\n";
        return 1;
    }
}
