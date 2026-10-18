import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Deleg2 extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager();
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    for(long a: new long[]{0x59250L,0x58e38L}){
      Function fn=fm.getFunctionContaining(toAddr(a));
      if(fn==null){ println("no fn @"+Long.toHexString(a)); continue; }
      println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" (contains "+Long.toHexString(a)+") =====");
      DecompileResults r=dc.decompileFunction(fn,180,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}
