import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
public class Recv extends GhidraScript {
  public void run() throws Exception {
    String[] names = {
      "_ZN15CarPlayProxyApp21recvCarPlayVideoFrameEPvjb",
      "_ZN15CarPlayProxyApp22writeCarLifeVideoFrameEPKhji",
      "_ZN15CarPlayProxyApp15handleVideoInitEN3gal11DisplayTypeENS0_14MediaCodecTypeEiiRNS0_6InsetsE",
      "_ZN15CarPlayProxyApp16handleVideoStartEN3gal11DisplayTypeE"};
    SymbolTable st=currentProgram.getSymbolTable(); FunctionManager fm=currentProgram.getFunctionManager();
    DecompInterface di=new DecompInterface(); di.openProgram(currentProgram);
    for(String nm:names){ SymbolIterator si=st.getSymbols(nm);
      while(si.hasNext()){ Symbol s=si.next(); Function fn=fm.getFunctionAt(s.getAddress()); if(fn==null)continue;
        println("\n===== "+nm+" @ "+fn.getEntryPoint()+" =====");
        DecompileResults r=di.decompileFunction(fn,150,new ConsoleTaskMonitor());
        if(r!=null&&r.getDecompiledFunction()!=null) println(r.getDecompiledFunction().getC()); } }
  }
}
