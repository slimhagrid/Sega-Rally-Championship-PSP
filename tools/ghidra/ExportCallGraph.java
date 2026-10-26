// Export the call graph to extracted/analysis/callgraph.tsv:  caller_addr  caller_name  callee_addr  callee_name  kind
// kind: call (jsr/bsr resolved by Ghidra), ptr (a literal-pool word that holds a function entry), jmp (branch to another function)
// plus extracted/analysis/datarefs.tsv: function  data_symbol  address  (literal-pool words that hold the address of a named data symbol)
//@category Rally
import java.io.File;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class ExportCallGraph extends GhidraScript {
    static final long IMG_LO = 0x06004000L, IMG_HI = 0x0603DA94L;

    String label(Address a) {
        Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
        if (s == null) return "";
        String ns = s.getParentNamespace().isGlobal() ? "" : s.getParentNamespace().getName() + "::";
        return ns + s.getName();
    }

    @Override
    public void run() throws Exception {
        File root = new File(getScriptArgs()[0]);
        FunctionManager fm = currentProgram.getFunctionManager();
        Set<String> seen = new HashSet<>();
        try (PrintWriter cg = new PrintWriter(new File(root, "extracted/analysis/callgraph.tsv"), "UTF-8");
             PrintWriter dr = new PrintWriter(new File(root, "extracted/analysis/datarefs.tsv"), "UTF-8")) {
            for (Function f : fm.getFunctions(true)) {
                String fname = f.getParentNamespace().isGlobal() ? f.getName() : f.getParentNamespace().getName() + "::" + f.getName();
                AddressSet body = new AddressSet(f.getBody());
                InstructionIterator it = currentProgram.getListing().getInstructions(body, true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (!to.isMemoryAddress()) continue;
                        RefType t = r.getReferenceType();
                        Function callee = fm.getFunctionAt(to);
                        if (t.isCall() && callee != null) {
                            emit(cg, seen, f, fname, callee, "call");
                        } else if (t.isJump() && callee != null && callee != f) {
                            emit(cg, seen, f, fname, callee, "jmp");
                        } else if (t.isData() || t.isRead()) {
                            long off = to.getOffset();
                            if (off < IMG_LO || off >= IMG_HI || (off & 3) != 0) continue;
                            // a literal-pool word: what does it point at?
                            long v;
                            try {
                                v = currentProgram.getMemory().getInt(to) & 0xFFFFFFFFL;
                            } catch (Exception e) { continue; }
                            Address va = to.getNewAddress(v);
                            if (!currentProgram.getMemory().contains(va)) continue;
                            Function c2 = fm.getFunctionAt(va);
                            if (c2 != null && c2 != f) {
                                emit(cg, seen, f, fname, c2, "ptr");
                            } else if (c2 == null) {
                                String l = label(va);
                                if (!l.isEmpty() && !l.startsWith("DAT_") && !l.startsWith("PTR_")) {
                                    String key = "D" + f.getEntryPoint() + l;
                                    if (seen.add(key))
                                        dr.println(fname + "\t" + l + "\t" + Long.toHexString(v));
                                }
                            }
                        }
                    }
                }
            }
        }
        println("ExportCallGraph: " + seen.size() + " distinct edges / data refs");
    }

    void emit(PrintWriter cg, Set<String> seen, Function f, String fname, Function callee, String kind) {
        String key = f.getEntryPoint() + ">" + callee.getEntryPoint() + kind;
        if (!seen.add(key)) return;
        String cn = callee.getParentNamespace().isGlobal() ? callee.getName() : callee.getParentNamespace().getName() + "::" + callee.getName();
        cg.println(Long.toHexString(f.getEntryPoint().getOffset()) + "\t" + fname + "\t" + Long.toHexString(callee.getEntryPoint().getOffset()) + "\t" + cn + "\t" + kind);
    }
}
