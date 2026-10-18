import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.*;
import ghidra.app.decompiler.*;
import ghidra.util.task.ConsoleTaskMonitor;

/* Vet candidate caves: for each, print ISA (ARM/Thumb via TMode register),
 * first few instructions (bytes), and a short decompile so we can tell what the
 * dead function is (musl/crypto/unwind?) before overwriting it. */
public class CaveCheck extends GhidraScript {
  public void run() throws Exception {
    long[] cands = { 0x000acdb0L, 0x0002f5c4L, 0x0006d350L, 0x00079ff8L };
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    Listing lst = currentProgram.getListing();
    Register tmode = currentProgram.getLanguage().getRegister("TMode");
    DecompInterface dc = new DecompInterface(); dc.openProgram(currentProgram);
    FunctionManager fm = currentProgram.getFunctionManager();
    for (long c : cands) {
      Address ep = sp.getAddress(c);
      println("\n===== FUN_" + Long.toHexString(c) + " =====");
      if (tmode != null) {
        try { ghidra.program.model.lang.RegisterValue rv =
          currentProgram.getProgramContext().getRegisterValue(tmode, ep);
          println("  TMode(Thumb)=" + (rv!=null?rv.getUnsignedValue():"?")); } catch (Exception e) {}
      }
      Instruction ins = lst.getInstructionAt(ep);
      for (int i=0; i<6 && ins!=null; i++) {
        byte[] b = ins.getBytes(); StringBuilder h=new StringBuilder();
        for (byte x:b) h.append(String.format("%02x",x&0xff));
        println(String.format("  %08x: %-10s %s", ins.getAddress().getOffset(), h, ins.toString()));
        ins = ins.getNext();
      }
      Function f = fm.getFunctionAt(ep);
      if (f != null) {
        DecompileResults r = dc.decompileFunction(f, 60, new ConsoleTaskMonitor());
        if (r!=null && r.getDecompiledFunction()!=null) {
          String csrc = r.getDecompiledFunction().getC();
          println("  --- decompile (first 900 chars) ---");
          println(csrc.length()>900 ? csrc.substring(0,900) : csrc);
        }
      }
    }
  }
}
