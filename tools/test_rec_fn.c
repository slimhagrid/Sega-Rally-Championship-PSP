/* M8: checks the record helpers of the port against function-level vectors of the original (tools/fn_records.py -> extracted/oracle/fn_records.txt).
 *
 *   test_rec_fn <vectors.txt> <datadir>
 *
 * Every case: the memory patches, the call with the registers of the original, the registers and the memory delta must match.  The compared regions are those of the menu trace
 * (tools/menu_oracle.py REGIONS).  The cases run one after the other on the same memory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sat.h"
#include "../src/satlib.h"
#include "../src/menu_records.h"
#include "../src/sat_save.h"

typedef struct { u32 addr, size; } Reg;
static const Reg REG[] = {
    { 0x0603D880, 0x060FE000 - 0x0603D880 }, { 0x00200000, 0x00100000 }, { 0x25C00000, 0x00080000 }, { 0x25E00000, 0x00080000 }, { 0x25F00000, 0x1000 }, { 0x25F80000, 0x120 },
};
#define NREG 6
static u8 *shot[NREG];

static void take(void)
{
    int k;
    for (k = 0; k < NREG; k++) memcpy(shot[k], sat_ptr(REG[k].addr), REG[k].size);
}

static const RecPutNum PUTS[8] = { 0 };
static u32 (*putfn(u32 addr))(u32, u32)
{
    switch (addr) {
    case 0x0602CB48: return putnum_1x2;
    case 0x0602CB62: return putnum_rec;
    case 0x0602CBA4: return putnum_name;
    case 0x0602CBBA: return putnum_name2;
    case 0x0602CBD8: return putnum_rank;
    case 0x0602CC0A: return putnum_rank2;
    case 0x0602CC44: return putnum_Grn;
    case 0x0602CC5C: return putnum_Grn2;
    }
    return NULL;
}

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

int main(int argc, char **argv)
{
    FILE *f;
    char line[1 << 17];
    int k, ncase = 0, nfail = 0;
    u32 fn = 0, r2 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r11 = 0, r14 = 0;
    u32 o0 = 0, o4 = 0, g0 = 0, g4 = 0, g9 = 0, o9 = 0;
    int have_regs = 0;
    static u32 dlist[4096][2];
    static u8 *dbuf[4096];
    int nd = 0, i;
    (void)PUTS;
    if (argc < 3) { fprintf(stderr, "usage: test_rec_fn vectors.txt datadir\n"); return 1; }
    f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    if (sat_init(argv[2])) { fprintf(stderr, "sat_init failed\n"); return 1; }
    sat_host_tick = NULL;
    sat_boot();
    for (k = 0; k < NREG; k++) shot[k] = (u8 *)malloc(REG[k].size);
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'C') {
            unsigned a, b, c, d, e, g, h, j, l;
            sscanf(line + 2, "%x %u %u %u %u %u %u %u %u", &a, &b, &c, &d, &e, &g, &h, &j, &l);
            fn = a; r2 = b; r4 = c; r5 = d; r6 = e; r7 = g; r8 = h; r11 = j; r14 = l;
            have_regs = 1;
            for (i = 0; i < nd; i++) free(dbuf[i]);
            nd = 0;
            take();
        } else if (line[0] == 'P') {
            unsigned a;
            char *hex = strchr(line + 2, ' ');
            u32 n, j;
            sscanf(line + 2, "%x", &a);
            hex++;
            n = (u32)strlen(hex);
            while (n && (hex[n - 1] == '\n' || hex[n - 1] == '\r')) n--;
            for (j = 0; j < n / 2; j++) W8(a + j, (u32)(hexval(hex[2 * j]) << 4 | hexval(hex[2 * j + 1])));
            take();                                         /* the patches are part of the state before the call */
        } else if (line[0] == 'O') {
            unsigned a, b, c;
            sscanf(line + 2, "%u %u %u", &a, &b, &c);
            o0 = a; o4 = b; o9 = c;
        } else if (line[0] == 'D') {
            unsigned a;
            char *hex = strchr(line + 2, ' ');
            u32 n, j;
            sscanf(line + 2, "%x", &a);
            hex++;
            n = (u32)strlen(hex);
            while (n && (hex[n - 1] == '\n' || hex[n - 1] == '\r')) n--;
            dlist[nd][0] = a; dlist[nd][1] = n / 2;
            dbuf[nd] = (u8 *)malloc(n / 2);
            for (j = 0; j < n / 2; j++) dbuf[nd][j] = (u8)(hexval(hex[2 * j]) << 4 | hexval(hex[2 * j + 1]));
            nd++;
        } else if (line[0] == 'E' && have_regs) {
            int bad = 0;
            g0 = g4 = g9 = 0;
            rec_disp_ctx((s32)r11, (s32)r14);
            switch (fn) {
            case 0x0602CB48: case 0x0602CB62: case 0x0602CBA4: case 0x0602CBBA: case 0x0602CBD8: case 0x0602CC0A: case 0x0602CC44: case 0x0602CC5C:
                g4 = putfn(fn)(r4, r2);
                break;
            case 0x0602CAF8:
                time_disp(r4, r5, (RecPutNum)putfn(r6));
                break;
            case 0x0602EE68: record_name_disp(r4, r5); break;
            case 0x0602E8EA: g9 = record_insert(r4, r5, r6, (s32)r8); break;
            case 0x0602D330: rec_pal_set(r8, r11); break;
            case 0x0602D358: rec_color_set(r8); break;
            case 0x0602D370: rec_vdp2_set_w(r8); break;
            case 0x0602EF2E: rec_tile16(r4, r5, r6, r7); break;
            case 0x0602EF4A: rec_tile32(r4, r5, r6, r7); break;
            case 0x0602F5BC: g0 = bup_check_main(); break;
            case 0x0602F5E8: g0 = bup_check_replays(); break;
            default: printf("unknown function %08x\n", (unsigned)fn); return 1;
            }
            /* registers: only the ones the function returns */
            if (fn >= 0x0602CB48 && fn <= 0x0602CC5C && fn != 0x0602CAF8 && g4 != o4) { printf("case %d fn %08x: r4 expected %08x got %08x\n", ncase, (unsigned)fn, (unsigned)o4, (unsigned)g4); bad = 1; }
            if (fn == 0x0602E8EA && g9 != o9) { printf("case %d insert: r9 expected %u got %u\n", ncase, (unsigned)o9, (unsigned)g9); bad = 1; }
            if ((fn == 0x0602F5BC || fn == 0x0602F5E8) && g0 != o0) { printf("case %d check: r0 expected %u got %u\n", ncase, (unsigned)o0, (unsigned)g0); bad = 1; }
            /* memory: the delta of the original must be what changed, and nothing else */
            {
                int r;
                u32 off, n;
                for (r = 0; r < NREG; r++) {
                    u8 *now = sat_ptr(REG[r].addr);
                    u8 *exp = (u8 *)malloc(REG[r].size);
                    memcpy(exp, shot[r], REG[r].size);
                    for (i = 0; i < nd; i++) {
                        u32 a = dlist[i][0];
                        if (a >= REG[r].addr && a < REG[r].addr + REG[r].size) memcpy(exp + (a - REG[r].addr), dbuf[i], dlist[i][1]);
                    }
                    for (off = 0; off < REG[r].size; off++)
                        if (exp[off] != now[off]) {
                            if (!bad || 1) {
                                n = 0;
                                printf("case %d fn %08x: %08x expected %02x got %02x\n", ncase, (unsigned)fn, (unsigned)(REG[r].addr + off), exp[off], now[off]);
                                (void)n;
                            }
                            bad = 1;
                            break;
                        }
                    free(exp);
                }
            }
            if (bad) {
                nfail++;
                if (nfail > 10) break;
            }
            ncase++;
            have_regs = 0;
        }
    }
    printf("%d cases, %d failed\n", ncase, nfail);
    return nfail ? 1 : 0;
}
