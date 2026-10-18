import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;
public class DispParse extends GhidraScript {
  public void run() throws Exception {
    Memory m=currentProgram.getMemory();
    byte[] pat="Recv proxy disply infos".getBytes();
    Address s=m.findBytes(currentProgram.getMinAddress(),pat,null,true,monitor);
    println("string @ "+s);
    FunctionManager fm=currentProgram.getFunctionManager();
    ReferenceManager rm=currentProgram.getReferenceManager();
    Set<Function> fns=new LinkedHashSet<Function>();
    if(s!=null){ ReferenceIterator it=rm.getReferencesTo(s);
      while(it.hasNext()){ Function fn=fm.getFunctionContaining(it.next().getFromAddress()); if(fn!=null) fns.add(fn);} }
    // also the viewareas string
    byte[] p2="Recv proxy disply viewarea".getBytes();
    Address s2=m.findBytes(currentProgram.getMinAddress(),p2,null,true,monitor);
    if(s2!=null){ ReferenceIterator it=rm.getReferencesTo(s2);
      while(it.hasNext()){ Function fn=fm.getFunctionContaining(it.next().getFromAddress()); if(fn!=null) fns.add(fn);} }
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    for(Function fn:fns){ println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults r=dc.decompileFunction(fn,200,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC()); }
    if(fns.isEmpty()) println("no refs found");
  }
}
