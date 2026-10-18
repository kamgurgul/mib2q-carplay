import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;

public class Pipeline extends GhidraScript {
  public void run() throws Exception {
    String[] names = {
      "SetupProxyScreenEx","SendProxyScreenVideoConfigFrame","SendProxyScreenFrame",
      "_ZN15CarPlayProxyApp21recvCarPlayVideoFrameEPvjb",
      "_ZN15CarPlayProxyApp22writeCarLifeVideoFrameEPKhji",
      "_ZN15CarPlayProxyApp15handleVideoInitEN3gal11DisplayTypeENS0_14MediaCodecTypeEiiRNS0_6InsetsE",
      "TearDownProxyScreen","ProxyScreenUninit"
    };
    SymbolTable st = currentProgram.getSymbolTable();
    FunctionManager fm = currentProgram.getFunctionManager();
    DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
    for (String nm : names) {
      SymbolIterator si = st.getSymbols(nm);
      boolean any=false;
      while (si.hasNext()) {
        Symbol s = si.next(); Function fn = fm.getFunctionAt(s.getAddress());
        if (fn==null) continue; any=true;
        println("\n===== "+nm+" @ "+fn.getEntryPoint()+" =====");
        DecompileResults res = di.decompileFunction(fn, 150, new ConsoleTaskMonitor());
        if (res!=null && res.getDecompiledFunction()!=null) println(res.getDecompiledFunction().getC());
      }
      if(!any) println("\n("+nm+" not found)");
    }
  }
}
