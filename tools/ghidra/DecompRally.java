// Decompile named functions to extracted/analysis/decomp/<module>__<name>.c.  Script args: <project root> <name | 0xADDR> ...  (or "ALL" for every function that has a DBG name, "EVERY" for all functions; 0xADDR also reaches FUN_ functions)
//@category Rally
import java.io.File;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class DecompRally extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File root = new File(args[0]);
        Set<String> want = new HashSet<>();
        for (int i = 1; i < args.length; i++) want.add(args[i]);
        boolean all = want.contains("ALL");
        boolean every = want.contains("EVERY");        // every function, FUN_ ones too
        File dir = new File(root, "extracted/analysis/decomp");
        dir.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int n = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            String name = f.getName();
            String hex = "0x" + Long.toHexString(f.getEntryPoint().getOffset());
            boolean byAddr = want.contains(hex) || want.contains(hex.toUpperCase().replace("0X", "0x"));
            if (!byAddr) {
                if (!every) {
                    if (name.startsWith("FUN_")) continue;
                    if (!all && !want.contains(name)) continue;
                }
            }
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            String mod = f.getParentNamespace().isGlobal() ? "" : f.getParentNamespace().getName();
            File out = new File(dir, mod + "__" + name + ".c");
            try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
                w.println("// " + mod + "::" + name + " @ " + f.getEntryPoint() + " (" + f.getBody().getNumAddresses() + " bytes)");
                if (r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
                else w.println("// decompile failed: " + r.getErrorMessage());
            }
            n++;
        }
        println("DecompRally: wrote " + n + " functions to " + dir);
    }
}
