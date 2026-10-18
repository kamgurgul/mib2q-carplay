import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

public class FindDisp2 extends GhidraScript {
  Address findStr(String s) throws Exception {
    Memory m=currentProgram.getMemory();
    byte[] pat=(s+"\0").getBytes();
    Address a=m.findBytes(currentProgram.getMinAddress(), pat, null, true, monitor);
    return a;
  }
  public void run() throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager();
    ReferenceManager rm=currentProgram.getReferenceManager();
    String[] strs={"displays","_AirPlayClientSendInfoCommandCompletion","instrumentCluster","InstrumentCluster","clusterInfo","AirPlayScreenInfoKey_InstrumentCluster"};
    Set<Function> funcs=new LinkedHashSet<Function>();
    for(String s:strs){
      Address a=findStr(s);
      if(a==null){ println("str '"+s+"' not found"); continue; }
      int n=0; ReferenceIterator it=rm.getReferencesTo(a);
      while(it.hasNext()){ Function fn=fm.getFunctionContaining(it.next().getFromAddress());
        if(fn!=null){ funcs.add(fn); n++; } }
      println("str '"+s+"' @"+a+" refs="+n);
    }
    DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
    for(Function fn:funcs){
      println("\n===== "+fn.getName()+" @ "+fn.getEntryPoint()+" =====");
      DecompileResults r=dc.decompileFunction(fn,150,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}
