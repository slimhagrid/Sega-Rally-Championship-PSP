/* M8: the BIOS backup library (BUP) on host files: see sat_save.h.  The original's BIOS routines are not ported (they are the console's firmware): this is a model of what the
 * game's wrappers (menu_bup.c) need from them, with the library's return codes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat_save.h"

#define BLK 64u
enum { M_STUB, M_NONE, M_FILES };

typedef struct { BupDir dir; u8 *data; } BFile;
typedef struct { int present, formatted; u32 total; BFile *files; int nfiles, cap; } BDev;

static int mode = M_STUB;
static char root[240];
static int cart_on;
static BDev dev[2] = { { 1, 1, 32768u, NULL, 0, 0 }, { 0, 1, 1048576u, NULL, 0, 0 } };

void sat_save_stub(void) { mode = M_STUB; }

static void clear_dev(BDev *d)
{
    int i;
    for (i = 0; i < d->nfiles; i++) free(d->files[i].data);
    free(d->files);
    d->files = NULL;
    d->nfiles = d->cap = 0;
}

static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static void dir_pack(const BupDir *d, u8 *p)                    /* the 34-byte entry as the console stores it */
{
    memcpy(p, d->filename, 12);
    memcpy(p + 12, d->comment, 11);
    p[23] = d->language;
    put32(p + 24, d->date);
    put32(p + 28, d->datasize);
    p[32] = (u8)(d->blocksize >> 8);
    p[33] = (u8)d->blocksize;
}

static void dir_unpack(BupDir *d, const u8 *p)
{
    memcpy(d->filename, p, 12);
    memcpy(d->comment, p + 12, 11);
    d->language = p[23];
    d->date = get32(p + 24);
    d->datasize = get32(p + 28);
    d->blocksize = (u16)((p[32] << 8) | p[33]);
}

static void img_path(int n, char *out, size_t len) { snprintf(out, len, "%s/BUP%d.BIN", root, n); }

static void dev_save(int n)                                     /* BUPn.BIN: "SBUP", version, formatted, nfiles, then entry + data per file */
{
    char path[300], tmp[310];
    FILE *f;
    int i;
    u8 h[16], e[34];
    if (mode != M_FILES) return;
    img_path(n, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return;
    memcpy(h, "SBUP", 4);
    put32(h + 4, 1);
    put32(h + 8, (u32)dev[n].formatted);
    put32(h + 12, (u32)dev[n].nfiles);
    fwrite(h, 1, 16, f);
    for (i = 0; i < dev[n].nfiles; i++) {
        dir_pack(&dev[n].files[i].dir, e);
        fwrite(e, 1, 34, f);
        fwrite(dev[n].files[i].data, 1, dev[n].files[i].dir.datasize, f);
    }
    fclose(f);
    remove(path);
    rename(tmp, path);
}

static int dev_load(int n)                                      /* 0 = loaded, -1 = no image / damaged (the device is then unformatted) */
{
    char path[300];
    FILE *f;
    u8 h[16], e[34];
    u32 nf, i;
    img_path(n, path, sizeof(path));
    f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "SBUP", 4) || get32(h + 4) != 1) { fclose(f); return -1; }
    dev[n].formatted = (int)get32(h + 8);
    nf = get32(h + 12);
    clear_dev(&dev[n]);
    for (i = 0; i < nf; i++) {
        BFile bf;
        if (fread(e, 1, 34, f) != 34) break;
        dir_unpack(&bf.dir, e);
        if (bf.dir.datasize > dev[n].total) break;
        bf.data = (u8 *)malloc(bf.dir.datasize ? bf.dir.datasize : 1);
        if (fread(bf.data, 1, bf.dir.datasize, f) != bf.dir.datasize) { free(bf.data); break; }
        if (dev[n].nfiles == dev[n].cap) {
            dev[n].cap = dev[n].cap ? dev[n].cap * 2 : 4;
            dev[n].files = (BFile *)realloc(dev[n].files, (size_t)dev[n].cap * sizeof(BFile));
        }
        dev[n].files[dev[n].nfiles++] = bf;
    }
    fclose(f);
    return 0;
}

int sat_save_open(const char *dir)
{
    int n;
    for (n = 0; n < 2; n++) clear_dev(&dev[n]);
    if (!dir) { mode = M_NONE; return 0; }
    mode = M_FILES;
    snprintf(root, sizeof(root), "%s", dir);
    dev[0].present = 1;
    dev[1].present = cart_on;
    for (n = 0; n < 2; n++) {
        dev[n].formatted = 1;
        if (dev[n].present && dev_load(n) != 0) {               /* no image yet: an empty, formatted memory (and a damaged image is replaced at the first write) */
            dev[n].formatted = 1;
            clear_dev(&dev[n]);
        }
    }
    return 0;
}

void sat_save_cart(int present) { cart_on = present; dev[1].present = present; }
int sat_save_has_cart(void) { return mode != M_NONE && dev[1].present; }

/* ---- the library ---- */
static u32 blocks_of(u32 datasize) { return 1 + (datasize + BLK - 1) / BLK; }              /* one directory block + the data */

static u32 used_blocks(const BDev *d)
{
    u32 s = 0;
    int i;
    for (i = 0; i < d->nfiles; i++) s += blocks_of(d->files[i].dir.datasize);
    return s;
}

static int find(const BDev *d, const char *name)
{
    int i;
    for (i = 0; i < d->nfiles; i++)
        if (!strncmp(d->files[i].dir.filename, name, 11)) return i;
    return -1;
}

static u32 dev_check(u32 device)                                /* BUP_NON / BUP_UNFORMAT / OK */
{
    if (mode == M_NONE) return BUP_NON;
    if (device > 1 || !dev[device].present) return BUP_NON;
    if (!dev[device].formatted) return BUP_UNFORMAT;
    return BUP_OK;
}

