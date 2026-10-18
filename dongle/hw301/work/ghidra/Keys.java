import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
public class Keys extends GhidraScript {
  public void run() throws Exception {
    Function fn=currentProgram.getFunctionManager().getFunctionContaining(toAddr(0x5a068L));
    ReferenceManager rm=currentProgram.getReferenceManager();
    Listing lst=currentProgram.getListing();
    AddressSetView body=fn.getBody();
    AddressIterator it=rm.getReferenceSourceIterator(body,true);
    java.util.Set<String> seen=new java.util.LinkedHashSet<String>();
    while(it.hasNext()){
      Address src=it.next();
      for(Reference r: rm.getReferencesFrom(src)){
        Address to=r.getToAddress();
        if(to==null||!to.isMemoryAddress()) continue;
        Data d=lst.getDataContaining(to);
        if(d!=null && d.hasStringValue()){
          String s=d.getValue().toString();
          seen.add(to+": "+s);
        } else {
          // CFString const: text pointer is at to+8 often; try reading a C string at *(to+8)
          try{
            long p = getInt(to.add(8)) & 0xffffffffL;
            if(p>0x10000 && p<0x170000){ Data sd=lst.getDataContaining(toAddr(p));
              if(sd!=null&&sd.hasStringValue()) seen.add(to+"->"+sd.getValue()); }
          }catch(Exception ex){}
        }
      }
    }
    for(String s: seen) println(s);
  }
}
