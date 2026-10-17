import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.address.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FindScalarRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outFile = "/root/ports/godhand/extract/ghidra_scalar_refs.txt";
        PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(outFile)));

        // magic byte patterns we care about (asset container tags)
        String[][] magics = {
            {"SNR2", "534e5232"},
            {"SES\\0", "53455300"},
            {"TM3\\0", "544d3300"},
            {"T32\\0", "54333200"},
            {"MPD\\0", "4d504400"},
            {"TBL\\0", "54424c00"},
            {"ICO\\0", "49434f00"},
        };

        // 1) locate each magic in memory, report refs to it
        pw.println("=== MAGIC BYTES IN MEMORY + REFERENCES ===");
        for (String[] m : magics) {
            byte[] pat = hex(m[1]);
            Memory mem = currentProgram.getMemory();
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isInitialized()) continue;
                AddressSpace sp = b.getStart().getAddressSpace();
                Address cur = b.getStart();
                while (true) {
                    Address hit = mem.findBytes(cur, b.getEnd(), pat, null, true, monitor);
                    if (hit == null) break;
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(hit);
                    int rc = 0;
                    List<Reference> rlist = new ArrayList<Reference>();
                    while (refs.hasNext()) { Reference r = refs.next(); rlist.add(r); rc++; }
                    pw.println(m[0] + " at " + hit + " block=" + b.getName() + " refs=" + rc);
                    for (Reference r : rlist) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        pw.println("    REF from " + r.getFromAddress() +
                            (f != null ? " in " + f.getName() + " " + f.getEntryPoint() : " (no func)"));
                    }
                    Address next = hit.add(1);
                    if (next.compareTo(b.getEnd()) > 0) break;
                    cur = next;
                }
            }
        }

        // 2) scalar operand scan: distinctive constants (full 32-bit + low16 halves)
        Set<Long> want = new HashSet<Long>();
        long[] full = {
            0x32524E53L, 0x534E5232L,   // SNR2 le/be
            0x00534553L, 0x53455300L,   // SES\0 le/be
            0x00334D54L, 0x544D3300L,   // TM3\0
            0x00323354L, 0x54333200L,   // T32\0
            0x0044504DL, 0x4D504400L,   // MPD\0
            0x004C4254L, 0x54424C00L,   // TBL\0
            0x00000F9EL, 0x0F9E0000L,   // type 0x9e0f container
            0x00000025L,                // type 0x25
        };
        for (long v : full) want.add(v);
        long[] low16 = {0x4E53, 0x534E, 0x4553, 0x5345, 0x334D, 0x4D33, 0x3233, 0x5433,
                        0x4450, 0x5044, 0x4C42, 0x424C, 0x0F9E, 0x9E0F};
        for (long v : low16) want.add(v);

        pw.println();
        pw.println("=== SCALAR OPERAND HITS ===");
        int hits = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            int n = ins.getNumOperands();
            for (int i = 0; i < n; i++) {
                Object[] ops = ins.getOpObjects(i);
                for (Object o : ops) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (want.contains(v)) {
                            Function f = getFunctionContaining(ins.getAddress());
                            pw.println(ins.getAddress() + " " + ins.toString() +
                                " val=0x" + Long.toHexString(v).toUpperCase() +
                                (f != null ? "  func=" + f.getName() + " " + f.getEntryPoint() : ""));
                            hits++;
                        }
                    }
                }
            }
        }
        pw.println("TOTAL_SCALAR_HITS=" + hits);
        pw.flush();
        pw.close();
        println("FINDSCALAR_DONE hits=" + hits);
    }

    private static byte[] hex(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++)
            b[i] = (byte) Integer.parseInt(h.substring(i * 2, i * 2 + 2), 16);
        return b;
    }
}
