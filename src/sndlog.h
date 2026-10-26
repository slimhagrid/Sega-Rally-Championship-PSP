/* Development aid: data/sndlog (an empty file) switches on data/sndlog.txt, a timing log of the audio: every second the audio thread writes how many blocks it made, the longest gap between
 * them, how many came too late (that is a crackle), how long a block took to make, how much clipped; late blocks, the sound library's calls and the music's reads of the memory stick are
 * logged as they happen.  Needs no other file.  data/nocd (an empty file) turns the CD music off to tell the music from the driver's sounds. */
#ifndef SNDLOG_H
#define SNDLOG_H

#if defined(__PSP__) || defined(PSP)
extern int sndlog_on;
void sndlog_init(const char *dir);                  /* looks for dir/sndlog */
void sndlog(const char *fmt, ...);                  /* any thread; a line with the time in seconds */
unsigned long long sndlog_us(void);                 /* a microsecond clock */
#else                                               /* the host tests: nothing */
#define sndlog_on 0
static inline void sndlog(const char *fmt, ...) { (void)fmt; }
static inline unsigned long long sndlog_us(void) { return 0; }
#endif

#endif
