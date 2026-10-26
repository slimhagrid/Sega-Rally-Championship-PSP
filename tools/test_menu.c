/* M8: checks the ported menu code (src/sat*.c, src/menu_*.c) against the oracle trace (tools/menu_oracle.py, MTR1 files).
 *
 *   test_menu <trace.mtr> <datadir> [maxdiffs]
 *
 * boot: sat_boot() must leave exactly the memory the original left (all regions of the trace except IGNORE);
 * then the main loop runs with the trace's pad words and after every V-blank tick the regions are compared again.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../src/sat.h"
#include "../src/satlib.h"
#include "../src/satmenu.h"
#include "../src/menu_ending.h"
#ifdef PROF
#include <time.h>
#include "../src/vdpr.h"
#include "../src/menu_pspfix.h"
extern int sat_v2_touched;
/* -DPROF (tools/menu_prof.sh): also draws the picture the PSP shows (pspfix + software renderer) on every second tick and times it; "prof:" line at the end */
static VdpFrame prof_fr, prof_hi;
static int prof_use_hi;
static double prof_fix, prof_ren, prof_max, prof_over[4];
static unsigned prof_n;
static double prof_last, prof_lsum, prof_lmax, prof_lover[3];      /* the ticks' own code (everything but the picture): sum, worst, ticks over 0.1 / 0.25 / 0.5 ms */
static unsigned prof_lmax_tick, prof_lvec, prof_lticks, prof_touched, prof_same;
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static unsigned long long clk_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec; }
#endif

typedef struct { u32 addr, size; u8 *exp; } Region;
typedef struct { u32 lo, hi; const char *why; } Ign;

static const Ign IGN[] = {
    { 0x0603D880, 0x0603DB60, "CD / SMPC library variables (not modelled)" },
    { 0x002A4000, 0x002A5000, "slave mailbox, CD library buffers" },
    { 0x060A0000, 0x060A0001, "main loop copy (code)" },
    { 0x060A0002, 0x060A0013, "main loop copy (code)" },
    { 0x060A0014, 0x060A001C, "main loop copy (code)" },
    { 0x060A0020, 0x060A0030, "main loop copy (code / pool)" },
    { 0x060D0100, 0x060E0000, "polygon pipeline work areas (mkpoly / zsort), not ported: the 3D parts are drawn by the PSP renderer" },
};
#define NIGN (sizeof(IGN) / sizeof(IGN[0]))

static Region *reg;
static u32 nreg;
static u8 *tr;               /* decompressed trace */
static size_t trlen, trpos;
static u32 ntick, tick_i;
static u32 pad_next;
static u32 start_vector, npatch;
static const u8 *patches;
static int maxdiffs = 24;
static jmp_buf done;
static unsigned failed;

/* a scenario may end where the screen hands over to a screen that is not ported (the race start, a screen of another package): the main loop dispatches the new main vector
 * after the last recorded tick.  Anything else that is unported is an error. */
static u32 fin_vec, fin_n;                  /* optional trailer of the trace (FIN1): the memory changes between the last tick and the dispatch of fin_vec */
static size_t fin_pos;
static int compare(const char *what, u32 tick);
static void final_compare(u32 addr)
{
    size_t pos = fin_pos;
    u32 c, i;
    for (c = 0; c < fin_n; c++) {
        u32 a = tr[pos] | (tr[pos + 1] << 8) | (tr[pos + 2] << 16) | ((u32)tr[pos + 3] << 24);
        u32 n = tr[pos + 4] | (tr[pos + 5] << 8) | (tr[pos + 6] << 16) | ((u32)tr[pos + 7] << 24);
        pos += 8;
        for (i = 0; i < nreg; i++)
            if (a >= reg[i].addr && a + n <= reg[i].addr + reg[i].size) { memcpy(reg[i].exp + (a - reg[i].addr), tr + pos, n); break; }
        pos += n;
    }
    if (compare("final", tick_i)) { failed = 1; longjmp(done, 2); }
    printf("exit stores identical (dispatch of %08x)\n", (unsigned)addr);
}

/* called by menu_call() first: a trace with a trailer ends when the main loop dispatches the vector the oracle stopped at (whether that screen is ported or not):
 * the memory changes the last screen made after the last tick are compared then */
static void call_hook(u32 addr)
{
    if (fin_pos && tick_i >= ntick && addr == fin_vec && addr == R32(0x060A001C)) {
        final_compare(addr);
        longjmp(done, 1);
    }
}

static void unported_hook(u32 addr)
{
    if (tick_i >= ntick && addr == R32(0x060A001C)) longjmp(done, 1);
}

