/* Development aid: a timing log of the audio (data/sndlog -> data/sndlog.txt), see sndlog.h.  The lines are formatted into a ring in memory by whichever thread has something to say and
 * written to the memory stick by a thread of its own at low priority, so a slow write never stalls the audio. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pspkernel.h>
#include "sndlog.h"

#define LINES 384
#define LEN 176

int sndlog_on;
static char ring[LINES][LEN];
static volatile unsigned head, tail, dropped;
static volatile char done[LINES];                   /* the line is formatted */
static unsigned long long t0;
static SceUID th = -1;
static volatile int quit;
static FILE *f;

unsigned long long sndlog_us(void) { return sceKernelGetSystemTimeWide(); }

void sndlog(const char *fmt, ...)
{
    char b[LEN - 16];
    va_list ap;
    unsigned long long t;
    unsigned i;
    int intr;
    if (!sndlog_on) return;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    t = (sceKernelGetSystemTimeWide() - t0) / 1000;
    intr = sceKernelCpuSuspendIntr();
    if (head - tail >= LINES) { dropped++; sceKernelCpuResumeIntr(intr); return; }
    i = head++ % LINES;
    sceKernelCpuResumeIntr(intr);
    snprintf(ring[i], LEN, "%7u.%03u %s\n", (unsigned)(t / 1000), (unsigned)(t % 1000), b);
    __sync_synchronize();
    done[i] = 1;
}

static int logger(SceSize args, void *argp)
{
    (void)args; (void)argp;
    while (!quit) {
        sceKernelDelayThread(1000000);
        while (tail != head && done[tail % LINES]) {
            fputs(ring[tail % LINES], f);
            done[tail % LINES] = 0;
            tail++;
        }
        if (dropped) { fprintf(f, "(%u lines dropped)\n", dropped); dropped = 0; }
        fflush(f);
    }
    return 0;
}

void sndlog_init(const char *dir)
{
    char p[300];
    FILE *t;
    snprintf(p, sizeof p, "%s/sndlog", dir);
    t = fopen(p, "r");
    if (!t) return;
    fclose(t);
    snprintf(p, sizeof p, "%s/sndlog.txt", dir);
    f = fopen(p, "w");
    if (!f) return;
    t0 = sceKernelGetSystemTimeWide();
    sndlog_on = 1;
    th = sceKernelCreateThread("sndlog", logger, 0x28, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}
