/* M8: the power-on sequence (boot / init / main modules) and the main loop.  Hardware and library calls whose only effect is outside the program's own memory
 * (BIOS vectors, SMPC library, sound driver, CD block library) are not modelled; the oracle (tools/menu_env.py) stubs the same calls. */
#include "satlib.h"
#include "satmenu.h"
#include "sat_save.h"

/* ---- the save data (backup memory): bup_load (menu_bup.c) reads the save file through the BIOS backup library model of sat_save.c; 0 = loaded, else the factory defaults are built. ---- */

static void copyl(u32 src, u32 dst, u32 n) { rcopy(src, dst, n); }                 /* FUN_0601D694 */
static void copyb(u32 src, u32 dst, u32 n) { while (n--) { W8(dst, R8(src)); src++; dst++; } }   /* FUN_0601D674 */

/* FUN_0602F404: the default record data (0x0029EA98, 0x180 entries of 8 bytes) -> 5 bytes each at 0x060EEC00, then the tail copies */
void rec_data_expand(u32 src)                          /* 0x0602F404 */
{
    u32 dst = 0x060EEC00;
    int n = 0x180;
    while (n--) {
        u32 r1 = R32(src), r2 = R32(src + 4), r3, r7;
        src += 8;
        r7 = (r2 & 0x01000000u) >> 9;
        r3 = ((r2 & 0x70000000u) >> 8) << 1;
        r1 += r3;
        W8(dst++, r1 >> 16);
        W8(dst++, r1 >> 8);
        W8(dst++, r1);
        r1 = (((r2 >> 16) & 0xFF) << 16) & r2;
        r1 >>= 6;
        r3 = (((r2 >> 8) & 0xFF) << 8) & r2;
        r3 >>= 3;
        r1 += r3;
        r1 += r2 & 0xFF;
        r1 += r7;
        W8(dst++, r1 >> 8);
        W8(dst++, r1);
    }
    copyl(0x06041E20, 0x060EF380, 0x302);
    W8(0x060EFF88, R8(0x06040062) & 22);
    copyb(0x06040063, 0x060EFF89, 33);
    copyb(0x06042A38, 0x060EFFAA, 14);
    {                                                    /* checksum of the image */
        u32 sum = 9, p = 0x060EEC00;
        int k;
        for (k = 0; k < 0x4EF; k++, p += 4) sum += R32(p);
        W32(0x060EFFBC, sum);
    }
}

/* FUN_0602F4E4: the 5-byte records at 0x060EEC00 -> two longs each at 0x06041220 */
void rec_data_unpack(void)                             /* 0x0602F4E4 */
{
    u32 src = 0x060EEC00, dst = 0x06041220;
    int n = 0x180;
    while (n--) {
        u32 b0 = R8(src), b1 = R8(src + 1), b2 = R8(src + 2), b3 = R8(src + 3), b4 = R8(src + 4);
        u32 r2, r5, r1, r3, t;
        src += 5;
        r2 = (((b0 & 0xE0) >> 1) << 24);
        r5 = ((b0 & 31) << 16) + (b1 << 8) + b2;
        W32(dst, r5);
        dst += 4;
        r1 = (b3 << 8) + b4;
        r3 = (r1 & 0x7C00) << 6;
        t = (r1 & 0x3E0) << 3;
        r3 += t;
        r3 += r1 & 31;
        r3 += (r1 & 0xFFFF8000u) << 9;
        r3 += r2;
        W32(dst, r3);
        dst += 4;
    }
    copyl(0x060EF380, 0x06041E20, 0x302);
    copyb(0x060EFF88, 0x06040062, 0x22);
    copyb(0x060EFFAA, 0x06042A38, 14);
    W8(0x06040062, R8(0x06040062) | (R8(0x06042A40) << 5));
}

static void rec_defaults(void)                          /* 0x0602FB8C: no backup memory: the factory records */
{
    rec_data_expand(0x0029EA98);
    rfillff(0x060EFF88, 0, 0x302);
    W8(0x06040062, 0);
    W16(0x060EFFAA, 0x0101);
    W32(0x060EFFAC, 0x00010000);
    W32(0x060EFFB0, 6);
    W32(0x060EFFB4, 0x00010203);
    if (R32(R32(0x060AA960) + 4) & 0x200) W8(0x060EFFB2, 1);
}

static void rec_init(void)                              /* 0x0602FBF8 */
{
    int fail = bup_load(0x06042A00, 0x06046A00, 0x060EEC00);
    if (fail) rec_defaults();
    rec_data_unpack();
    copyl(0x0029F698, 0x06041000, 0x44);                 /* rec_table_init */
    copyl(0x0029F7A8, 0x06041110, 0x44);                 /* cmp_table_init */
    W32(0x06042A30, 0);
    W32(0x06042A34, 0);
}

static void watanabe_init(void)                         /* 0x0601F844 */
{
    W16(0x060A88C0, 0);
    W16(0x06040012, 1);
    W16(0x06040014, 1);
    W16(0x06040016, 1);
    W16(0x060A887A, 0);
    W16(0x0604001A, 0);
    W16(0x06040020, 0);
    W16(0x06040026, 0);
    W16(0x06040028, 0);
    W8(0x060A88EE, 0x7F);
    W8(0x060A88EF, 0x7F);
    W8(0x060A88F0, 0x7F);
    W8(0x0604002A, 0);
    W8(0x0604002D, 0); W8(0x0604002E, 0); W8(0x0604002F, 0); W8(0x06040030, 0); W8(0x06040031, 0); W8(0x06040032, 0); W8(0x06040033, 0);
    W8(0x06040034, 0); W8(0x06040035, 0); W8(0x06040036, 0); W8(0x06040037, 0); W8(0x06040038, 0); W8(0x06040039, 0); W8(0x0604003A, 0);
    W8(0x0604003B, 3);
    W8(0x0604003C, 0);
    W8(0x0604003D, 0);
}

