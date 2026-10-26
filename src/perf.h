/* Sega Rally PSP - frame time log for real-hardware runs: writes data/perf.txt on the Memory Stick (hand it over after a run).
 * One line per slow pass (work time above one 30 Hz frame or a missed vblank) and a summary every 300 passes. */
#ifndef PERF_H
#define PERF_H

typedef struct {
    int pass;
    float logic_ms, build_ms, submit_ms, ge_ms;     /* car + camera + HUD upkeep, scene assembly, display list build, wait for the GE */
    int vblanks;                                    /* vblanks since the previous pass (2 = 30 Hz) */
    int items, polys, tris, draws, nparts, spray;
    int seg, mph, laps;
} PerfFrame;

void perf_open(int view_mode);
void perf_frame(const PerfFrame *f);                /* call once per pass; flushes to the file when needed */
void perf_close(void);

#endif
