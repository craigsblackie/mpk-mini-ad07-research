import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.io.FileWriter;
import java.util.*;

public class MapPeripheralUsers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        LinkedHashMap<Long, String> peripherals = new LinkedHashMap<>();
        peripherals.put(0x40010800L, "GPIOA");
        peripherals.put(0x40010c00L, "GPIOB");
        peripherals.put(0x40011000L, "GPIOC");
        peripherals.put(0x40006000L, "USB_PACKET_MEM");
        peripherals.put(0x40005c00L, "USB");
        peripherals.put(0x40012400L, "ADC1");
        peripherals.put(0x40012800L, "ADC2");
        peripherals.put(0x40021000L, "RCC");
        peripherals.put(0x40010000L, "AFIO");
        peripherals.put(0x40022000L, "FLASH_IF");

        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (Map.Entry<Long, String> e : peripherals.entrySet()) {
                out.println("=== " + e.getValue() + " (0x" + Long.toHexString(e.getKey()) + ") ===");
                TreeSet<String> funcsUsing = new TreeSet<>();
                // scan a window of addresses around the base (registers are offsets from base)
                for (long off = 0; off < 0x400; off += 4) {
                    Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(e.getKey() + off);
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
                    while (refs.hasNext()) {
                        Reference r = refs.next();
                        Address from = r.getFromAddress();
                        Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
                        if (f != null) {
                            funcsUsing.add(f.getName() + " @ " + f.getEntryPoint() + "  (offset +0x" + Long.toHexString(off) + " from base, at " + from + ")");
                        }
                    }
                }
                if (funcsUsing.isEmpty()) {
                    out.println("  (no direct references found)");
                } else {
                    for (String s : funcsUsing) out.println("  " + s);
                }
                out.println();
            }
        }
    }
}
