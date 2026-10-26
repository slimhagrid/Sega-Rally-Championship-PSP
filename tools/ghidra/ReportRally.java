// Post-script: after auto-analysis, write extracted/analysis/functions.tsv (addr, size, name, module) and a short summary.
//@category Rally
import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Namespace;

public class ReportRally extends GhidraScript {
    @Override
    public void run() throws Exception {
        File root = new File(getScriptArgs().length > 0 ? getScriptArgs()[0] : ".");
        File out = new File(root, "extracted/analysis/functions.tsv");
        int total = 0, named = 0;
        long bytes = 0;
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                total++;
                String name = f.getName();
                boolean auto = name.startsWith("FUN_");
                if (!auto) named++;
                long size = f.getBody().getNumAddresses();
                bytes += size;
                Namespace ns = f.getParentNamespace();
                String mod = ns.isGlobal() ? "" : ns.getName();
                w.println(Long.toHexString(f.getEntryPoint().getOffset()) + "\t" + size + "\t" + name + "\t" + mod);
            }
        }
        println("ReportRally: " + total + " functions (" + named + " carry a DBG name, " + (total - named) + " auto-named), " + bytes + " bytes of function bodies -> " + out);
    }
}
