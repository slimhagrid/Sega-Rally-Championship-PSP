/* Sega Rally PSP - where the game's data lives on the memory stick.  The game reads and saves in ms0:/PSP/GAME/SegaRallyPSP/data; -DSEGA_BASE=... (CMakeLists.txt) builds for another folder (tools/run_ppsspp.ps1 -StickName). */
#ifndef GAMEDIR_H
#define GAMEDIR_H
#ifndef SEGA_BASE
#define SEGA_BASE "ms0:/PSP/GAME/SegaRallyPSP/data"
#endif
#endif
