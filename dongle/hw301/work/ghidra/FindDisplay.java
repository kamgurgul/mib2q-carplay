import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class FindDisplay extends GhidraScript {
  public void run() throws Exception {
    // display-dict key strings (v129): cluster A @0x150ee0, cluster B @0x156188
    long[] targets = {0x150ee0L,0x156188L,0x1561dcL,0x150f94L,0x1515f8L,0x151050L,0x156214L};
    FunctionManager fm = currentProgram.getFunctionManager();
    ReferenceManager rm = currentProgram.getReferenceManager();
    Set<Function> funcs = new LinkedHashSet<Function>();
    for (long t : targets) {
      Address a = toAddr(t);
      ReferenceIterator it = rm.getReferencesTo(a);
      int n=0;
      while (it.hasNext()) {
        Reference r = it.next(); Address fr = r.getFromAddress();
        Function fn = fm.getFunctionContaining(fr);
        println(String.format("REF 0x%x <- %s func=%s", t, fr.toString(),
            fn!=null? fn.getName()+"@"+fn.getEntryPoint().toString():"?"));
        if (fn!=null) funcs.add(fn); n++;
      }
      if (n==0) println(String.format("REF 0x%x <- none", t));
    }
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    for (Function fn : funcs) {
      println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults res = di.decompileFunction(fn, 120, new ConsoleTaskMonitor());
      if (res!=null && res.getDecompiledFunction()!=null) println(res.getDecompiledFunction().getC());
    }
  }
}