u32 bios_bup_init(u32 lib_addr, u32 work_addr) { (void)lib_addr; (void)work_addr; return BUP_OK; }

u32 bios_bup_format(u32 device)
{
    u32 r;
    if (mode == M_STUB) return BUP_OK;
    if (device > 1 || mode == M_NONE || !dev[device].present) return BUP_NON;
    r = BUP_OK;
    clear_dev(&dev[device]);
    dev[device].formatted = 1;
    dev_save((int)device);
    return r;
}

u32 bios_bup_stat(u32 device, u32 datasize, BupStat *st)
{
    u32 r, used, nb;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    used = used_blocks(&dev[device]);
    nb = dev[device].total / BLK;
    st->totalsize = dev[device].total;
    st->totalblock = nb;
    st->blocksize = BLK;
    st->freeblock = nb > used ? nb - used : 0;
    st->freesize = st->freeblock * BLK;
    st->datanum = (u32)dev[device].nfiles;
    (void)datasize;
    return BUP_OK;
}

u32 bios_bup_write(u32 device, const BupDir *dir, u32 data_addr, u32 wmode)
{
    BDev *d;
    u32 r, i, need, have;
    int at;
    BFile bf;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    d = &dev[device];
    at = find(d, dir->filename);
    if (at >= 0 && wmode != 0) return BUP_FOUND;
    need = blocks_of(dir->datasize);
    have = d->total / BLK - used_blocks(d) + (at >= 0 ? blocks_of(d->files[at].dir.datasize) : 0);
    if (need > have) return BUP_NOT_ENOUGH_MEMORY;
    bf.dir = *dir;
    bf.dir.blocksize = (u16)need;
    bf.data = (u8 *)malloc(dir->datasize ? dir->datasize : 1);
    for (i = 0; i < dir->datasize; i++) bf.data[i] = R8(data_addr + i);
    if (at >= 0) {
        free(d->files[at].data);
        d->files[at] = bf;
    } else {
        if (d->nfiles == d->cap) {
            d->cap = d->cap ? d->cap * 2 : 4;
            d->files = (BFile *)realloc(d->files, (size_t)d->cap * sizeof(BFile));
        }
        d->files[d->nfiles++] = bf;
    }
    dev_save((int)device);
    return BUP_OK;
}

u32 bios_bup_read(u32 device, const char *filename, u32 data_addr)
{
    u32 r, i;
    int at;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    at = find(&dev[device], filename);
    if (at < 0) return BUP_NOT_FOUND;
    for (i = 0; i < dev[device].files[at].dir.datasize; i++) W8(data_addr + i, dev[device].files[at].data[i]);
    return BUP_OK;
}

u32 bios_bup_delete(u32 device, const char *filename)
{
    u32 r;
    int at;
    BDev *d;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    d = &dev[device];
    at = find(d, filename);
    if (at < 0) return BUP_NOT_FOUND;
    free(d->files[at].data);
    memmove(&d->files[at], &d->files[at + 1], (size_t)(d->nfiles - at - 1) * sizeof(BFile));
    d->nfiles--;
    dev_save((int)device);
    return BUP_OK;
}

u32 bios_bup_dir(u32 device, const char *filename, u32 tbsize, BupDir *tb, u32 *found)
{
    u32 r, n = 0;
    int i;
    if (found) *found = 0;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    for (i = 0; i < dev[device].nfiles && n < tbsize; i++) {
        if (filename && filename[0] && strncmp(dev[device].files[i].dir.filename, filename, 11)) continue;
        tb[n++] = dev[device].files[i].dir;
    }
    if (found) *found = n;
    return BUP_OK;
}

u32 bios_bup_verify(u32 device, const char *filename, u32 data_addr)
{
    u32 r, i;
    int at;
    if (mode == M_STUB) return BUP_OK;
    r = dev_check(device);
    if (r != BUP_OK) return r;
    at = find(&dev[device], filename);
    if (at < 0) return BUP_NOT_FOUND;
    for (i = 0; i < dev[device].files[at].dir.datasize; i++)
        if (R8(data_addr + i) != dev[device].files[at].data[i]) return BUP_NO_MATCH;
    return BUP_OK;
}

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

u32 bios_bup_setdate(const BupDate *d)                          /* minutes since 1980-01-01 00:00 */
{
    u32 days = 0, y, m;
    for (y = 1980; y < 1980u + d->year; y++) days += leap((int)y) ? 366u : 365u;
    for (m = 1; m < d->month && m <= 12; m++) days += (u32)mdays[m - 1] + (m == 2 && leap((int)(1980 + d->year)) ? 1u : 0u);
    days += d->day ? d->day - 1u : 0u;
    return (days * 24u + d->time) * 60u + d->min;
}

void bios_bup_getdate(u32 code, BupDate *d)
{
    u32 days = code / 1440u, rest = code % 1440u, y = 1980, m = 0;
    d->week = (u8)((days + 2) % 7);                             /* 1980-01-01 was a Tuesday (0 = Sunday) */
    for (;;) {
        u32 yl = leap((int)y) ? 366u : 365u;
        if (days < yl) break;
        days -= yl;
        y++;
    }
    for (m = 0; m < 12; m++) {
        u32 ml = (u32)mdays[m] + (m == 1 && leap((int)y) ? 1u : 0u);
        if (days < ml) break;
        days -= ml;
    }
    d->year = (u8)(y - 1980);
    d->month = (u8)(m + 1);
    d->day = (u8)(days + 1);
    d->time = (u8)(rest / 60u);
    d->min = (u8)(rest % 60u);
}
