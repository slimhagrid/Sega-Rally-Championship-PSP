#include <stdio.h>
#include <string.h>
#include "menu_padcfg.h"

unsigned char padcfg_act[PC_TYPES][PC_NBTN];
const char *const padcfg_act_name[PC_NACT] = { "NOT USED", "BRAKE", "ACCEL", "SHIFT UP", "SHIFT DOWN", "CHANGE VIEW" };
const char *const padcfg_btn_name[PC_NBTN] = { "L", "D-PAD UP", "D-PAD DOWN", "TRIANGLE", "R", "CIRCLE", "CROSS", "SQUARE" };

/* the Saturn pad bits the PSP buttons stand for in the race (play.c sat_word): L 0x008, up 0x1000, down 0x2000, Triangle = X 0x040, R 0x080, Circle = C 0x200, Cross = B 0x100, Square = A 0x400 */
static const unsigned short sat_bits[PC_NBTN] = { 0x008, 0x1000, 0x2000, 0x040, 0x080, 0x200, 0x100, 0x400 };

unsigned padcfg_sat_mask(int button) { return sat_bits[button]; }

void padcfg_defaults(void)
{
    memset(padcfg_act, 0, sizeof padcfg_act);
    /* A: L or Square brake, R accelerate, Triangle shift up, Cross shift down, Circle change view */
    padcfg_act[0][PCB_L] = PC_BRAKE; padcfg_act[0][PCB_SQUARE] = PC_BRAKE; padcfg_act[0][PCB_R] = PC_ACCEL;
    padcfg_act[0][PCB_TRIANGLE] = PC_SHIFT_UP; padcfg_act[0][PCB_CROSS] = PC_SHIFT_DOWN; padcfg_act[0][PCB_CIRCLE] = PC_VIEW;
    /* B: L shift down, R shift up, Square or Circle brake, Cross accelerate, Triangle change view */
    padcfg_act[1][PCB_L] = PC_SHIFT_DOWN; padcfg_act[1][PCB_R] = PC_SHIFT_UP; padcfg_act[1][PCB_SQUARE] = PC_BRAKE; padcfg_act[1][PCB_CIRCLE] = PC_BRAKE;
    padcfg_act[1][PCB_CROSS] = PC_ACCEL; padcfg_act[1][PCB_TRIANGLE] = PC_VIEW;
    memcpy(padcfg_act[2], padcfg_act[0], PC_NBTN);              /* DIY starts as A */
}

static void __attribute__((constructor)) padcfg_init(void) { padcfg_defaults(); }

void padcfg_load(const char *base)
{
    char path[300], line[128];
    FILE *f;
    padcfg_defaults();
    snprintf(path, sizeof path, "%s/keycfg.txt", base);
    f = fopen(path, "r");
    if (!f) return;
    if (fgets(line, sizeof line, f) && strncmp(line, "DIY", 3) == 0) {         /* "DIY a b c d e f g h": the function of each button slot */
        int v[PC_NBTN], k;
        if (sscanf(line + 3, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == PC_NBTN) {
            int ok = 1;
            for (k = 0; k < PC_NBTN; k++) if (v[k] < 0 || v[k] >= PC_NACT) ok = 0;
            if (ok) for (k = 0; k < PC_NBTN; k++) padcfg_act[2][k] = (unsigned char)v[k];
            padcfg_act[2][PCB_UP] = padcfg_act[2][PCB_DOWN] = PC_NONE;      /* the D-pad up / down have no function */
        }
    }
    fclose(f);
}

void padcfg_save(const char *base)
{
    char path[300];
    FILE *f;
    int k;
    snprintf(path, sizeof path, "%s/keycfg.txt", base);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "DIY");
    for (k = 0; k < PC_NBTN; k++) fprintf(f, " %d", padcfg_act[2][k]);
    fprintf(f, "\n");
    fclose(f);
}

void padcfg_rows(int type, unsigned short row[7])
{
    int k;
    if (type < 0 || type >= PC_TYPES) type = 0;
    memset(row, 0, 7 * sizeof row[0]);
    for (k = 0; k < PC_NBTN; k++) {
        switch (padcfg_act[type][k]) {
        case PC_ACCEL: row[0] |= sat_bits[k]; break;
        case PC_BRAKE: row[1] |= sat_bits[k]; break;
        case PC_SHIFT_UP: row[2] |= sat_bits[k]; break;
        case PC_SHIFT_DOWN: row[3] |= sat_bits[k]; break;
        case PC_VIEW: row[4] |= sat_bits[k]; break;
        default: break;
        }
    }
}
