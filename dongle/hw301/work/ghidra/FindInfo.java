import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class FindInfo extends GhidraScript {
  public void run() throws Exception {
    String[] wanted = {"displays","widthPixels","heightPixels","displayUUID","features","InstrumentCluster","viewAreas","deviceID"};
    FunctionManager fm = currentProgram.getFunctionManager();
    ReferenceManager rm = currentProgram.getReferenceManager();
    Listing lst = currentProgram.getListing();
    Memory mem = currentProgram.getMemory();
    Set<Function> funcs = new LinkedHashSet<Function>();
    // scan defined strings
    DataIterator di2 = lst.getDefinedData(true);
    while (di2.hasNext()) {
      Data d = di2.next();
      if (d.hasStringValue()) {
        String v = d.getValue().toString();
        for (String w : wanted) {
          if (v.equals(w)) {
            Address a = d.getAddress();
            ReferenceIterator it = rm.getReferencesTo(a);
            while (it.hasNext()) {
              Function fn = fm.getFunctionContaining(it.next().getFromAddress());
              if (fn!=null){ println("string '"+w+"' @"+a+" <- "+fn.getName()+"@"+fn.getEntryPoint()); funcs.add(fn);} 
            }
          }
        }
      }
    }
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    for (Function fn : funcs) {
      println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults res = di.decompileFunction(fn, 150, new ConsoleTaskMonitor());
      if (res!=null && res.getDecompiledFunction()!=null) println(res.getDecompiledFunction().getC());
    }
    if (funcs.isEmpty()) println("(no refs found to display strings in libAirPlay)");
  }
}
