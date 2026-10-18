import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class APInfo extends GhidraScript {
  public void run() throws Exception {
    String[] want = {"displays","display","widthPixels","heightPixels","features","clusate","cluster",
                     "primaryInputDevice","maxFPS","/info","transportType","AirPlayScreenDisplay",
                     "screenDisplay","deviceID","displayUUID","uuid"};
    Set<String> ws = new HashSet<String>(Arrays.asList(want));
    FunctionManager fm = currentProgram.getFunctionManager();
    ReferenceManager rm = currentProgram.getReferenceManager();
    Listing lst = currentProgram.getListing();
    Map<Function,Set<String>> hits = new LinkedHashMap<Function,Set<String>>();
    DataIterator di = lst.getDefinedData(true);
    while (di.hasNext()) {
      Data d = di.next();
      if (!d.hasStringValue()) continue;
      String v = d.getValue().toString();
      if (!ws.contains(v)) continue;
      ReferenceIterator it = rm.getReferencesTo(d.getAddress());
      while (it.hasNext()) {
        Function fn = fm.getFunctionContaining(it.next().getFromAddress());
        if (fn==null) continue;
        if (!hits.containsKey(fn)) hits.put(fn,new TreeSet<String>());
        hits.get(fn).add(v);
      }
    }
    // rank by number of distinct display-ish strings referenced
    List<Function> fns = new ArrayList<Function>(hits.keySet());
    fns.sort((a,b)->hits.get(b).size()-hits.get(a).size());
    for (Function fn: fns) println(fn.getName()+"@"+fn.getEntryPoint()+"  strings="+hits.get(fn));
    // decompile the top 2
    DecompInterface dc = new DecompInterface(); dc.openProgram(currentProgram);
    int k=0;
    for (Function fn: fns){ if(k++>=2)break;
      println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults r=dc.decompileFunction(fn,150,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}