static void record_sel_init(void)                       /* 0x0602D4D4 */
{
    W32(0x06040040, 0);
    W32(0x06040044, 0);
    W32(0x06040048, 0x04040404);
    W32(0x0604004C, 0x04050504);
    W8(0x06042A46, 0);
    W8(0x06042A47, 0);
    W8(0x06042A48, 0);
}

/* CD file requests (0x0600F550): a request block names the file (and a raw destination for plain files); the harness loads scatter files and raw files like the CD loader */
static void cd_file_init(void)                          /* 0x06004AEC */
{
    W8(0x060AA803, R8(0x060AA803) | 0x10);
    sat_cd_request(0x0601F3D4);                          /* set_replay_camera_data: CAMERA.DAT -> camera_dat_buf */
    sat_load_file("PRGDATA.DAT");
}

static void init_nclram(void)                           /* 0x06004B20 */
{
    int i;
    for (i = 0; i < 32; i++) W8(0x06040064 + i, R8(0x06004B3C + i));
    W8(0x0604000C, 0xC0);
}

static void pad_kind(void)                              /* 0x060053B8 */
{
    u32 r13 = R8(0x060AA803) & 0xFE;
    r13 = (RS8(0x060AA990 + 20) == 16) ? (r13 | 4) : (r13 & 0xFB);
    r13 = (RS8(0x060AA990 + 22) == 16) ? (r13 | 8) : (r13 & 0xF7);
    W8(0x060AA803, r13);
}

void pad_kind_init(void)                         /* 0x06005310 */
{
    W8(0x060AA803, R8(0x060AA803) & 0xFE);
    sync_vin(); sync_vout(); sync_vin(); sync_vout();
    sync_vin(); sync_vout();
    pad_read();
    sync_vin(); sync_vout();
    sync_vin(); sync_vout();
    pad_read();
    pad_kind();
    W8(0x060AA803, R8(0x060AA803) | 1);
}

void swclear(void)                               /* 0x060059B0 */
{
    W16(0x060AFF02, 0xFFFF);
    W8(0x060AFF09, 0xFF);
    W8(0x060AFF0A, 0xFF);
    W16(0x060AFF10, 0xFFFF);
    W8(0x060AFF17, 0xFF);
    W8(0x060AFF18, 0xFF);
    W16(0x060AA996, 0);
    W16(0x060AA992, 0);
    W16(0x060AA994, 0);
    W16(0x060AA998, 0);
    W16(0x060AA99A, 0);
    W32(0x060AA99C, 0);
    W32(0x060AA9A0, 0);
}

static void init_smpcif(void)                           /* 0x060052B4 */
{
    swclear();
    sync_vout();
    sync_vout();
    W32(0x060AA960, R8(0x0603DAAC) ? 0x0603DAA0 : 0);
    pad_kind_init();
    sync_vout();
}

void sat_boot(void)                                     /* 0x06004000 poweron_start (cold start) */
{
    int i;
    tv_off_boot();
    /* init_alwram */
    rfillff(0x06048000, 0, 0x2000);
    rfillff(0x060F0000, 0, 0x14000);
    sync_vout();                                         /* init_vect_slave */
    sync_vout();
    cd_file_init();
    init_nclram();
    rec_init();
    watanabe_init();
    record_sel_init();
    W8(0x060AA82B, 1);
    W32(0x06040008, 0x70776F6E);
    W8(0x0604000D, R8(0x0604000D) | 1);
    init_smpcif();
    init_vram();
    /* FUN_06016B74: sound driver start-up; the program's own variables it sets */
    W8(0x060AA82C, 1);
    W8(0x060AA80B, 0);
    W8(0x060AA80D, 1);
    W8(0x060AAAD8, 0);
    /* init_texture */
    W32(0x060D008C, 0); W32(0x060D0090, 0); W32(0x060D0094, 0); W32(0x060D0098, 0);
    /* main_init */
    sync_vout();
    sync_vout();
    if (!(R8(0x06040007) & 1)) {
        for (i = 0; i < 7; i++) W8(0x06040000 + i, R8(R32(0x0603DA9C) + (u32)i));      /* get_time: the RTC bytes (not available: zeros), copied in reverse order */
        W8(0x06040007, 1);
    }
    /* mainv_load: the main loop is copied to 0x060A0000 (its code is not executed here); the words the program patches at run time */
    W8(0x060A0001, 2);                                   /* `mov #2,r0`: the V-OUT recipe of every pass */
    W8(0x060A0013, 2);                                   /* `mov #2,r4`: the sync argument */
    W32(0x060A001C, 0x0600D214);                         /* the main vector: adv_init */
}

/* the main loop (0x060A0000 copy): [0x060AA801] = imm; call *[0x060A001C]; sync(imm2).  `first` (0 = none) is a screen handler entered directly the first time round (tools/menu_env.py start()) */
void sat_main_loop_from(u32 first)
{
    for (;;) {
        W8(0x060AA801, R8(0x060A0001));
        if (first) { u32 v = first; first = 0; menu_call(v); }
        else menu_call(R32(0x060A001C));
        sync_n(R8(0x060A0013));
    }
}

void sat_main_loop(unsigned max_ticks)
{
    (void)max_ticks;
    sat_main_loop_from(0);
}
