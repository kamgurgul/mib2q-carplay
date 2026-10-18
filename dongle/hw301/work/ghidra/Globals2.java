import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.Memory;
import java.util.*;

/* Resolve the single-screen globals to absolute (link-time) addresses so the
 * dual pipeline can allocate a parallel slot set and target the patch sites.
 * For each target function, list every data reference (to-address) and the
 * instruction that makes it, plus dump raw bytes around the socket-store site. */
public class Globals2 extends GhidraScript {
  long readWord(long at) throws Exception {
    Memory m = currentProgram.getMemory();
    Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(at);
    return m.getInt(a) & 0xffffffffL;
  }
  void dumpFunc(long entry) throws Exception {
    FunctionManager fm = currentProgram.getFunctionManager();
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    Function f = fm.getFunctionAt(sp.getAddress(entry));
    if (f == null) { println("no func @ " + Long.toHexString(entry)); return; }
    println("\n===== " + f.getName() + " @ " + f.getEntryPoint() + " data refs =====");
    Listing lst = currentProgram.getListing();
    InstructionIterator it = lst.getInstructions(f.getBody(), true);
    ReferenceManager rm = currentProgram.getReferenceManager();
    while (it.hasNext()) {
      Instruction ins = it.next();
      for (Reference r : ins.getReferencesFrom()) {
        if (r.getReferenceType().isData()) {
          Address to = r.getToAddress();
          String val = "";
          try { val = " word@=0x" + Long.toHexString(readWord(to.getOffset())); } catch (Exception e) {}
          println(ins.getAddress() + ": " + ins.toString()
              + "  -> " + to + val);
        }
      }
    }
  }
  public void run() throws Exception {
    long[] funcs = { 0x5c7ecL, 0x5c400L, 0x5c19cL, 0x5b7c4L };
    for (long e : funcs) dumpFunc(e);

    // raw bytes around the socket-store in FUN_0005c7ec (store insn ~0x5caf4-0x5cb0c)
    println("\n===== raw bytes 0x5cad0..0x5cb30 (socket-store region) =====");
    Memory m = currentProgram.getMemory();
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    for (long a = 0x5cad0L; a < 0x5cb30L; a += 4) {
      int w = m.getInt(sp.getAddress(a));
      println(String.format("%08x: %08x", a, w));
    }
    // the type-gate compare in FUN_0005c7ec is near 0x5c9?? per notes (cmp #0x6e)
    println("\n===== scan FUN_0005c7ec for cmp #0x6e / #0x6f =====");
    Listing lst = currentProgram.getListing();
    FunctionManager fm = currentProgram.getFunctionManager();
    Function f = fm.getFunctionAt(sp.getAddress(0x5c7ecL));
    if (f != null) {
      InstructionIterator it = lst.getInstructions(f.getBody(), true);
      while (it.hasNext()) {
        Instruction ins = it.next();
        String s = ins.toString();
        if (s.contains("0x6e") || s.contains("0x6f") || s.contains("#110") || s.contains("#111")
            || s.toLowerCase().contains("cmp")) {
          println(ins.getAddress() + ": " + s);
        }
      }
    }
  }
}
