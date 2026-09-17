import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.io.FileWriter;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        long[] targets = {0x080048f4L, 0x08004990L, 0x08004c44L, 0x08006d54L};
        String[] names = {"matrix_scan", "edge_detector", "tx_pump", "ring_push"};
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 0; i < targets.length; i++) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(targets[i]);
                out.println("=== callers of " + names[i] + " @ " + addr + " ===");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    if (!r.getReferenceType().isCall()) continue;
                    Address from = r.getFromAddress();
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
                    out.println("  called from " + from + (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : " (no containing function)"));
                }
                out.println();
            }
        }
    }
}
