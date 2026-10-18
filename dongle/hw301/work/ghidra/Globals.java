import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;

public class Globals extends GhidraScript {
  void refsAt(long lo,long hi,String tag){
    ReferenceManager rm=currentProgram.getReferenceManager();
    FunctionManager fm=currentProgram.getFunctionManager();
    for(long a=lo;a<hi;a+=2){
      Address ad=toAddr(a);
      for(Reference r: rm.getReferencesFrom(ad)){
        if(r.getReferenceType().isData()){
          Address to=r.getToAddress();
          if(to!=null&&to.isMemoryAddress()&&(to.getOffset()<lo||to.getOffset()>hi))
            println(tag+" "+ad+" -> DATA "+to+" ("+r.getReferenceType()+")");
        }
      }
    }
  }
  public void run() throws Exception {
    println("=== SendProxyScreenFrame socket-fd + writen area (0x5c400-0x5c470):");
    refsAt(0x5c400L,0x5c470L,"SPF");
    println("=== FUN_0005c7ec socket store area (0x5cab0-0x5cb10):");
    refsAt(0x5cab0L,0x5cb10L,"SETUP");
    // memory blocks to find spare .bss
    println("=== memory blocks:");
    for(MemoryBlock b: currentProgram.getMemory().getBlocks())
      println(String.format("  %-14s %s-%s w=%b init=%b", b.getName(), b.getStart(), b.getEnd(), b.isWrite(), b.isInitialized()));
  }
}
