/* M8: the console's backup memory (the BIOS backup library, "BUP") on host files, and the game's save / load layer on top of it (menu_bup.c).
 *
 * The game keeps its save data in two backup files (docs/menus.md, section "Save data"):
 *   SEGARALLY_0  comment "RECORDS"  0x13C0 bytes = the image at 0x060EEC00 (records, options, unlock flags, names; checksum at 0x060EFFBC)   written to the device chosen in [0x06042A32]
 *   SEGARALLY_1  comment "GHOSTS"   0xF400 bytes = the four stored replays at 0x002C0000 (0x3D00 bytes each, checksum in the first word)      written to device 1 (the memory cartridge)
 * The original calls the BIOS library through the vector table at 0x06000354 (BUP_Format, BUP_Stat, BUP_Write, BUP_Read, BUP_Delete, ..., BUP_Init at 0x06000358).  The port calls the
 * functions below instead.  Three modes:
 *   - stub (the default, the same as the oracle's BIOS stubs): every call returns BUP_OK and does nothing; a load therefore fails the checksum test and the factory data is used;
 *   - none (sat_save_open(NULL)): no backup memory at all: every call returns BUP_NON (the same visible result: factory data, nothing is saved);
 *   - files (sat_save_open(dir)): device 0 = the internal memory (32 KB, 64-byte blocks), device 1 = the memory cartridge (1 MB) if enabled; each device is one image file
 *     `BUP0.BIN` / `BUP1.BIN` in `dir` (a file list with the BUP directory entries), written after every change. */
#ifndef SAT_SAVE_H
#define SAT_SAVE_H
#include "sat.h"

enum { BUP_OK = 0, BUP_NON = 1, BUP_UNFORMAT = 2, BUP_WRITE_PROTECT = 3, BUP_NOT_ENOUGH_MEMORY = 4, BUP_NOT_FOUND = 5, BUP_FOUND = 6, BUP_NO_MATCH = 7, BUP_BROKEN = 8 };

typedef struct { char filename[12]; char comment[11]; u8 language; u32 date; u32 datasize; u16 blocksize; } BupDir;     /* the directory entry (34 bytes on the console) */
typedef struct { u32 totalsize, totalblock, blocksize, freesize, freeblock, datanum; } BupStat;
typedef struct { u8 year, month, day, time, min, week; } BupDate;                                                    /* year = years since 1980 */

/* host configuration */
void sat_save_stub(void);                       /* mode stub (the default) */
int sat_save_open(const char *dir);             /* mode files (dir) or none (NULL); returns 0 when the images could be read / created */
void sat_save_cart(int present);                /* device 1 present (files mode: BUP1.BIN with 1 MB; default: absent) */
int sat_save_has_cart(void);

/* the BIOS library on the host (the original's vector table) */
u32 bios_bup_init(u32 lib_addr, u32 work_addr);
u32 bios_bup_format(u32 device);
u32 bios_bup_stat(u32 device, u32 datasize, BupStat *st);
u32 bios_bup_write(u32 device, const BupDir *dir, u32 data_addr, u32 wmode);        /* data_addr: console address of the data */
u32 bios_bup_read(u32 device, const char *filename, u32 data_addr);
u32 bios_bup_delete(u32 device, const char *filename);
u32 bios_bup_dir(u32 device, const char *filename, u32 tbsize, BupDir *tb, u32 *found);
u32 bios_bup_verify(u32 device, const char *filename, u32 data_addr);
u32 bios_bup_setdate(const BupDate *d);         /* minutes since 1980-01-01 00:00 */
void bios_bup_getdate(u32 code, BupDate *d);

/* the game's layer (menu_bup.c) */
int bup_load(u32 lib_addr, u32 work_addr, u32 data);        /* 0x06034C04: reads SEGARALLY_0 into the image at `data`, device 1 then device 0, 3 tries each, checksum tested; 0 = loaded */
void bup_save_data(u32 data, u32 device);                   /* 0x06034D68: writes the image at `data` as SEGARALLY_0 to the device */
void bup_save_replays(u32 replays);                         /* 0x06034F40: writes the 4 replays (0xF400 bytes at `replays`) as SEGARALLY_1 to device 1 */
u32 bup_check_main(void);                                   /* 0x0602F5BC: checksum of the image at 0x060EEC00: 0 = good, 99 = bad */
u32 bup_check_replays(void);                                /* 0x0602F5E8: checksum of the stored replays: 0 = good, 99 = a replay is bad */
void bup_main_save(void);                                   /* 0x0602FC40: packs the live records into the image and saves it when [0x06042A33] > 0 */
void bup_replay_save(void);                                 /* 0x0602FCB4: stores the race's replay of a time attack (mode 4) when faster and saves the replays when [0x06042A33] == 2 */
void rec_data_expand(u32 src);                              /* 0x0602F404: the records at src -> the image at 0x060EEC00 (+ tail copies, checksum) */
void rec_data_unpack(void);                                 /* 0x0602F4E4: the image at 0x060EEC00 -> the live records */
void bup_host_attach(int cartridge);                        /* what the backup screen (0x0602F64E, not ported) leaves behind when the memory works: [0x06042A32] = 0 (internal), [0x06042A33] = 1 (2 with a cartridge), [0x06042A35] = 1 */
void bup_screen_skip(void);                                 /* the PSP replacement of the backup screen, registered at 0x0602F64E: attach, then the main vector goes to the mode select */

#endif
