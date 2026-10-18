import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Cb2 extends GhidraScript {
  public void run() throws Exception {
    Function fn=currentProgram.getFunctionManager().getFunctionContaining(toAddr(0x5a068L));
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    DecompileResults r=dc.decompileFunction(fn,220,new ConsoleTaskMonitor());
    String c=r.getDecompiledFunction().getC();
    // print from the 'displays'/widthPixels handling onward (skip the audio part already seen)
    String[] lines=c.split("\n");
    int n=0; boolean on=false;
    for(String L: lines){ n++;
      if(L.contains("0x5a5")||L.contains("widthPixels")||L.contains("0xbc")||L.contains("0xc0")||n>150) on=true;
      if(on) println(L);
    }
  }
}
