"""M8 (menus): the disc data the ported menu code reads -> `extracted/psp/menu/*.blk` (read by src/sat.c).

    python tools/conv_menu.py

* every scatter-load `.DAT` / `.MAD` / `.CG` ... file the menu code loads is expanded the way the original loader leaves it in memory (BPE blocks expanded, the name-table
  offset added) and stored as MBK1 files: "MBK1", u32 nblocks, per block u32 dest address, u32 size, bytes (all little endian except the block bytes, which stay big endian).
  The C port's loader (`sat_load_file("NAME.DAT")`) copies the blocks to their destination addresses in the emulated console memory, exactly like the original.
* `ROM.blk`: the data tables of the program image itself (0RALLY.DAT at 0x06004000): everything between the functions, plus the extra ranges in EXTRA (tables that sit inside a
  function's bounds).  The C code reads them at their original addresses.
Nothing here is committed: it is derived from the user's disc (extracted/ is ignored).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from datfile import Scatter  # noqa: E402

DISC = os.path.join(ROOT, "extracted", "disc", "files")
OUT = os.path.join(ROOT, "extracted", "psp", "menu")
AN = os.path.join(ROOT, "extracted", "analysis")

# tables that lie inside the bounds of a function (jump tables, string tables); (start, end)
EXTRA = [
]

# the record screens (src/menu_records.c): the code-embedded tables they read (cursor / sprite / picture tables, request blocks, the page layout tables)
EXTRA += [
    (0x0602C7A4, 0x0602C7B4),       # rec_dataset: course / transmission translation tables
    (0x0602CEC8, 0x0602CED8),       # record_main first-run init: the VDP2 register words
    (0x0602D28C, 0x0602D2B8),       # record_main: the VDP2 register table
    (0x0602D3B4, 0x0602D3EC),       # picture requests (RECORD.DAT / RECORD2.DAT)
    (0x0602E7FC, 0x0602E8AC),       # record_menu_init: the button tables
    (0x0602EFBC, 0x0602F02C),       # the palette fade coefficients
    (0x0602F140, 0x0602F194),       # the cursor frame sprite table
    (0x0602F2A4, 0x0602F2B4),       # the mark sprite table
    (0x06032EA0, 0x06032F60),       # the header button tables
]

# the option screen (src/menu_option.c): tables inside option_main
EXTRA += [
    (0x06004CB0, 0x06004CBC),       # the PCM data file name (0x06004C90, run when the sound tests start)
    (0x060332C4, 0x060332CE),       # page 1: the value range of every item
    (0x060334A0, 0x060334AA),       # page 2: the value range of every item
    (0x060336CC, 0x060336E4),       # the line-scroll flags of the page slide
    (0x060339A4, 0x060339C0),       # option_pict_init: the CD request block (OPTION.DAT)
]

# the screens after a race (src/menu_post.c, menu_name.c, menu_result.c): the post-goal dispatcher .. the result screen, with the tables / request blocks / literal pools between its functions
EXTRA += [
    (0x0602FF84, 0x06033000),
    (0x0601ADBC, 0x0601ADD8),       # set_alphabet_model: the CD request (ALPHA.MDL -> 0x06048000)
    (0x06011EC0, 0x06011ED0),       # init_game_over: the file name (GMGOVER.DAT)
]

# the files the menus load; the scatter files of the race (course data, cars, sound) are not here
FILES = [
    "PRGDATA.DAT", "BACKUP.DAT", "NAME.DAT", "RESULT.DAT", "RESULT2.DAT", "OPTION.DAT", "RECORD.DAT", "RECORD2.DAT", "REC_RET.DAT",
    "TM_REC.DAT", "BT_REC.DAT", "TM_ATT0.DAT", "TM_ATT1.DAT", "CAR_SET0.DAT", "GMGOVER.DAT",
    "MD_SLT0.DAT", "MD_SLT1.DAT", "MD_SLT2.DAT", "MD_SLT3.DAT", "MD_SLT4.DAT", "MD_SLT5.DAT",
    "SCRL_DST.DAT", "SCRL_FST.DAT", "SCRL_LST.DAT", "SCRL_MST.DAT", "SCRL_SLT.DAT", "SCRL_STR.DAT", "SCRL_TTL.DAT",
    "CGDATA.DAT", "CGDATAAD.DAT", "CGDATAUS.DAT", "CGDATAVS.DAT", "CGDATA_F.DAT", "ASCII.DAT", "PHOTOTBL.DAT", "EVENTTBL.DAT", "EVENTTBL.MAD",
    "ANIMAL2.MAD", "ANIMAL2.MDL", "ANIMAL2.TEX", "ANIMAL.MOT", "OBJECT.MAD", "OBJECT.MDL", "OBJECT.TEX",
    "SELECT.MAD", "SELECT.MDL", "SELECT.TEX", "ENDING.MAD", "ENDING.MDL", "ENDING.TEX", "STAFF.CG", "STAFF.MAP",
    "ALPHA.MDL", "CAMERA.DAT", "PCMDATA.DAT",
]

# the photo module (src/menu_photo.c): the course data of the staff roll's scene (sky bitmap, road block grid) and the four course models
BPE_RAW = ["STAFF.CG"]       # raw files whose CD request has flag 4: BPE streams, stored expanded
FILES += ["ADDESERT.DAT", "ADFOREST.DAT", "ADMOUNT.DAT", "ADLAKE.DAT", "DESERT.MAD", "FOREST.MAD", "MOUNT.MAD", "LAKE.MAD"]
EXTRA += [
    (0x0600821C, 0x060082A0),       # get_poly_map: the block list tables
]

# the title's demo run (src/menu_adv.c, set_advdata 0x0600E088): the recorded cars (80 bytes per pass), the camera script of the demo; one file per demo stage
FILES += ["ADSAFARI.MAD", "ADYOSEMI.MAD", "ADCORSE.MAD"]
EXTRA += [
    (0x0600E0B4, 0x0600E0E6),       # set_advdata: the file names of the three demo runs
    (0x06004C34, 0x06004CC0),       # init_adv_vdp2: the colour tables (CGDATAAD.DAT's palettes) and the file name
]

# the ceremony and the ending (src/menu_ending.c): the ceremony's scene (the models are OBJECT.MAD / ENDING.MAD, in FILES above)
FILES += ["CEDESERT.DAT", "BGLAKE.DAT", "CRLAKE.DAT"]          # the ending replays the lake: the course data (collision grid, block lists) and the game screen graphics
EXTRA += [
    (0x0601E4E8, 0x0601F6E0),       # the ceremony and ending modules: literal pools, the confetti vector, the animation tables, the camera key table
    (0x06013B6C, 0x06013B7C),       # init_replay_win: the window words
]


# the sound (M9, src/snd_lib.c): the driver builds, the stage / car banks, the co-driver's phrases, the jingles of the select / name / ceremony / ending screens (they load into the sound RAM
# 0x25A00000 .. or, for PCMDATA / DJn, into the work RAM)
FILES += ["SDDRVS.DAT", "ESDDRVS.DAT", "P2SDDRVS.DAT", "SDDESERT.DAT", "SDDESE_N.DAT", "SDFOREST.DAT", "SDFORE_N.DAT", "STRATOS1.DAT",
          "CELICA1.DAT", "CELICA2.DAT", "CELICA3.DAT", "CELICA4.DAT", "CELICA5.DAT", "CELICAE.DAT", "LANCIA1.DAT", "LANCIA2.DAT", "LANCIA3.DAT", "LANCIA4.DAT", "LANCIA5.DAT", "LANCIAE.DAT",
          "SDCERE.DAT", "SDENDING.DAT", "SDSELECT.DAT", "SDNAME.DAT", "SDORGSEL.DAT", "SDORGSNG.DAT",
          "OOH.DAT", "OOH2.DAT", "GET.DAT", "GET2.DAT", "TAKE.DAT", "TAKE2.DAT", "WAY.DAT", "WAY2.DAT", "WHO.DAT", "WHO2.DAT", "SMOKIN.DAT", "SMOKIN2.DAT", "SMOOTH.DAT", "SMOOTH2.DAT", "FENDER.DAT", "FENDER2.DAT",
          "DJ1.DAT", "DJ2.DAT", "DJ3.DAT", "DJ4.DAT", "DJ5.DAT"]


def write_blk(path, blocks):
    with open(path, "wb") as f:
        f.write(b"MBK1")
        f.write(struct.pack("<I", len(blocks)))
        for dest, data in blocks:
            f.write(struct.pack("<II", dest, len(data)))
            f.write(data)


def functions():
    out = []
    for ln in open(os.path.join(AN, "functions.tsv")):
        p = ln.rstrip("\n").split("\t")
        if len(p) >= 4:
            out.append((int(p[0], 16), int(p[0], 16) + int(p[1])))
    out.sort()
    return out


def rom_blocks():
    img = open(os.path.join(DISC, "0RALLY.DAT"), "rb").read()
    base = 0x06004000
    top = 0x0603D880
    blocks = []
    cur = base
    for a, b in functions():
        if not (base <= a < top):
            continue
        if a > cur:
            blocks.append((cur, a))
        cur = max(cur, b)
    if cur < top:
        blocks.append((cur, top))
    blocks += list(EXTRA)
    blocks.sort()
    out = []
    for a, b in blocks:
        if b - a >= 2:
            out.append((a, img[a - base:b - base]))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    rb = rom_blocks()
    write_blk(os.path.join(OUT, "ROM.blk"), rb)
    print("ROM.blk: %d regions, %d bytes" % (len(rb), sum(len(d) for _, d in rb)))
    for name in FILES:
        p = os.path.join(DISC, name)
        if not os.path.isfile(p):
            print("missing", name)
            continue
        try:
            s = Scatter(p)
            if not s.blocks or not s.check():
                raise ValueError("not a scatter-load layout")
            blocks = [(b.dest, s.expanded(b)) for b in s.blocks]
        except Exception as e:                      # not a scatter file (the converter keeps it raw: the loader that reads it handles it)
            print("%s: not a scatter file (%s): stored raw" % (name, e))
            data = open(p, "rb").read()
            if name in BPE_RAW:                     # loaded by a CD request of flag 4 (0x0600F154): the loader expands the BPE stream into the destination, the port's raw loader gets it expanded
                from datfile import bpe_expand
                data = bpe_expand(data)
            write_blk(os.path.join(OUT, name + ".raw"), [(0, data)])
            continue
        write_blk(os.path.join(OUT, name + ".blk"), blocks)
        print("%-14s %2d blocks %8d bytes" % (name, len(blocks), sum(len(d) for _, d in blocks)))


if __name__ == "__main__":
    main()
