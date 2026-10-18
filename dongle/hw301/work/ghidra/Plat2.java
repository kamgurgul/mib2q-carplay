import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Plat2 extends GhidraScript {
  void dec(long a){ FunctionManager fm=currentProgram.getFunctionManager();
    Function fn=fm.getFunctionAt(toAddr(a)); if(fn==null){println("no fn@"+Long.toHexString(a));return;}
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
    DecompileResults r=dc.decompileFunction(fn,150,new ConsoleTaskMonitor());
    if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC()); }
  public void run() throws Exception { dec(0x9378L); }
}
