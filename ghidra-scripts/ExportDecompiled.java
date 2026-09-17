import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.PrintWriter;
import java.io.FileWriter;

public class ExportDecompiled extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp/decompiled.c";
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setOptions(new ghidra.app.decompiler.DecompileOptions());

        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            FunctionIterator funcs = currentProgram.getFunctionManager().getFunctions(true);
            int count = 0;
            for (Function f : funcs) {
                if (f.isThunk() || f.isExternal()) continue;
                out.println("// ==================================================");
                out.println("// Function: " + f.getName() + " @ " + f.getEntryPoint());
                out.println("// ==================================================");
                DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("// DECOMPILE FAILED: " + (res != null ? res.getErrorMessage() : "null result"));
                }
                out.println();
                count++;
            }
            out.println("// Total functions: " + count);
        }
        decomp.dispose();
    }
}
