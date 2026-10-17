import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class DecompFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("USAGE: DecompFuncs.java <startHex> <endHex> <outfile>");
            return;
        }
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(args[0], 16));
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(args[1], 16));
        String outFile = args[2];

        DecompInterface di = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        di.setOptions(opts);
        if (!di.openProgram(currentProgram)) { println("DECOMP_FAIL"); return; }
        ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
        PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(outFile)));

        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int ok = 0, fail = 0;
        while (it.hasNext()) {
            Function f = it.next();
            Address ep = f.getEntryPoint();
            if (ep.compareTo(start) < 0) continue;
            if (ep.compareTo(end) > 0) break;
            // callers
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(ep);
            StringBuilder callers = new StringBuilder();
            int nc = 0;
            while (refs.hasNext()) {
                Reference r = refs.next();
                Function cf = getFunctionContaining(r.getFromAddress());
                if (cf != null) { callers.append(cf.getName()).append("@").append(cf.getEntryPoint()).append(" "); nc++; }
            }
            pw.println("/* ===== " + ep + " " + f.getName() + " callers=" + nc + " " + callers + " ===== */");
            try {
                DecompileResults r = di.decompileFunction(f, 30, mon);
                if (r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    pw.println(r.getDecompiledFunction().getC()); ok++;
                } else { pw.println("/* DECOMPILE_FAILED */"); fail++; }
            } catch (Exception e) { pw.println("/* EXCEPTION " + e + " */"); fail++; }
            pw.println();
        }
        pw.flush(); pw.close();
        di.dispose();
        println("DECOMPFUNCS_DONE ok=" + ok + " fail=" + fail);
    }
}
