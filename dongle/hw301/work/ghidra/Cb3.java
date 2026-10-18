import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Cb3 extends GhidraScript {
  public void run() throws Exception {
    Function fn=currentProgram.getFunctionManager().getFunctionContaining(toAddr(0x5a068L));
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    DecompileResults r=dc.decompileFunction(fn,240,new ConsoleTaskMonitor());
    println(r.getDecompiledFunction().getC());
  }
}
