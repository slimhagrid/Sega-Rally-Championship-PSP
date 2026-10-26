// Sega Rally Championship (Saturn): memory map + symbols from RALLY.DBG for 0RALLY.DAT loaded at 0x06004000.
// Run as a pre-script of analyzeHeadless (see tools/ghidra_import.ps1); it needs extracted/analysis/symbols.tsv + modules.tsv (tools/dbg_parse.py).
//@category Rally
import java.io.File;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

public class SetupRally extends GhidraScript {
    static final long BASE = 0x06004000L;

    Address a(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    void block(String name, long start, long size, boolean x) throws Exception {
        Memory mem = currentProgram.getMemory();
        if (mem.getBlock(a(start)) != null) return;
        MemoryBlock b = mem.createUninitializedBlock(name, a(start), size, false);
        b.setRead(true);
        b.setWrite(true);
        b.setExecute(x);
        b.setVolatile(!x && start >= 0x25000000L);
    }

    boolean inMemory(long v) {
        return currentProgram.getMemory().contains(a(v));
    }

    // skip tables / buffers: only things that look like code become entry points
    boolean looksLikeCode(String name, long addr) throws Exception {
        String n = name.toLowerCase();
        String[] data = {"tbl", "table", "_dat", "data", "buf", "work", "_map", "_cl", "_cg", "param", "sintbl", "list"};
        for (String d : data) if (n.contains(d)) return false;
        if ((addr & 1) != 0) return false;
        int w = currentProgram.getMemory().getShort(a(addr)) & 0xFFFF;
        if (w == 0x0000) return false;                 // all-zero word: a table
        if ((w & 0xFFF0) == 0x0600) return false;      // high half of a 0x06xxxxxx pointer: a pointer table
        return true;
    }

    @Override
    public void run() throws Exception {
        File root = new File(getScriptArgs().length > 0 ? getScriptArgs()[0] : ".");
        File symf = new File(root, "extracted/analysis/symbols.tsv");
        File modf = new File(root, "extracted/analysis/modules.tsv");
        Memory mem = currentProgram.getMemory();

        // the loaded image: rename and make executable
        MemoryBlock img = mem.getBlock(a(BASE));
        img.setName("rally_image");
        img.setRead(true);
        img.setWrite(true);
        img.setExecute(true);
        long end = BASE + img.getSize();

        block("hwram_low", 0x06000000L, BASE - 0x06000000L, false);
        block("hwram_rest", end, 0x06100000L - end, true);       // sections WRAM / D / B and the buffers the game fills at run time
        block("lwram", 0x00200000L, 0x100000L, false);
        block("bios_rom", 0x00000000L, 0x80000L, false);
        block("smpc", 0x00100000L, 0x80L, false);
        block("backup_ram", 0x00180000L, 0x10000L, false);
        block("abus_cs0", 0x02000000L, 0x01000000L, false);       // the DBG puts model / texture / scroll data here (see NOTES.md)
        block("sound_ram", 0x25A00000L, 0x80000L, false);
        block("scsp_regs", 0x25B00000L, 0x1000L, false);
        block("vdp1_vram", 0x25C00000L, 0x80000L, false);
        block("vdp1_fb", 0x25C80000L, 0x40000L, false);
        block("vdp1_regs", 0x25D00000L, 0x20L, false);
        block("vdp2_vram", 0x25E00000L, 0x80000L, false);
        block("vdp2_cram", 0x25F00000L, 0x1000L, false);
        block("vdp2_regs", 0x25F80000L, 0x120L, false);
        block("scu_regs", 0x25FE0000L, 0xD0L, false);

        SymbolTable st = currentProgram.getSymbolTable();
        Namespace global = currentProgram.getGlobalNamespace();
        int labels = 0, skipped = 0, entries = 0;

        // symbols
        for (String line : Files.readAllLines(symf.toPath())) {
            String[] f = line.split("\t");
            if (f.length < 4) continue;
            long addr = Long.parseUnsignedLong(f[0], 16);
            String kind = f[1], name = f[2], mod = f[3];
            if (!inMemory(addr)) { skipped++; continue; }
            Namespace ns = st.getOrCreateNameSpace(global, mod, SourceType.IMPORTED);
            st.createLabel(a(addr), name, ns, SourceType.IMPORTED);
            labels++;
            if (!kind.equals("data") && addr >= BASE && addr < end && looksLikeCode(name, addr)) {
                st.addExternalEntryPoint(a(addr));
                entries++;
            }
        }
        // module starts
        for (String line : Files.readAllLines(modf.toPath())) {
            String[] f = line.split("\t");
            if (f.length < 4 || !f[1].equals("P")) continue;
            long addr = Long.parseUnsignedLong(f[2], 16);
            if (!inMemory(addr) || addr < BASE) continue;
            if (st.getSymbols(a(addr)).length == 0) {       // a DBG name at the same address keeps the primary label
                Namespace ns = st.getOrCreateNameSpace(global, f[0], SourceType.IMPORTED);
                st.createLabel(a(addr), "_module_" + f[0], ns, SourceType.IMPORTED);
            }
            currentProgram.getListing().setComment(a(addr), CodeUnit.PLATE_COMMENT, "module " + f[0] + " (0x" + f[3] + " bytes of code)");
        }

        st.addExternalEntryPoint(a(BASE));      // poweron_start
        println("SetupRally: " + labels + " labels, " + entries + " code entry points, " + skipped + " symbols outside the memory map");
    }
}
