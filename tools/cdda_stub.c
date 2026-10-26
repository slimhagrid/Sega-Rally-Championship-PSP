/* the PC tools that run the sound library (bgm_render, test_snd ..) have no CD audio: the PSP's src/cdda.c is replaced by empty calls */
#include "cdda.h"
void cdda_init(const char *dir, const char *logpath) { (void)dir; (void)logpath; }
void cdda_play(int track, int repeat) { (void)track; (void)repeat; }
void cdda_stop(void) { }
void cdda_pause(int on) { (void)on; }
void cdda_volume(int level) { (void)level; }
void cdda_shutdown(void) { }
