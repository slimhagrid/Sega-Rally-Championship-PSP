"""M1: pull everything off the Sega Rally Championship (Saturn) disc image.

    python tools/extract_disc.py ["Sega Rally Championship (USA)/Sega Rally Championship (USA).cue"] [--force]

Writes (all under extracted/, which is git-ignored):
    disc/system_area.bin      sectors 0-15 of the data track (Saturn header, security code, IP)
    disc/files/<NAME>         every file of the ISO9660 filesystem
    disc/catalog.txt          human-readable listing (header, tracks, files, sizes, SHA-1)
    disc/catalog.json         the same, for the other tools
    audio/trackNN.wav         CD-audio tracks, 44.1 kHz 16-bit stereo, from INDEX 01 (the pregap is dropped)
Re-running skips files that already exist with the right size (use --force to rewrite).
"""
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from cdimage import Disc, CD_BYTES_PER_SECOND, RAW_SECTOR  # noqa: E402

DEFAULT_CUE = os.path.join(ROOT, "Sega Rally Championship (USA)", "Sega Rally Championship (USA).cue")
OUT = os.path.join(ROOT, "extracted")


def saturn_header(sysarea):
    """The 0x100-byte Saturn disc header at the start of the system area (fields ASCII padded with spaces, words big-endian)."""
    def s(a, n):
        return sysarea[a:a + n].decode("ascii", "replace").strip()

    def w(a):
        return struct.unpack(">I", sysarea[a:a + 4])[0]
    return {
        "hardware_id": s(0x00, 0x10), "maker": s(0x10, 0x10), "product_number": s(0x20, 0x0a), "version": s(0x2a, 0x06),
        "release_date": s(0x30, 0x08), "device_info": s(0x38, 0x08), "area_symbols": s(0x40, 0x0a),
        "peripherals": s(0x50, 0x10), "title": s(0x60, 0x70),
        "ip_size": w(0xe0), "stack_master": w(0xe8), "stack_slave": w(0xec),
        "first_read_address": w(0xf0), "first_read_size": w(0xf4),
    }


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    force = "--force" in sys.argv
    cue = os.path.abspath(args[0]) if args else DEFAULT_CUE
    disc = Disc(cue)
    os.makedirs(os.path.join(OUT, "disc", "files"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "audio"), exist_ok=True)
    catalog = {"cue": os.path.basename(cue), "tracks": [], "files": []}
    log = []

    # --- system area -------------------------------------------------------------------------
    sysarea = disc.system_area()
    with open(os.path.join(OUT, "disc", "system_area.bin"), "wb") as f:
        f.write(sysarea)
    catalog["header"] = hdr = saturn_header(sysarea)
    catalog["volume_label"] = disc.volume_label()
    log.append(f"Disc: {hdr['title']!r} {hdr['product_number']} {hdr['version']} released {hdr['release_date']} areas {hdr['area_symbols']!r} "
               f"peripherals {hdr['peripherals']!r} ISO label {catalog['volume_label']!r}")
    log.append(f"  IP size 0x{hdr['ip_size']:x}, stacks M 0x{hdr['stack_master']:x} / S 0x{hdr['stack_slave']:x}, "
               f"first read address 0x{hdr['first_read_address']:08x} size 0x{hdr['first_read_size']:x}")

    # --- tracks ------------------------------------------------------------------------------
    log.append("")
    log.append("Tracks")
    for t in disc.tracks:
        h = hashlib.sha1()
        with open(t.path, "rb") as f:
            for blk in iter(lambda: f.read(1 << 20), b""):
                h.update(blk)
        secs = t.sectors
        entry = {"number": t.number, "mode": t.mode, "file": os.path.basename(t.path), "sectors": secs,
                 "pregap_sectors": t.pregap_sectors, "seconds": round(secs / 75, 2),
                 "size": os.path.getsize(t.path), "sha1": h.hexdigest()}
        if os.path.getsize(t.path) % t.sector_size:
            entry["warning"] = "file size is not a whole number of sectors"
        catalog["tracks"].append(entry)
        log.append(f"  {t.number:02d} {t.mode:11s} {secs:7d} sectors {secs / 75 / 60:6.2f} min  pregap {t.pregap_sectors:3d}  sha1 {h.hexdigest()}")

    # --- ISO9660 files -----------------------------------------------------------------------
    log.append("")
    log.append("Files (ISO9660 of track 1, in directory order: the Saturn BIOS loads the first one at the first read address)")
    entries = [f for f in disc.iso_files() if not f.is_dir]
    catalog["first_read_file"] = entries[0].path if entries else None
    for fi in entries:
        dest = os.path.join(OUT, "disc", "files", fi.path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if force or not os.path.exists(dest) or os.path.getsize(dest) != fi.size:
            data = disc.read_file(fi)
            with open(dest, "wb") as f:
                f.write(data)
        else:
            with open(dest, "rb") as f:
                data = f.read()
        sha = hashlib.sha1(data).hexdigest()
        catalog["files"].append({"path": fi.path, "lba": fi.lba, "sectors": (fi.size + 2047) // 2048, "size": fi.size, "sha1": sha})
        log.append(f"  {fi.path:16s} lba {fi.lba:6d}  {fi.size:10d} bytes  {sha}")
        if fi.path == "IP.BIN":   # the IP file is a copy of the start of the system area (header + security code + initial program)
            catalog["ip_bin_matches_system_area"] = data == sysarea[:len(data)]
    log.append("")
    log.append(f"First read file: {catalog['first_read_file']} (loaded at 0x{hdr['first_read_address']:08x})")
    log.append(f"IP.BIN file equals the start of the system area: {catalog.get('ip_bin_matches_system_area')}")

    # --- gaps between files (anything on the disc the filesystem does not name) ----------------
    used = sorted((f["lba"], f["lba"] + f["sectors"]) for f in catalog["files"])
    gaps, pos = [], 16 + 16  # system area + a first stab at the filesystem structures
    for a, b in used:
        if a > pos:
            gaps.append((pos, a))
        pos = max(pos, b)
    total = catalog["tracks"][0]["sectors"]
    if total > pos:
        gaps.append((pos, total))
    catalog["unreferenced_ranges"] = gaps
    nonzero = [(a, b) for a, b in gaps if any(any(disc.read_sector(l)) for l in range(a, b))]
    catalog["unreferenced_nonzero_ranges"] = nonzero
    log.append("Sector ranges of track 1 not covered by any file (filesystem structures, padding): " +
               ", ".join(f"{a}-{b - 1} ({b - a})" for a, b in gaps))
    log.append("  of those, ranges holding non-zero data: " + (", ".join(f"{a}-{b - 1}" for a, b in nonzero) or "none"))

    # --- audio -------------------------------------------------------------------------------
    log.append("")
    log.append("Audio -> extracted/audio/")
    for t in disc.audio_tracks():
        dest = os.path.join(OUT, "audio", f"track{t.number:02d}.wav")
        pcm_len = (t.sectors - t.index01) * RAW_SECTOR
        if force or not os.path.exists(dest) or os.path.getsize(dest) != pcm_len + 44:
            disc.write_wav(t.number, dest)
        with open(t.path, "rb") as f:   # is the pregap silent? (tells us whether dropping it loses anything)
            f.seek((t.index00 or 0) * RAW_SECTOR)
            pre = f.read(t.pregap_sectors * RAW_SECTOR)
        silent = not any(pre)
        for e in catalog["tracks"]:
            if e["number"] == t.number:
                e["pregap_silent"] = silent
                e["wav"] = f"audio/track{t.number:02d}.wav"
                e["wav_seconds"] = round(pcm_len / CD_BYTES_PER_SECOND, 2)
        log.append(f"  track{t.number:02d}.wav  {pcm_len / CD_BYTES_PER_SECOND:7.1f} s  pregap {t.pregap_sectors} sectors {'silent' if silent else 'NOT silent'}")

    with open(os.path.join(OUT, "disc", "catalog.json"), "w") as f:
        json.dump(catalog, f, indent=1)
    with open(os.path.join(OUT, "disc", "catalog.txt"), "w") as f:
        f.write("\n".join(log) + "\n")
    print("\n".join(log))
    disc.close()


if __name__ == "__main__":
    main()
