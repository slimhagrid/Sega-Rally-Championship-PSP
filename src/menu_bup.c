/* M8: the save data code of the game: bup_file (0x0602F800 ..) / bup_freeze / bup_lib (0x06034C04 ..) ported from the original, on the BIOS backup library model of sat_save.c.
 *
 * What the original does (docs/menus.md "Save data"): the records and settings live at 0x06040000 .. 0x06042A60; "packing" (0x0602F404) turns the 384 records (8 bytes each at 0x06041220) into
 * 5 bytes each in the image at 0x060EEC00, copies the 0x302 longs at 0x06041E20 (the settings, 0x060EF380), 0x22 bytes of the option block (0x06040062 -> 0x060EFF88) and 14 bytes at
 * 0x06042A38 (-> 0x060EFFAA) behind them, and puts 9 + the sum of the 0x4EF longs of the image into 0x060EFFBC.  The image (0x13C0 bytes) is the backup file SEGARALLY_0.
 * The stored replays (4 x 0x3D00 bytes at 0x002C0000, the time attack runs the records can replay) are the backup file SEGARALLY_1 (0xF400 bytes), kept on the memory cartridge only.
 * 0x06042A30 .. 0x06042A37 (the original's gbr block of bup_file): +0 screen state, +1 next state, +2 device of SEGARALLY_0 (0 internal / 1 cartridge), +3 save mode (0 = no saving,
 * 1 = records, 2 = records + replays), +4 .. +5 flags of the backup screen, +6 = "factory records in use" (the screen's reset request). */
#include "satlib.h"
#include "menu_lib.h"
#include "satmenu.h"
#include "sat_save.h"

#define NAME_MAIN    "SEGARALLY_0"
#define NAME_REPLAY  "SEGARALLY_1"

static void bcd_date(BupDate *bd)                       /* 0x06034D68: the console's clock (the SMPC time at [0x0603DA9C]: year digits, month, day, hour, minute as BCD bytes) -> the BUP date fields */
{
    u32 p = R32(0x0603DA9C);
    u32 y = ((R8(p + 6) >> 4) & 15) * 1000 + (R8(p + 6) & 15) * 100 + ((R8(p + 5) >> 4) & 15) * 10 + (R8(p + 5) & 15);
    bd->year = (u8)(y - 1980);
    bd->month = (u8)(R8(p + 4) & 15);
    bd->day = (u8)(((R8(p + 3) >> 4) & 15) * 10 + (R8(p + 3) & 15));
    bd->time = (u8)(((R8(p + 2) >> 4) & 15) * 10 + (R8(p + 2) & 15));
    bd->min = (u8)(((R8(p + 1) >> 4) & 15) * 10 + (R8(p + 1) & 15));
    bd->week = 0;
}

static void make_dir(BupDir *h, const char *name, const char *comment, u32 datasize)   /* 0x06034D68 / 0x06034F40: the file header */
{
    BupDate bd;
    memset(h, 0, sizeof(*h));
    strcpy(h->filename, name);
    strcpy(h->comment, comment);
    h->language = 0;
    bcd_date(&bd);
    h->date = bios_bup_setdate(&bd);
    h->datasize = datasize;
}

u32 bup_check_main(void)                                /* 0x0602F5BC */
{
    u32 sum = 9, p = 0x060EEC00;
    int n = 0x4EF;
    while (n--) { sum += R32(p); p += 4; }
    return sum == R32(0x060EFFBC) ? 0 : 99;
}

u32 bup_check_replays(void)                             /* 0x0602F5E8 */
{
    u32 slot = 0x002C0000, r7 = 0;
    int k;
    for (k = 0; k < 4; k++) {
        if (R32(slot) != 0) {
            u32 sum = 9, p = slot + 4;
            int n = (0x3D00 - 4) >> 2;
            while (n--) { sum += R32(p); p += 4; }
            if (R32(slot) != sum) r7 = 99;
        }
        slot += 0x3D00;
    }
    return r7;
}

int bup_load(u32 lib_addr, u32 work_addr, u32 data)     /* 0x06034C04 */
{
    int i;
    bios_bup_init(lib_addr, work_addr);                 /* (the SMPC reset-disable / enable commands around the library calls are the console's) */
    for (i = 0; i < 3; i++)
        if (bios_bup_read(1, NAME_MAIN, data) == 0 && bup_check_main() == 0) return 0;
    for (i = 0; i < 3; i++)
        if (bios_bup_read(0, NAME_MAIN, data) == 0 && bup_check_main() == 0) return 0;
    return 1;
}

void bup_save_data(u32 data, u32 device)                /* 0x06034D68 .. 0x06034F3F */
{
    BupDir h;
    make_dir(&h, NAME_MAIN, "RECORDS   ", 0x13C0);
    bios_bup_write(device & 0xFF, &h, data, 0);
}

void bup_save_replays(u32 replays)                      /* 0x06034F40 */
{
    BupDir h;
    make_dir(&h, NAME_REPLAY, "GHOSTS    ", 0xF400);
    bios_bup_write(1, &h, replays, 0);
}

void bup_main_save(void)                                /* 0x0602FC40 */
{
    /* wait_slave_end() (the slave CPU is not run), interrupts masked */
    if (RS8(0x06042A30 + 3) > 0) {
        rec_data_expand(0x06041220);
        bup_save_data(0x060EEC00, R8(0x06042A30 + 2));
    }
}

void bup_replay_save(void)                              /* 0x0602FCB4: a time attack run (mode 4) that is faster than the stored replay of the course replaces it */
{
    u32 r10, r2, r3;
    int n;
    if (RS16(0x060AA860) != 4) return;
    r10 = 0x002C0000u + (u32)(u16)RS16(0x060AA87E) * 0x3D00u;
    r2 = 0x060B058C;
    r3 = R32(r2); r2 += 4;
    if ((s32)r3 >= RS32(r10 + 4)) return;
    W32(r10 + 4, r3);
    W32(r10 + 8, R32(r2)); r2 += 4;
    W32(r10 + 12, R32(r2)); r2 += 4;
    W32(r10 + 16, R32(r2)); r2 += 4;
    W8(r10 + 20, R8(0x060AA80A));
    W8(r10 + 21, R8(0x060AA811));
    W8(r10 + 22, R8(0x060AA809));
    copy_l(0x002CF800, r10 + 104, 0xF26);
    copy_l(0x060AA400, r10 + 32, 18);
    W8(r10 + 24, R8(0x060AA817));
    W8(r10 + 25, R8(0x060AA818));
    W8(r10 + 26, R8(0x060AA819));
    W8(r10 + 27, R8(0x060AA81A));
    W8(r10 + 28, R8(0x060AA81B));
    r2 = r10 + 4;
    r3 = 9;
    for (n = 0xF3F; n; n--) { r3 += R32(r2); r2 += 4; }
    W32(r10, r3);
    if (R8(0x06042A30 + 3) == 2) bup_save_replays(0x002C0000);
}

void bup_host_attach(int cartridge)                     /* host side (no original address) */
{
    W8(0x06042A32, 0);
    W8(0x06042A33, cartridge ? 2 : 1);
    W8(0x06042A35, 1);
}

void bup_screen_skip(void)                              /* NOT the original screen (0x0602F64E checks / formats the backup memory with message boxes): the PSP keeps the save in a host file */
{
    bup_host_attach(sat_save_has_cart());
    W32(0x060A001C, 0x06022894);
}

void menu_bup_register(void)
{
    menu_reg(0x0602FC40, bup_main_save);
    menu_reg(0x0602F64E, bup_screen_skip);
    menu_reg(0x0602FCB4, bup_replay_save);
}
