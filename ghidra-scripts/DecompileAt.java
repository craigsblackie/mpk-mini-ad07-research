import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.io.FileWriter;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            decomp.setOptions(new ghidra.app.decompiler.DecompileOptions());
            for (int i = 1; i < args.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[i]);
                Function f = currentProgram.getFunctionManager().getFunctionContaining(addr);
                if (f == null) {
                    out.println("No function at " + addr + " -- creating one");
                    disassemble(addr);
                    createFunction(addr, null);
                    f = currentProgram.getFunctionManager().getFunctionContaining(addr);
                }
                out.println("=== " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : addr.toString()) + " ===");
                if (f != null) {
                    DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                    if (res != null && res.decompileCompleted()) {
                        out.println(res.getDecompiledFunction().getC());
                    } else {
                        out.println("DECOMPILE FAILED: " + (res != null ? res.getErrorMessage() : "null"));
                    }
                }
                out.println();
            }
            decomp.dispose();
        }
    }
}
