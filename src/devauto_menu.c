/* Development: data/autoplay.txt = a script for unattended runs in PPSSPP.  One command per line (# = comment), numbers are decimal unless they are marked hex:
 *   wait N                     N V-blank ticks (two ticks make a picture)
 *   tap HEXMASK [HOLD] [GAP]   Saturn pad bits held HOLD ticks (4), then released GAP ticks (6)   (X = 0200 confirm, O = 0100 back, SQUARE = 0400, TRIANGLE = 0040, START = 0800, UP 1000, DOWN 2000, LEFT 4000, RIGHT 8000)
 *   hold HEXMASK N             the bits held N ticks
 *   until W HEXADDR HEXVAL [T] wait until the W-bit (8 / 16 / 32) memory value equals HEXVAL; after T ticks (1800) go on and say TIMEOUT in the log (the main vector is 32 060A001C)
 *   shot NAME                  the picture now -> data/shots/NNNN_NAME.png (the runner captures the window while the game waits)
 *   poke W HEXADDR HEXVAL      write the console memory (a set-up for a test)
 *   por W HEXADDR HEXVAL       the same, ORed into the memory (unlock bits: 06040062 bit 1 = the lake, bit 2 = the Stratos)
 *   pilot MODE [VMAX [CURVEK [STEERK [LOOK]]]] the races from now on: drive (the guide line pilot), idle (no input), pad (the real pad); VMAX in metres per pass (2.1), CURVEK = how much corners slow it (1.0)
 *   raceshots N                a screenshot every N passes of a race (0 = none)
 *   vecshots DELAY             a screenshot DELAY ticks after every change of the main vector (0 = off), named by the vector
 *   autoshots EVERY            a screenshot every EVERY ticks of the menus (0 = off)
 *   cheat time 0|1             the time limit of a race never runs below 20 s (a slow pilot gets through the stage)
 *   cheat rank N               the goal of a race is reached as rank N (1 = the winner; 0 = the real rank)
 *   turbo 0|1                  1: the races do not wait for the V-blank (fast; the sound is not in time)
 *   realpad 0|1                1: the script's pad is ignored from here on (the real pad plays; the test runner sends keys)
 *   log TEXT                   a line in data/autoplay.log
 *   end                        the log says END and the script stops
 * Every command is logged with the tick and the main vector. */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat.h"
#include "devauto.h"
#include "devauto_menu.h"

enum { C_WAIT, C_TAP, C_HOLD, C_UNTIL, C_SHOT, C_POKE, C_PILOT, C_RSHOTS, C_LOG, C_END, C_VECSHOTS, C_AUTOSHOTS, C_REALPAD, C_TURBO, C_CHEAT, C_POR };

typedef struct { int op; unsigned a, b, c, d; float f, g, h, k; char s[48]; } Cmd;

int devauto_realpad;
static Cmd cmds[1024];
static int ncmd, pc;
static char log_path[256];
static const char *base_dir;
static int busy, started, finished, waited;
static unsigned cur_mask, vec_delay, vec_last, vec_due, auto_every, auto_next;

static void say(unsigned tick, const char *what, const char *arg)
{
    FILE *f = fopen(log_path, "a");
    if (f) { fprintf(f, "%6u  vec %08x  %s %s\n", tick, (unsigned)R32(0x060A001C), what, arg); fclose(f); }
}

int devauto_menu_load(const char *base)
{
    char path[256], line[160];
    FILE *f;
    base_dir = base;
    snprintf(path, sizeof path, "%s/autoplay.txt", base);
    snprintf(log_path, sizeof log_path, "%s/autoplay.log", base);
    f = fopen(path, "r");
    if (!f) return 0;
    while (ncmd < 1024 && fgets(line, sizeof line, f)) {
        Cmd *c = &cmds[ncmd];
        char w[16] = "", t[48] = "";
        unsigned a = 0, b = 0, d = 0, e = 0;
        int n;
        memset(c, 0, sizeof *c);
        if (line[0] == '#' || sscanf(line, "%15s", w) != 1) continue;
        if (!strcmp(w, "wait") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_WAIT; c->a = a; }
        else if (!strcmp(w, "tap") && (n = sscanf(line, "%*s %x %u %u", &a, &b, &d)) >= 1) { c->op = C_TAP; c->a = a; c->b = n >= 2 ? b : 4; c->c = n >= 3 ? d : 6; }
        else if (!strcmp(w, "hold") && sscanf(line, "%*s %x %u", &a, &b) == 2) { c->op = C_HOLD; c->a = a; c->b = b; }
        else if (!strcmp(w, "until") && (n = sscanf(line, "%*s %u %x %x %u", &a, &b, &d, &e)) >= 3) { c->op = C_UNTIL; c->a = a; c->b = b; c->c = d; c->d = n >= 4 ? e : 1800; }
        else if (!strcmp(w, "shot") && sscanf(line, "%*s %47s", t) == 1) { c->op = C_SHOT; strcpy(c->s, t); }
        else if (!strcmp(w, "poke") && sscanf(line, "%*s %u %x %x", &a, &b, &d) == 3) { c->op = C_POKE; c->a = a; c->b = b; c->c = d; }
        else if (!strcmp(w, "por") && sscanf(line, "%*s %u %x %x", &a, &b, &d) == 3) { c->op = C_POR; c->a = a; c->b = b; c->c = d; }
        else if (!strcmp(w, "pilot") && sscanf(line, "%*s %47s", t) == 1) { c->op = C_PILOT; strcpy(c->s, t); c->f = 0; c->g = 0; c->h = 0; c->k = 0; sscanf(line, "%*s %*s %f %f %f %f", &c->f, &c->g, &c->h, &c->k); }
        else if (!strcmp(w, "raceshots") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_RSHOTS; c->a = a; }
        else if (!strcmp(w, "vecshots") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_VECSHOTS; c->a = a; }
        else if (!strcmp(w, "autoshots") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_AUTOSHOTS; c->a = a; }
        else if (!strcmp(w, "realpad") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_REALPAD; c->a = a; }
        else if (!strcmp(w, "turbo") && sscanf(line, "%*s %u", &a) == 1) { c->op = C_TURBO; c->a = a; }
        else if (!strcmp(w, "cheat") && sscanf(line, "%*s %47s %u", t, &a) >= 1) { c->op = C_CHEAT; strcpy(c->s, t); c->a = a; }
        else if (!strcmp(w, "log")) { c->op = C_LOG; snprintf(c->s, sizeof c->s, "%s", line + 3 + strspn(line + 3, " ")); c->s[strcspn(c->s, "\r\n")] = 0; }
        else if (!strcmp(w, "end")) c->op = C_END;
        else continue;
        ncmd++;
    }
    fclose(f);
    f = fopen(log_path, "w");
    if (f) fclose(f);
    return ncmd > 0;
}

