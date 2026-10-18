import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;
public class Deleg extends GhidraScript {
  public void run() throws Exception {
    // find callers of AirPlayReceiverServerSetDelegate (import) in CPAAProxyEx
    SymbolTable st=currentProgram.getSymbolTable(); FunctionManager fm=currentProgram.getFunctionManager();
    ReferenceManager rm=currentProgram.getReferenceManager();
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    Set<Function> callers=new LinkedHashSet<Function>();
    for(String n: new String[]{"AirPlayReceiverServerSetDelegate","AirPlayReceiverServerCreate"}){
      SymbolIterator si=st.getSymbols(n);
      while(si.hasNext()){ Symbol s=si.next();
        ReferenceIterator it=rm.getReferencesTo(s.getAddress());
        while(it.hasNext()){ Function fn=fm.getFunctionContaining(it.next().getFromAddress());
          if(fn!=null){ callers.add(fn); } } } }
    for(Function fn: callers){
      println("\n===== delegate setup: "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults r=dc.decompileFunction(fn,150,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}