static u32 dyn_ign[16][2];                              /* ranges a scenario leaves out (the pseudo patch 0xFFFFFE00, tools/menu_drive.py IGNORE) */
static unsigned ndyn_ign;
static struct { u32 tick, addr, n; const u8 *data; } pokes[64];     /* memory the scenario changes at the end of a tick (the pseudo patch 0xFFFFFD00, tools/menu_drive.py POKE): what a component the port does not run would write */
static unsigned npokes;

static int ignored(u32 a)
{
    unsigned i;
    for (i = 0; i < NIGN; i++) if (a >= IGN[i].lo && a < IGN[i].hi) return 1;
    for (i = 0; i < ndyn_ign; i++) if (a >= dyn_ign[i][0] && a < dyn_ign[i][1]) return 1;
    return 0;
}

static u32 rd32(void) { u32 v = tr[trpos] | (tr[trpos + 1] << 8) | (tr[trpos + 2] << 16) | ((u32)tr[trpos + 3] << 24); trpos += 4; return v; }
static u32 rd16(void) { u32 v = tr[trpos] | (tr[trpos + 1] << 8); trpos += 2; return v; }

static u8 *host_ptr(u32 a) { return sat_ptr(a); }

static int compare(const char *what, u32 tick)
{
    u32 k, i;
    int ndiff = 0;
#ifdef PROF
    return 0;                                           /* pspfix writes into the VDP2 memory: the picture timing run does not compare */
#endif
    for (k = 0; k < nreg; k++) {
        const u8 *got = host_ptr(reg[k].addr);
        for (i = 0; i < reg[k].size; ) {
            if (reg[k].exp[i] != got[i] && !ignored(reg[k].addr + i)) {
                u32 j = i;
                while (j + 1 < reg[k].size && reg[k].exp[j + 1] != got[j + 1] && !ignored(reg[k].addr + j + 1)) j++;
                if (ndiff < maxdiffs) {
                    u32 n = j - i + 1, m;
                    printf("  %s tick %u: %08x..%08x (%u bytes)  expected", what, tick, reg[k].addr + i, reg[k].addr + j, n);
                    for (m = 0; m < n && m < 12; m++) printf(" %02x", reg[k].exp[i + m]);
                    printf("  got");
                    for (m = 0; m < n && m < 12; m++) printf(" %02x", got[i + m]);
                    printf("\n");
                }
                ndiff++;
                i = j + 1;
            } else {
                i++;
            }
        }
    }
    if (ndiff) printf("  %s tick %u: %d differing runs\n", what, tick, ndiff);
    if (ndiff && getenv("MENU_DUMP")) {                 /* MENU_DUMP=hexaddr:len[,hexaddr:len..]: the expected and the got bytes of those ranges (dev aid) */
        const char *s = getenv("MENU_DUMP");
        while (*s) {
            unsigned long a = strtoul(s, (char **)&s, 16), n = 0, j;
            if (*s == ':') n = strtoul(s + 1, (char **)&s, 16);
            for (k = 0; k < nreg; k++)
                if (a >= reg[k].addr && a + n <= reg[k].addr + reg[k].size) {
                    printf("  dump %08lx expected", a);
                    for (j = 0; j < n; j++) printf(" %02x", reg[k].exp[a - reg[k].addr + j]);
                    printf("\n  dump %08lx got     ", a);
                    for (j = 0; j < n; j++) printf(" %02x", host_ptr((u32)a)[j]);
                    printf("\n");
                }
            if (*s == ',') s++; else break;
        }
    }
    return ndiff;
}

