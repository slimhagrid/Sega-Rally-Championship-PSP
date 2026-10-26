/* M8: unit test of the save data layer (src/sat_save.c + src/menu_bup.c): save the records through the file-backed backup library, "power cycle" the console model, load them back.
 *
 *   test_save <datadir> <tmpdir>        (see tools/test_save.sh)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sat.h"
#include "../src/satlib.h"
#include "../src/sat_save.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void rm(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    remove(p);
}

static u32 hsum(u32 a, u32 n)
{
    u32 s = 0, i;
    for (i = 0; i < n; i++) s = s * 31u + R8(a + i);
    return s;
}

static void boot(const char *dd)
{
    if (sat_init(dd)) { printf("sat_init failed\n"); exit(1); }
    sat_host_tick = NULL;
    sat_boot();
}

int main(int argc, char **argv)
{
    const char *dd, *td;
    u32 rec_before, opt_before, set_before, nm_before, i;
    char path[512];
    FILE *f;
    if (argc < 3) { fprintf(stderr, "usage: test_save datadir tmpdir\n"); return 1; }
    dd = argv[1]; td = argv[2];
    rm(td, "BUP0.BIN");
    rm(td, "BUP1.BIN");

    /* 1. first boot: no save file yet: the factory records */
    sat_save_open(td);
    boot(dd);
    CHECK(R32(0x06041220) == 0x00032525, "factory record 0 = %08x", (unsigned)R32(0x06041220));
    CHECK(R8(0x06042A33) == 0, "save is off until the backup screen / the host turns it on");

    /* 2. change records and options, turn saving on (device 0) and save */
    W32(0x06041220, 0x00012345);
    W32(0x06041224, 0x12090A0Bu);                       /* flag + 3 letters */
    W32(0x06041230, 0x00022222);
    W32(0x06041234, 0x01010101u);
    W8(0x06040062, 0x16);
    for (i = 0; i < 0x22; i++) W8(0x06040063 + i, (u8)(i * 5 + 1));
    W8(0x06042A38, 3);
    W8(0x06042A32, 0);
    W8(0x06042A33, 1);
    rec_data_expand(0x06041220);                        /* normalise (the packed form is what is saved) */
    rec_data_unpack();
    rec_before = hsum(0x06041220, 0xC00);
    opt_before = hsum(0x06040062, 0x22);
    set_before = hsum(0x06041E20, 0x302 * 4);
    nm_before = hsum(0x06042A38, 14);
    bup_main_save();
    snprintf(path, sizeof(path), "%s/BUP0.BIN", td);
    f = fopen(path, "rb");
    CHECK(f != NULL, "the image file was written");
    if (f) fclose(f);

    /* 3. power cycle: everything in the console model is gone, the file stays */
    boot(dd);
    CHECK(R32(0x06041220) != 0x00032525, "the saved record came back");
    CHECK(hsum(0x06041220, 0xC00) == rec_before, "records identical after the reload");
    CHECK(hsum(0x06040062, 0x22) == opt_before, "options identical after the reload");
    CHECK(hsum(0x06041E20, 0x302 * 4) == set_before, "settings identical after the reload");
    CHECK(hsum(0x06042A38, 14) == nm_before, "name block identical after the reload");

    /* 4. a damaged image is refused (checksum): the factory records again */
    f = fopen(path, "r+b");
    if (f) { fseek(f, 16 + 34 + 100, SEEK_SET); fputc(0x5A ^ fgetc(f), f); fclose(f); }
    sat_save_open(td);
    boot(dd);
    CHECK(R32(0x06041220) == 0x00032525, "a damaged save falls back to the factory records");

    /* 5. no backup memory at all: the factory records, saving does nothing */
    sat_save_open(NULL);
    boot(dd);
    CHECK(R32(0x06041220) == 0x00032525, "no backup memory: factory records");
    W8(0x06042A33, 1);
    bup_main_save();                                    /* must not crash */

    /* 6. the library: files, capacity, directory, verify, delete, format, the memory cartridge (replays) */
    {
        BupDir d, tb[8];
        BupStat st;
        BupDate bd;
        u32 found = 0, code;
        sat_save_cart(1);
        sat_save_open(td);
        rm(td, "BUP0.BIN"); rm(td, "BUP1.BIN");
        sat_save_open(td);
        sat_init(dd);
        for (i = 0; i < 0xF400; i++) W8(0x002C0000 + i, (u8)(i * 13 + (i >> 8)));
        CHECK(bios_bup_stat(1, 0xF400, &st) == BUP_OK && st.totalsize == 1048576u && st.datanum == 0, "cartridge stat");
        memset(&d, 0, sizeof(d));
        strcpy(d.filename, "SEGARALLY_1");
        strcpy(d.comment, "GHOSTS    ");
        d.datasize = 0xF400;
        bd.year = 15; bd.month = 9; bd.day = 17; bd.time = 14; bd.min = 33; bd.week = 0;
        d.date = bios_bup_setdate(&bd);
        CHECK(bios_bup_write(1, &d, 0x002C0000, 0) == BUP_OK, "replay write");
        CHECK(bios_bup_write(0, &d, 0x002C0000, 0) == BUP_NOT_ENOUGH_MEMORY, "32 KB internal memory cannot hold 62 KB");
        CHECK(bios_bup_write(1, &d, 0x002C0000, 1) == BUP_FOUND, "no-overwrite mode refuses an existing file");
        CHECK(bios_bup_write(2, &d, 0x002C0000, 0) == BUP_NON, "no such device");
        for (i = 0; i < 0xF400; i++) W8(0x002D0000 + i, 0);
        CHECK(bios_bup_read(1, "SEGARALLY_1", 0x002D0000) == BUP_OK, "replay read");
        CHECK(memcmp(sat_ptr(0x002C0000), sat_ptr(0x002D0000), 0xF400) == 0, "replay data identical");
        CHECK(bios_bup_verify(1, "SEGARALLY_1", 0x002C0000) == BUP_OK, "verify equal");
        W8(0x002C0010, R8(0x002C0010) ^ 1);
        CHECK(bios_bup_verify(1, "SEGARALLY_1", 0x002C0000) == BUP_NO_MATCH, "verify differs");
        CHECK(bios_bup_dir(1, "", 8, tb, &found) == BUP_OK && found == 1 && !strcmp(tb[0].filename, "SEGARALLY_1") && tb[0].datasize == 0xF400, "dir");
        bios_bup_getdate(tb[0].date, &bd);
        CHECK(bd.year == 15 && bd.month == 9 && bd.day == 17 && bd.time == 14 && bd.min == 33, "date round trip %d-%d-%d %d:%d", bd.year, bd.month, bd.day, bd.time, bd.min);
        code = bios_bup_setdate(&bd);
        CHECK(code == d.date, "setdate / getdate");
        /* the cartridge image survives a reopen */
        sat_save_open(td);
        CHECK(bios_bup_read(1, "SEGARALLY_1", 0x002D0000) == BUP_OK, "replay read after reopen");
        CHECK(bios_bup_delete(1, "SEGARALLY_1") == BUP_OK && bios_bup_read(1, "SEGARALLY_1", 0x002D0000) == BUP_NOT_FOUND, "delete");
        CHECK(bios_bup_format(1) == BUP_OK, "format");
        sat_save_cart(0);
        sat_save_open(td);
        CHECK(bios_bup_stat(1, 0, &st) == BUP_NON, "cartridge removed");
    }
    /* 7. the replay checksums (0x0602F5E8): an unused slot (first word 0) is skipped, a used slot needs 9 + the sum of its longs */
    {
        u32 slot, k, sum;
        sat_init(dd);
        CHECK(bup_check_replays() == 0, "empty replays are fine");
        for (k = 0; k < 4; k += 2) {
            slot = 0x002C0000 + k * 0x3D00;
            sum = 9;
            for (i = 0; i < (0x3D00 - 4) / 4; i++) { W32(slot + 4 + 4 * i, 0x01010101u * (i + k)); sum += 0x01010101u * (i + k); }
            W32(slot, sum);
        }
        CHECK(bup_check_replays() == 0, "valid replays");
        W8(0x002C0000 + 2 * 0x3D00 + 100, R8(0x002C0000 + 2 * 0x3D00 + 100) ^ 4);
        CHECK(bup_check_replays() == 99, "a damaged replay is found");
        CHECK(bup_check_main() == 99, "no image: the checksum fails");
    }
    printf(fails ? "%d FAILED\n" : "all save tests passed\n", fails);
    return fails ? 1 : 0;
}
