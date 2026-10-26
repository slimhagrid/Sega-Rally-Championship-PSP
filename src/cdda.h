/* M9: the CD audio of the original (16 tracks of music and jingles the game starts with FUN_0600F756: track, repeat count) as MP3 files played by the PSP's decoder (sceMp3, hardware: the main CPU
 * only feeds the file).  tools/conv_cdda.py makes data/cd/trackNN.mp3 from the disc's tracks.  On the Saturn the drive cannot read data and play audio at the same time: a play ends with the next
 * access to a disc file (sat_cd_hook in sat.c), exactly where the original's CD music is cut off. */
#ifndef CDDA_H
#define CDDA_H

void cdda_init(const char *dir, const char *logpath);   /* dir: data/cd; logpath: development log or NULL */
void cdda_play(int track, int repeat);              /* FUN_0600F756: disc track 2 .. 17; repeat 0 = once, 1 .. 14 = that many more times, 15 = for ever */
void cdda_stop(void);                               /* the CD drive is used for data */
void cdda_pause(int on);                           /* the race's pause menu: the music stands still and goes on from there */
void cdda_set_gain(int percent);                    /* 0 .. 100: the music's level on top of the game's own (data/cdvol) */
void cdda_volume(int level);                        /* FUN_06016FA4: the byte the game sends (bits 7 .. 5 = the level of the CD input, 0xC0 = 6 of 7) */
void cdda_info(int *ok, int *track, unsigned *frames, int *level);      /* development (sndlog): the player's state */
void cdda_shutdown(void);

#endif