static void host_tick(unsigned rin, unsigned rout)
{
    u32 pad, vec, nch, c, i;
    (void)rin; (void)rout;
    if (tick_i >= ntick) { if (getenv("MENU_DBG")) fprintf(stderr, "extra tick requested at vector %08x\n", (unsigned)R32(0x060A001C)); longjmp(done, 1); }
    pad = rd16(); (void)pad;
    if (tr[trpos] != rin || tr[trpos + 1] != rout) {
        printf("  main tick %u: V-blank recipes differ: expected in %u out %u, got in %u out %u\n", tick_i, tr[trpos], tr[trpos + 1], rin, rout);
        failed = 1;
        longjmp(done, 2);
    }
    trpos += 2;                         /* recipe bytes */
    vec = rd32(); (void)vec;
    nch = rd32();
    for (c = 0; c < nch; c++) {
        u32 a = rd32(), n = rd32();
        for (i = 0; i < nreg; i++)
            if (a >= reg[i].addr && a + n <= reg[i].addr + reg[i].size) { memcpy(reg[i].exp + (a - reg[i].addr), tr + trpos, n); break; }
        trpos += n;
    }
    if (compare("main", tick_i)) { failed = 1; longjmp(done, 2); }
    {
        unsigned pk;
        for (pk = 0; pk < npokes; pk++)
            if (pokes[pk].tick == tick_i) { u32 j; for (j = 0; j < pokes[pk].n; j++) W8(pokes[pk].addr + j, pokes[pk].data[j]); }
    }
    tick_i++;
#ifdef PROF
    {
        double tn = now_ms(), dl = prof_last ? tn - prof_last : 0;
        if (prof_last) {
            prof_lsum += dl; prof_lticks++;
            if (dl > prof_lmax) { prof_lmax = dl; prof_lmax_tick = tick_i; prof_lvec = R32(0x060A001C); }
            if (dl > 0.1) prof_lover[0]++;
            if (dl > 0.25) prof_lover[1]++;
            if (dl > 0.5) prof_lover[2]++;
        }
    }
    if (tick_i & 1) {
        double t0 = now_ms(), t1, t2;
        if (sat_v2_touched || sat_v2_dirty_any) { if (pspfix_apply()) sat_v2_touched = 1; prof_touched++; }
        t1 = now_ms();
        if (prof_use_hi) { vdpr_hi = &prof_hi; vdpr_hi_prio = (int)(sat->v2reg[0xF1] & 7); }      /* VDPR_HI=1: the picture in two parts at the sprite priority, as the host draws a screen with 3D models */
        vdp_render_frame(&prof_fr);
        vdpr_hi = NULL;
        t2 = now_ms();
        prof_fix += t1 - t0; prof_ren += t2 - t1; prof_n++; prof_same += prof_fr.same;
#ifdef VLOG_HOOK
        VLOG_HOOK(prof_n, t2 - t0, (unsigned)R32(0x060A001C));
#endif
        if (getenv("PROF_LOG")) {                       /* PROF_LOG=file: "tick picture ms main-vector screen-vector" for every picture */
            static FILE *lf;
            if (!lf) lf = fopen(getenv("PROF_LOG"), "w");
            static unsigned long long pv[VDPR_NPARTS];
            int pk;
            if (lf) {
                fprintf(lf, "%u %u %.3f %08x %08x", tick_i, prof_n, t2 - t0, (unsigned)R32(0x060A001C), (unsigned)R32(0x060A8800));
                for (pk = 0; pk < VDPR_NPARTS; pk++) fprintf(lf, " %.3f", (double)(vdpr_t[pk] - pv[pk]) / 1e6);
                fprintf(lf, "\n");
            }
            for (pk = 0; pk < VDPR_NPARTS; pk++) pv[pk] = vdpr_t[pk];
        }
        if (getenv("PROF_HASH")) {                      /* PROF_HASH=file: one hash of the picture per line (compare two renderers); PROF_SHOT=n: also picture n as P6 to PROF_HASH.ppm */
            static FILE *hf;
            u32 hsh = 2166136261u;
            int yy, xx;
            for (yy = 0; yy < prof_fr.h; yy++) for (xx = 0; xx < prof_fr.w; xx++) hsh = (hsh ^ prof_fr.pix[yy * VDPR_MAXW + xx]) * 16777619u;
            if (prof_use_hi) for (yy = 0; yy < prof_hi.h; yy++) for (xx = 0; xx < prof_hi.w; xx++) hsh = (hsh ^ prof_hi.pix[yy * VDPR_MAXW + xx]) * 16777619u;
            if (!hf) hf = fopen(getenv("PROF_HASH"), "w");
            fprintf(hf, "%u %08x\n", prof_n, hsh);
            if (getenv("PROF_SHOT") && (unsigned)atoi(getenv("PROF_SHOT")) == prof_n) {
                char pp[600];
                FILE *pf;
                {                                       /* the raw pixels (352 x 224 u32 each: the picture, and the upper part of a two-part picture) for tools that compare two renderers */
                    FILE *rf;
                    snprintf(pp, sizeof pp, "%s.fr.raw", getenv("PROF_HASH"));
                    if ((rf = fopen(pp, "wb")) != NULL) { fwrite(prof_fr.pix, 4, VDPR_MAXW * VDPR_MAXH, rf); fclose(rf); }
                    snprintf(pp, sizeof pp, "%s.hi.raw", getenv("PROF_HASH"));
                    if ((rf = fopen(pp, "wb")) != NULL) { fwrite(prof_hi.pix, 4, VDPR_MAXW * VDPR_MAXH, rf); fclose(rf); }
                }
                snprintf(pp, sizeof pp, "%s.ppm", getenv("PROF_HASH"));
                pf = fopen(pp, "wb");
                fprintf(pf, "P6\n%d %d\n255\n", prof_fr.w, prof_fr.h);
                for (yy = 0; yy < prof_fr.h * prof_fr.w; yy++) { u32 c = prof_fr.pix[(yy / prof_fr.w) * VDPR_MAXW + yy % prof_fr.w]; fputc(c & 255, pf); fputc((c >> 8) & 255, pf); fputc((c >> 16) & 255, pf); }
                fclose(pf);
            }
            if (hf) fflush(hf);
        }
        if (t2 - t0 > prof_max) prof_max = t2 - t0;
        if (t2 - t0 > 0.25) prof_over[0]++;
        if (t2 - t0 > 0.5) prof_over[1]++;
        if (t2 - t0 > 1.0) prof_over[2]++;
    }
    prof_last = now_ms();
#endif
    if (getenv("MENU_PROGRESS") && tick_i % 50 == 0) { fprintf(stderr, "tick %u vec %08x\n", tick_i, (unsigned)R32(0x060A001C)); }
    /* the pad of the next tick */
    if (tick_i < ntick) pad_next = tr[trpos] | (tr[trpos + 1] << 8);
    sat_pad = pad_next;
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *raw;
    long rawlen;
    z_stream z;
    u32 k;
    int rc;
    setvbuf(stdout, NULL, _IONBF, 0);
    menu_anim_log = 1;                                  /* the ceremony's animated model logs its matrices where the oracle's hook of draw_polygon2 does (tools/scn_ending.py) */
    if (argc < 3) { fprintf(stderr, "usage: test_menu trace.mtr datadir [maxdiffs]\n"); return 1; }
    if (argc > 3) maxdiffs = atoi(argv[3]);
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); rawlen = ftell(f); fseek(f, 0, SEEK_SET);
    raw = (unsigned char *)malloc(rawlen);
    if (fread(raw, 1, rawlen, f) != (size_t)rawlen) return 1;
    fclose(f);
    if (memcmp(raw, "MTR1", 4)) { fprintf(stderr, "not an MTR1 file\n"); return 1; }
    memset(&z, 0, sizeof(z));
    inflateInit(&z);
    trlen = 64u << 20;
    tr = (u8 *)malloc(trlen);
    z.next_in = raw + 4; z.avail_in = (uInt)(rawlen - 4);
    z.next_out = tr; z.avail_out = (uInt)trlen;
    if (inflate(&z, Z_FINISH) != Z_STREAM_END) { fprintf(stderr, "inflate failed\n"); return 1; }
    trlen = z.total_out;
    trpos = 0;
    start_vector = rd32();
    npatch = rd32();
    patches = (u8 *)tr + trpos;
    for (k = 0; k < npatch; k++) { u32 a = rd32(), n = rd32(); trpos += n; (void)a; }
    nreg = rd32();
    reg = (Region *)calloc(nreg, sizeof(Region));
    for (k = 0; k < nreg; k++) { reg[k].addr = rd32(); reg[k].size = rd32(); }
    for (k = 0; k < nreg; k++) { reg[k].exp = (u8 *)malloc(reg[k].size); memcpy(reg[k].exp, tr + trpos, reg[k].size); trpos += reg[k].size; }
    ntick = rd32();
    printf("%s: %u regions, %u ticks\n", argv[1], nreg, ntick);
    {                                                   /* the trailer, if any: skip the ticks */
        size_t p = trpos;
        u32 t, c;
        for (t = 0; t < ntick; t++) {
            u32 nch = tr[p + 8] | (tr[p + 9] << 8) | (tr[p + 10] << 16) | ((u32)tr[p + 11] << 24);
            p += 12;
            for (c = 0; c < nch; c++) {
                u32 n = tr[p + 4] | (tr[p + 5] << 8) | (tr[p + 6] << 16) | ((u32)tr[p + 7] << 24);
                p += 8 + n;
            }
        }
        if (p + 12 <= trlen && !memcmp(tr + p, "FIN1", 4)) {
            fin_vec = tr[p + 4] | (tr[p + 5] << 8) | (tr[p + 6] << 16) | ((u32)tr[p + 7] << 24);
            fin_n = tr[p + 8] | (tr[p + 9] << 8) | (tr[p + 10] << 16) | ((u32)tr[p + 11] << 24);
            fin_pos = p + 12;
            printf("trailer: %u change chunks up to the dispatch of %08x\n", fin_n, (unsigned)fin_vec);
        }
    }

    if (sat_init(argv[2])) { fprintf(stderr, "sat_init failed (data dir %s)\n", argv[2]); return 1; }
    sat_host_tick = NULL;
    sat_boot();
