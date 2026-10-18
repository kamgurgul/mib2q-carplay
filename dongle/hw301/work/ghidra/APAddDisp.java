import ghidra.app.script.GhidraScript; import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*; import ghidra.util.task.ConsoleTaskMonitor;
public class APAddDisp extends GhidraScript { public void run() throws Exception {
  FunctionManager fm=currentProgram.getFunctionManager();
  DecompInterface dc=new DecompInterface(); dc.openProgram(currentProgram);
  for (Function f: fm.getFunctions(true)) {
    String n=f.getName();
    if (n.equals("AirPlayInfoArrayAddScreenDisplay")||n.contains("AddScreenDisplay")) {
      println("\n==== "+n+" @ "+f.getEntryPoint()+" params="+f.getParameterCount()+" ====");
      DecompileResults r=dc.decompileFunction(f,120,new ConsoleTaskMonitor());
      if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC());
    }
  }
}}
