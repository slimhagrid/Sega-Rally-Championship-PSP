/* Sega Rally PSP - frame time log, see perf.h */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>
#include "perf.h"
#include "gamedir.h"

#define PERF_PATH SEGA_BASE "/perf.txt"
#define SUMMARY_EVERY 300

static SceUID fd = -1;
static char buf[8192];
static int blen;
static struct { int n, slow, vb[4]; double work, logic, build, submit, ge, items, polys, tris; float max_work; int max_pass; } sum;

static void flush(void)                            /* open - append - close every time: the FAT only learns the new size at close, and the game is usually left with HOME */
{
    if (blen > 0) {
        SceUID f = sceIoOpen(PERF_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
        if (f >= 0) { sceIoWrite(f, buf, (unsigned)blen); sceIoClose(f); }
    }
    blen = 0;
}

static void put(const char *s, int n)
{
    if (n <= 0) return;
    if (blen + n > (int)sizeof buf) flush();
    memcpy(buf + blen, s, (size_t)n);
    blen += n;
}

void perf_open(int view_mode)
{
    fd = sceIoOpen(PERF_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoClose(fd); fd = -1; }
    char t[400];
    int n = snprintf(t, sizeof t, "# Sega Rally PSP perf log: cpu %d MHz, bus %d MHz, view mode %d\n# slow line: pass | work = logic + build + submit + ge (ms) | vblanks since last pass | items polys tris draws parts spray | seg mph lap\n",
                     scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(), view_mode);
    put(t, n < (int)sizeof t ? n : (int)sizeof t - 1);
    flush();
    memset(&sum, 0, sizeof sum);
}

static void summary(int pass)
{
    if (sum.n == 0) return;
    char t[400];
    double n = sum.n;
    int len = snprintf(t, sizeof t, "SUMMARY passes %d-%d: work avg %.1f max %.1f (pass %d) ms | logic %.1f build %.1f submit %.1f ge %.1f | vblanks 2:%d 3:%d 4+:%d | slow %d | items %.0f polys %.0f tris %.0f\n",
                       pass - sum.n + 1, pass, sum.work / n, sum.max_work, sum.max_pass, sum.logic / n, sum.build / n, sum.submit / n, sum.ge / n,
                       sum.vb[0], sum.vb[1], sum.vb[2] + sum.vb[3], sum.slow, sum.items / n, sum.polys / n, sum.tris / n);
    put(t, len < (int)sizeof t ? len : (int)sizeof t - 1);
    memset(&sum, 0, sizeof sum);
    flush();
}

void perf_frame(const PerfFrame *f)
{
    float work = f->logic_ms + f->build_ms + f->submit_ms + f->ge_ms;
    sum.n++;
    sum.work += work; sum.logic += f->logic_ms; sum.build += f->build_ms; sum.submit += f->submit_ms; sum.ge += f->ge_ms;
    sum.items += f->items; sum.polys += f->polys; sum.tris += f->tris;
    if (work > sum.max_work) { sum.max_work = work; sum.max_pass = f->pass; }
    sum.vb[f->vblanks <= 2 ? 0 : f->vblanks == 3 ? 1 : f->vblanks == 4 ? 2 : 3]++;
    if (work > 33.3f || f->vblanks > 2) {
        sum.slow++;
        char t[300];
        int len = snprintf(t, sizeof t, "slow %d | %.1f = %.1f + %.1f + %.1f + %.1f | vb %d | %d %d %d %d %d %d | %d %d %d\n", f->pass, work, f->logic_ms, f->build_ms, f->submit_ms, f->ge_ms,
                           f->vblanks, f->items, f->polys, f->tris, f->draws, f->nparts, f->spray, f->seg, f->mph, f->laps);
        put(t, len < (int)sizeof t ? len : (int)sizeof t - 1);
    }
    if (sum.n >= SUMMARY_EVERY) summary(f->pass);
}

void perf_close(void)
{
    flush();
}
