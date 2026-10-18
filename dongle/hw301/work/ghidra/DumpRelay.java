import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.util.*;

/* Decompile the single-screen video relay + crypto pipeline so the 2nd-screen
 * shim can be designed. Targets by name (exported) and by the global offsets the
 * notes recorded (0x5c430 head-unit socket, 0x5c428/0x5c514 crypto ctx). */
public class DumpRelay extends GhidraScript {
  public void run() throws Exception {
    FunctionManager fm = currentProgram.getFunctionManager();
    String[] names = {
      "SendProxyScreenFrame", "SendProxyScreenVideoConfigFrame",
      "SetupProxyScreenSecurityInfo", "SetupProxyScreen",
      "recvCarPlayVideoFrame", "writeCarLifeVideoFrame", "handleVideoInit",
      "SocketConnect", "TeardownProxyScreen", "writen"
    };
    Set<Function> want = new LinkedHashSet<>();
    for (String n : names) {
      for (Function f : fm.getFunctions(true)) {
        if (f.getName().equals(n)) { want.add(f); }
      }
    }
    // also dump the known SETUP handler the notes patched
    try {
      Function f = fm.getFunctionAt(currentProgram.getAddressFactory()
          .getDefaultAddressSpace().getAddress(0x5c7ec));
      if (f != null) want.add(f);
    } catch (Exception e) {}

    DecompInterface dc = new DecompInterface();
    dc.openProgram(currentProgram);
    for (Function f : want) {
      println("\n===== " + f.getName() + " @ " + f.getEntryPoint() + " =====");
      DecompileResults r = dc.decompileFunction(f, 200, new ConsoleTaskMonitor());
      if (r != null && r.getDecompiledFunction() != null)
        println(r.getDecompiledFunction().getC());
      else
        println("(decompile failed)");
    }
    println("\n===== names seen (sanity) =====");
    int shown = 0;
    for (Function f : fm.getFunctions(true)) {
      String n = f.getName();
      if (n.contains("ProxyScreen") || n.contains("VideoFrame") || n.contains("CarLife")
          || n.contains("Security") || n.contains("VideoInit")) {
        println(n + " @ " + f.getEntryPoint());
        if (++shown > 60) break;
      }
    }
  }
}
