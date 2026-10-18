import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Plat extends GhidraScript {
  public void run() throws Exception {
    String[] names={"AirPlayReceiverServerPlatformCopyProperty","AirPlayReceiverSessionPlatformCopyProperty",
                    "_AirPlayReceiverServerPlatformCopyProperty"};
    SymbolTable st=currentProgram.getSymbolTable(); FunctionManager fm=currentProgram.getFunctionManager();
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    for(String n:names){ SymbolIterator si=st.getSymbols(n); boolean f=false;
      while(si.hasNext()){ Symbol s=si.next(); Function fn=fm.getFunctionAt(s.getAddress()); if(fn==null)continue; f=true;
        println("\n===== "+n+" @ "+fn.getEntryPoint()+" =====");
        DecompileResults r=dc.decompileFunction(fn,150,new ConsoleTaskMonitor());
        if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC()); }
      if(!f) println("("+n+" not found as symbol)"); }
  }
}
