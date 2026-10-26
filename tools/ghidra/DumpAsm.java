// Disassembly listing with literal-pool values resolved.  Script args: <project root> <name | 0xADDR> [<name | 0xADDR> ...]
// -> extracted/analysis/asm/<name>.asm   (pc-relative loads show "; = 0xVALUE symbol")
//@category Rally
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class DumpAsm extends GhidraScript {
    String sym(Address a) {
        Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
        if (s == null) return "";
        String ns = s.getParentNamespace().isGlobal() ? "" : s.getParentNamespace().getName() + "::";
        return ns + s.getName();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(new File(args[0]), "extracted/analysis/asm");
        dir.mkdirs();
        java.util.List<Function> todo = new java.util.ArrayList<>();
        if (args.length > 1 && args[1].equals("EVERY")) {
            for (Function g : currentProgram.getFunctionManager().getFunctions(true)) todo.add(g);
        }
        for (Function g : todo) dump(dir, g);
        for (int i = 1; i < args.length && todo.isEmpty(); i++) {
            Function f = null;
            String a = args[i];
            if (a.startsWith("0x")) {
                f = currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(a.substring(2), 16)));
            } else {
                for (Function g : currentProgram.getFunctionManager().getFunctions(true))
                    if (g.getName().equals(a)) { f = g; break; }
            }
            if (f == null) { println("DumpAsm: no function " + a); continue; }
            dump(dir, f);
        }
        println("DumpAsm: done");
    }

    void dump(File dir, Function f) throws Exception {
        {
            File out = new File(dir, f.getName() + ".asm");
            try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
                w.println("; " + f.getParentNamespace().getName() + "::" + f.getName() + " @ " + f.getEntryPoint() + " (" + f.getBody().getNumAddresses() + " bytes)");
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    StringBuilder sb = new StringBuilder();
                    sb.append(String.format("%08x  %-28s", ins.getAddress().getOffset(), ins.toString()));
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (!to.isMemoryAddress()) continue;
                        if (r.getReferenceType().isData() && (to.getOffset() & 3) == 0 && to.getOffset() >= 0x06004000L && to.getOffset() < 0x0603DA94L) {
                            long v = currentProgram.getMemory().getInt(to) & 0xFFFFFFFFL;
                            String s = sym(to.getNewAddress(v));
                            sb.append(String.format("  ; [%08x] = 0x%08x %s", to.getOffset(), v, s));
                        } else {
                            String s = sym(to);
                            if (!s.isEmpty()) sb.append("  ; -> " + s);
                        }
                    }
                    w.println(sb);
                }
            }
        }
    }
}
