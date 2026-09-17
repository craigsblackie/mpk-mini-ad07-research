import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindStateRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        long[] targets = {
            0x20000026L, 0x20000030L, 0x20000031L, 0x20000032L,
            0x20000060L, 0x20000061L, 0x20000062L, 0x20000068L,
            0x20000069L, 0x2000006aL, 0x200004baL, 0x20000740L
        };
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (long target : targets) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target);
                out.println("=== " + addr + " ===");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                    out.println(ref.getFromAddress() + " " + ref.getReferenceType() +
                        (f == null ? "" : " " + f.getName() + " @ " + f.getEntryPoint()));
                }
            }
        }
    }
}
