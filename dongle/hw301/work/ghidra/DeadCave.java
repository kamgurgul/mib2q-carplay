import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/* Find candidate code caves: functions whose entry point has ZERO references of
 * any kind (no call, no data/pointer ref) -> never reached by call or function
 * pointer as far as static analysis can see. Big ones can host the dual-pipeline
 * detour. Report sorted by size. Also print the .text/exec extent for the
 * new-segment fallback. NOTE: zero static xrefs is necessary but not sufficient
 * proof of deadness; shortlist must still be sanity-checked before overwriting. */
public class DeadCave extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm = currentProgram.getFunctionManager();
    ReferenceManager rm = currentProgram.getReferenceManager();
    SymbolTable st = currentProgram.getSymbolTable();

    class Cand { String name; Address ep; long size; Cand(String n, Address e, long s){name=n;ep=e;size=s;} }
    List<Cand> dead = new ArrayList<>();
    int total = 0;
    for (Function f : fm.getFunctions(true)) {
      total++;
      Address ep = f.getEntryPoint();
      // count ALL references to the entry point
      int refs = 0;
      ReferenceIterator it = rm.getReferencesTo(ep);
      while (it.hasNext()) { it.next(); refs++; }
      if (refs != 0) continue;
      // also skip anything that is an exported/global symbol (could be called externally)
      Symbol s = st.getPrimarySymbol(ep);
      boolean exported = (s != null && s.isExternalEntryPoint());
      if (exported) continue;
      long size = f.getBody().getNumAddresses();
      dead.add(new Cand(f.getName(), ep, size));
    }
    dead.sort((a,b) -> Long.compare(b.size, a.size));
    println("total functions: " + total + " ; zero-xref candidates: " + dead.size());
    println("=== top 40 zero-xref functions (potential caves, biggest first) ===");
    int n = 0;
    for (Cand c : dead) {
      println(String.format("  %08x  %6d B  %s", c.ep.getOffset(), c.size, c.name));
      if (++n >= 40) break;
    }
    // exec extent
    println("\n=== executable blocks (for new-segment fallback) ===");
    for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks()) {
      if (b.isExecute())
        println(String.format("  %s  %08x..%08x  (%d bytes)", b.getName(),
          b.getStart().getOffset(), b.getEnd().getOffset(), b.getSize()));
    }
  }
}
