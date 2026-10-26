/* M9: the songs of the menu screens as pre-rendered streams.  The original plays them with its sound driver (a 68000 running the sequencer, ~20 SCSP voices); on the PSP that is a fifth
 * of the CPU while the menus draw.  tools/conv_bgm.sh renders each song once with the same driver on the PC (tools/bgm_render.c) and stores it as a 4 bit stream (tools/bgm_enc.c);
 * the sound library (snd_lib.c) starts / stops / fades the stream where it would have sent the play / stop / fade command to the driver.  Sounds without a stream keep going to the driver. */
#ifndef BGM_H
#define BGM_H

/* dir: the folder with bgm.idx and the .bgm files (data/snd); stereo_byte: the driver's stereo flag (bit 7 of the sound RAM byte 0x483: the song is rendered for both settings) */
void bgm_init(const char *dir, const volatile unsigned char *stereo_byte);
int  bgm_start(unsigned set, unsigned value);       /* the play command of sound set `set` with the id table value `value`: 1 = a stream plays it now (the driver must not) */
void bgm_stop(void);                                /* the driver's stop / reset */
void bgm_fade(int vol, int time);                   /* FUN_06016F3C (class 0): volume 0 .. 127, fade time in the driver's units (0 = at once) */
int  bgm_playing(void);
void bgm_mix(short *out, int frames);               /* the audio thread: adds `frames` stereo frames of the song to out (int16 interleaved) */
void bgm_shutdown(void);

#endif