static u32 rd(unsigned w, u32 a) { return w == 8 ? R8(a) : w == 16 ? R16(a) : R32(a); }

static void bg_shots(unsigned tick)
{
    char nm[32];
    if (vec_delay) {
        unsigned v = R32(0x060A001C);
        if (v != vec_last) { vec_last = v; vec_due = tick + vec_delay; }
        if (vec_due && tick >= vec_due) { vec_due = 0; snprintf(nm, sizeof nm, "v%08x", (unsigned)R32(0x060A001C)); devauto_shot(base_dir, nm); }
    }
    if (auto_every && tick >= auto_next) { auto_next = tick + auto_every; snprintf(nm, sizeof nm, "t%u", tick); devauto_shot(base_dir, nm); }
}

unsigned devauto_menu_pad(unsigned tick)
{
    char buf[64];
    bg_shots(tick);
    while (pc < ncmd && !finished) {
        Cmd *c = &cmds[pc];
        if (!started) {                                 /* the command's first tick */
            started = 1; waited = 0;
            switch (c->op) {
            case C_WAIT: busy = (int)c->a; break;
            case C_TAP: busy = (int)(c->b + c->c); snprintf(buf, sizeof buf, "%04x", c->a); say(tick, "tap", buf); break;
            case C_HOLD: busy = (int)c->b; snprintf(buf, sizeof buf, "%04x %u", c->a, c->b); say(tick, "hold", buf); break;
            case C_UNTIL: snprintf(buf, sizeof buf, "%u %08x == %x", c->a, c->b, c->c); say(tick, "until", buf); break;
            default: break;
            }
        }
        switch (c->op) {
        case C_WAIT:
            if (busy-- > 0) { cur_mask = 0; return 0; }
            break;
        case C_TAP:
            if (busy-- > 0) { cur_mask = (unsigned)busy >= c->c ? c->a : 0; return cur_mask; }
            break;
        case C_HOLD:
            if (busy-- > 0) { cur_mask = c->a; return cur_mask; }
            break;
        case C_UNTIL:
            if (rd(c->a, c->b) == c->c) { say(tick, "reached", ""); break; }
            if (++waited > (int)c->d) { say(tick, "TIMEOUT", ""); devauto_shot(base_dir, "timeout"); break; }
            return 0;
        case C_SHOT: devauto_shot(base_dir, c->s); say(tick, "shot", c->s); break;
        case C_POKE:
            if (c->a == 8) W8(c->b, c->c); else if (c->a == 16) W16(c->b, c->c); else W32(c->b, c->c);
            say(tick, "poke", ""); break;
        case C_PILOT: {
            devauto_pilot_mode = !strcmp(c->s, "drive") ? 1 : !strcmp(c->s, "idle") ? 2 : 0;
            if (c->f > 0) devauto_vmax = c->f;
            if (c->g > 0) devauto_curve_k = c->g;
            if (c->h > 0) devauto_steer_k = c->h;
            if (c->k > 0) devauto_look = c->k;
            snprintf(buf, sizeof buf, "%s %.2f", c->s, devauto_vmax); say(tick, "pilot", buf);
            break;
        }
        case C_RSHOTS: devauto_race_shots = (int)c->a; say(tick, "raceshots", ""); break;
        case C_VECSHOTS: vec_delay = c->a; vec_last = R32(0x060A001C); break;
        case C_AUTOSHOTS: auto_every = c->a; auto_next = tick + c->a; break;
        case C_POR:
            if (c->a == 8) W8(c->b, R8(c->b) | c->c); else if (c->a == 16) W16(c->b, R16(c->b) | c->c); else W32(c->b, R32(c->b) | c->c);
            say(tick, "por", ""); break;
        case C_CHEAT:
            if (!strcmp(c->s, "time")) devauto_cheat_time = (int)c->a; else if (!strcmp(c->s, "rank")) devauto_cheat_rank = (int)c->a;
            say(tick, "cheat", c->s); break;
        case C_TURBO: devauto_turbo = (int)c->a; break;
        case C_REALPAD: devauto_realpad = (int)c->a; break;
        case C_LOG: say(tick, "log", c->s); break;
        case C_END: say(tick, "END", ""); finished = 1; return 0;
        }
        pc++; started = 0;
    }
    if (pc >= ncmd && !finished) { finished = 1; say(tick, "END", "(script ran out)"); }
    return 0;
}
