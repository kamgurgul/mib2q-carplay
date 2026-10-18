import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

/* Design data for the 2nd head-unit send-socket patch:
 *  (1) disassemble FUN_0005c7ec across the type-gate (cmp #0x6e) and the socket
 *      store (str r8,[r12] -> 0x17e418), with raw bytes + register operands, so
 *      we can see which register still holds the stream type at the store site.
 *  (2) scan the executable region for a code cave (a run of >= 48 bytes of 0x00
 *      after the last real instruction / between functions) to host the detour.
 *  (3) report the BSS/data neighbourhood of 0x17e418 for a free 2nd-slot word. */
public class DualSock extends GhidraScript {
  public void run() throws Exception {
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    Listing lst = currentProgram.getListing();
    Memory mem = currentProgram.getMemory();

    println("===== FUN_0005c7ec disasm 0x5c9a0..0x5cb20 (bytes + ops) =====");
    Address a = sp.getAddress(0x5c9a0L);
    Address end = sp.getAddress(0x5cb20L);
    while (a.compareTo(end) < 0) {
      Instruction ins = lst.getInstructionAt(a);
      if (ins == null) { a = a.add(2); continue; }
      byte[] b = ins.getBytes();
      StringBuilder hex = new StringBuilder();
      for (byte x : b) hex.append(String.format("%02x", x & 0xff));
      println(String.format("%08x: %-10s %s", a.getOffset(), hex.toString(), ins.toString()));
      a = a.add(ins.getLength());
    }

    println("\n===== code caves: >=48 zero bytes in executable blocks =====");
    for (MemoryBlock blk : mem.getBlocks()) {
      if (!blk.isExecute()) continue;
      long start = blk.getStart().getOffset(), stop = blk.getEnd().getOffset();
      long runStart = -1; int run = 0;
      for (long off = start; off <= stop; off++) {
        int v;
        try { v = mem.getByte(sp.getAddress(off)) & 0xff; } catch (Exception e) { v = 1; }
        if (v == 0) { if (run == 0) runStart = off; run++; }
        else { if (run >= 48) println(String.format("  cave %08x..%08x  (%d bytes) in %s",
                 runStart, off-1, run, blk.getName())); run = 0; }
      }
      if (run >= 48) println(String.format("  cave %08x..%08x  (%d bytes) in %s",
               runStart, stop, run, blk.getName()));
    }

    println("\n===== neighbourhood of socket slot 0x17e418 (look for free words) =====");
    for (long off = 0x17e400L; off <= 0x17e440L; off += 4) {
      int w = mem.getInt(sp.getAddress(off));
      println(String.format("  %08x: %08x", off, w));
    }
  }
}
