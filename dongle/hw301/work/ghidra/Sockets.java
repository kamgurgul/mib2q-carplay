import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class Sockets extends GhidraScript {
  // For a function, list the absolute data addresses it references (globals).
  void dumpGlobals(long entry, String tag) throws Exception {
    FunctionManager fm=currentProgram.getFunctionManager();
    Function fn=fm.getFunctionAt(toAddr(entry));
    if(fn==null){println(tag+": no func"); return;}
    ReferenceManager rm=currentProgram.getReferenceManager();
    Set<String> globals=new TreeSet<String>();
    AddressSetView body=fn.getBody();
    AddressIterator it=rm.getReferenceSourceIterator(body,true);
    while(it.hasNext()){
      Address src=it.next();
      for(Reference r: rm.getReferencesFrom(src)){
        if(r.getReferenceType().isData()){
          Address to=r.getToAddress();
          if(to!=null && to.isMemoryAddress() && !body.contains(to)) globals.add(to.toString());
        }
      }
    }
    println(tag+" ("+fn.getName()+") data globals: "+globals);
  }
  public void run() throws Exception {
    dumpGlobals(0x5c7ecL, "SETUP_HANDLER");       // FUN_0005c7ec (sets up/ connects socket)
    dumpGlobals(0x5c400L, "SEND_FRAME");          // SendProxyScreenFrame (writes socket)
    dumpGlobals(0x5c19cL, "SEND_CONFIG");         // SendProxyScreenVideoConfigFrame
  }
}
