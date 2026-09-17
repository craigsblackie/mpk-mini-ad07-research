import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.io.FileWriter;
import java.util.*;

public class QuickPeriphCheck extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        LinkedHashMap<Long, String> peripherals = new LinkedHashMap<>();
        peripherals.put(0x40013000L, "SPI1");
        peripherals.put(0x40003800L, "SPI2");
        peripherals.put(0x40005400L, "I2C1");
        peripherals.put(0x40005800L, "I2C2");
        peripherals.put(0x40000c00L, "TIM6");
        peripherals.put(0x40001000L, "TIM7");
        peripherals.put(0xe000e010L, "SysTick");
        peripherals.put(0xe000e100L, "NVIC_ISER");
        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (Map.Entry<Long, String> e : peripherals.entrySet()) {
                out.println("=== " + e.getValue() + " (0x" + Long.toHexString(e.getKey()) + ") ===");
                TreeSet<String> funcsUsing = new TreeSet<>();
                for (long off = 0; off < 0x100; off += 4) {
                    Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(e.getKey() + off);
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                        if (f != null) funcsUsing.add(f.getName() + " @ " + f.getEntryPoint());
                    }
                }
                if (funcsUsing.isEmpty()) out.println("  (none)");
                else for (String s : funcsUsing) out.println("  " + s);
            }
        }
    }
}
