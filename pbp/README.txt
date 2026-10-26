SEGA RALLY PSP
==============

A native PSP port of Sega Rally Championship (Sega Saturn, USA, 1995): the game's engine is rewritten for the PSP, it is not an emulator. The game data is converted
from the user's own disc; nothing of the game is included in the source. This folder contains data made from that disc: keep it for yourself, do not share it.

INSTALL
  Copy the folder SegaRallyPSP (EBOOT.PBP and data/) to ms0:/PSP/GAME/ on the Memory Stick. About 100 MB.
  Built for the PSP's full memory (MEMSIZE); tested on a PSP and in PPSSPP, the older PSP-1000 has not been tried.
  Records, car set-ups and options are saved in ms0:/PSP/GAME/SegaRallyPSP/data/ (BUP0.BIN, BUP1.BIN, keycfg.txt). Copy those files to keep your records.

MENUS
  D-pad / analog nub  move          CROSS  confirm (the Saturn's C)      CIRCLE  back (B)
  SQUARE = A, TRIANGLE = X, SELECT = Y, L / R, START = start
  The mode select has Arcade, Time Attack, Car Settings, Records and Options (the original's 2 Player Battle is skipped: a PSP has one pad).

RACING  (Options > KEY CONFIG chooses the layout; the D-pad left / right and the analog nub always steer)
  TYPE A (default)  accelerate R, brake L or SQUARE, shift up TRIANGLE, shift down CROSS, change view CIRCLE
  TYPE B            accelerate CROSS, brake SQUARE or CIRCLE, shift up R, shift down L, change view TRIANGLE
  DIY               your own: on the KEY CONFIG page move to DIY and press CROSS; UP / DOWN choose a function, CROSS then the new buttons for it,
                    START to finish (SELECT = start again from type A). Saved in data/keycfg.txt.
  START in a race opens the pause menu (EXIT? NO / YES / RESTART).
  Replay (Time Attack): CROSS, CIRCLE or SQUARE switch between the TV cameras and the chase camera; TRIANGLE, L or R change its view.

SOUND
  Menu songs, the race's engine / tyre / wall sounds and the co-driver's voice come from the original's own sound driver; the CD tracks are played as MP3 files in data/cd.
  The music is made for the PSP's small speaker (bass and drums cut a little). Optional files in data/ (a text file with a number in it): cdvol = the music's level in percent,
  sndvol = the level of the sound driver's sounds. An empty file named nocd turns the CD music off.

IF SOMETHING IS WRONG
  An empty file data/sndlog makes the game write data/sndlog.txt, a timing log of the audio (late blocks, clipping); data/menu_unported.txt says where a screen handed over to something that
  is not ported. Send them with a description to the project's issue tracker.
