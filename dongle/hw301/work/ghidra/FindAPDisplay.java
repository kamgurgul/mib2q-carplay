import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class FindAPDisplay extends GhidraScript {
  public void run() throws Exception {
    SymbolTable st = currentProgram.getSymbolTable();
    FunctionManager fm = currentProgram.getFunctionManager();
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    // decompile AirPlayInfoArrayAddScreenDisplay + its callers, and the info/displays builder
    String[] names = {"AirPlayInfoArrayAddScreenDisplay"};
    for (String nm : names) {
      SymbolIterator si = st.getSymbols(nm);
      while (si.hasNext()) {
        Symbol s = si.next();
        Function fn = fm.getFunctionAt(s.getAddress());
        if (fn==null) continue;
        // find internal callers
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(s.getAddress());
        println("== callers of "+nm+":");
        Set<Function> callers = new LinkedHashSet<Function>();
        while (it.hasNext()) {
          Reference r = it.next(); Function c = fm.getFunctionContaining(r.getFromAddress());
          if (c!=null){ println("   <- "+c.getName()+"@"+c.getEntryPoint()); callers.add(c);}
        }
        for (Function c : callers) {
          println("\n===== CALLER "+c.getName()+" @ "+c.getEntryPoint()+" =====");
          DecompileResults res = di.decompileFunction(c, 120, new ConsoleTaskMonitor());
          if (res!=null && res.getDecompiledFunction()!=null) println(res.getDecompiledFunction().getC());
        }
      }
    }
  }
}
