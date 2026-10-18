import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Cb extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager();
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    for(long a: new long[]{0x5a068L}){
      Function fn=fm.getFunctionContaining(toAddr(a));
      if(fn==null){ println("no fn @"+Long.toHexString(a)); continue; }
      println("===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults r=dc.decompileFunction(fn,200,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}
