import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.RefType;
import java.io.PrintWriter;
import java.io.FileWriter;
import java.util.*;

public class FindWriters extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        long[] targets = {0x2000049aL, 0x2000086eL};
        String[] names = {"iVar5_0x2000049a", "iVar4_0x2000086e"};
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (int t = 0; t < targets.length; t++) {
                out.println("=== references near " + names[t] + " ===");
                for (long off = -4; off < 40; off += 2) {
                    Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(targets[t] + off);
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Address from = r.getFromAddress();
                        Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
                        out.println("  off+" + off + " ref from " + from + " type=" + r.getReferenceType() +
                            (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : ""));
                    }
                }
            }
        }
    }
}