#ifdef PROF
    (void)pspfix_load(argv[2]);
    vdpr_noskip = getenv("VDPR_NOSKIP") != NULL;
    prof_use_hi = getenv("VDPR_HI") != NULL;
    vdpr_clock = clk_ns;
    { extern int vdpr_ref, vdpr_nocache; vdpr_ref = getenv("VDPR_REF") != NULL; vdpr_nocache = getenv("VDPR_NOCACHE") != NULL; }
#endif
    {                                                   /* the state the scenario set before the start */
        const u8 *p = patches;
        u32 i, a, n, j;
        for (i = 0; i < npatch; i++) {
            a = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); n = p[4] | (p[5] << 8) | (p[6] << 16) | ((u32)p[7] << 24);
            p += 8;
            if (a == 0xFFFFFF00u) sat_axis = p[0];      /* pseudo patch: the analog axis byte pad_read presents (tools/menu_drive.py AXIS) */
            else if (a == 0xFFFFFD00u && npokes < 64) {
                pokes[npokes].tick = ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                pokes[npokes].addr = ((u32)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
                pokes[npokes].n = n - 8;
                pokes[npokes].data = p + 8;
                npokes++;
            }
            else if (a == 0xFFFFFE00u && ndyn_ign < 16) { dyn_ign[ndyn_ign][0] = ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; dyn_ign[ndyn_ign][1] = ((u32)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]; ndyn_ign++; }
            else for (j = 0; j < n; j++) W8(a + j, p[j]);
            p += n;
        }
    }
    if (start_vector) W8(0x060AA801, 2);                /* Menu.start() sets the V-OUT byte before it enters a screen directly */
    if (compare("boot", 0)) { printf("BOOT DIFFERS\n"); return 1; }
    printf("boot identical\n");
    if (ntick == 0) return 0;
    pad_next = tr[trpos] | (tr[trpos + 1] << 8);
    sat_pad = pad_next;
    sat_host_tick = host_tick;
    menu_unported_hook = unported_hook;
    menu_call_hook = call_hook;
    rc = setjmp(done);
    if (!rc) sat_main_loop_from(start_vector);
#ifdef PROF
    if (prof_n) printf("prof: %u pictures  avg %.3f ms (pspfix %.3f + render %.3f)  max %.3f ms  over 0.25/0.5/1 ms: %.0f/%.0f/%.0f\n", prof_n, (prof_fix + prof_ren) / prof_n, prof_fix / prof_n, prof_ren / prof_n,
                       prof_max, prof_over[0], prof_over[1], prof_over[2]);
    if (getenv("PROF_WHY")) {                           /* why the pictures were composed again: counts per reason, then the VDP2 registers that changed */
        extern unsigned vdpr_prof_why[10], vdpr_prof_reg[];
        unsigned k;
        printf("prof: recomposed because: noskip %u first %u frame %u composite %u cram %u back %u sprites %u sprite-layer %u registers %u other %u\n", vdpr_prof_why[0], vdpr_prof_why[1], vdpr_prof_why[2], vdpr_prof_why[3], vdpr_prof_why[4], vdpr_prof_why[5], vdpr_prof_why[6], vdpr_prof_why[7], vdpr_prof_why[8], vdpr_prof_why[9]);
        for (k = 0; k < 0x120; k++) if (vdpr_prof_reg[k]) printf("prof:   v2reg %03x changed in %u pictures\n", k, vdpr_prof_reg[k]);
    }
    if (prof_n) {
        double p[VDPR_NPARTS];
        int k;
        for (k = 0; k < VDPR_NPARTS; k++) p[k] = (double)vdpr_t[k] / 1e6 / prof_n;
        printf("prof: parts avg (ms): VRAM scan %.3f  colour RAM %.3f  layers %.3f  composite %.3f  dots %.3f  VDP1 draw %.3f  VDP1 list %.3f;  VDP2 VRAM touched in %u of %u pictures, %u pictures unchanged\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], prof_touched, prof_n, prof_same);
    }
    if (prof_lticks) printf("prof: ticks' own code avg %.3f ms  max %.3f ms (tick %u, vector %08x)  over 0.1/0.25/0.5 ms: %.0f/%.0f/%.0f of %u\n", prof_lsum / prof_lticks, prof_lmax, prof_lmax_tick, prof_lvec, prof_lover[0], prof_lover[1], prof_lover[2], prof_lticks);
#endif
    if (failed) { printf("FAILED at tick %u\n", tick_i); return 1; }
    printf("%u ticks identical\n", tick_i);
    return 0;
}
