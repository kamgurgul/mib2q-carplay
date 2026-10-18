import ghidra.app.script.GhidraScript; import ghidra.program.model.listing.*;
import ghidra.program.model.address.*; import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*; import ghidra.program.model.data.*;
public class APKeys extends GhidraScript { public void run() throws Exception {
  FunctionManager fm=currentProgram.getFunctionManager();
  Listing lst=currentProgram.getListing(); ReferenceManager rm=currentProgram.getReferenceManager();
  Memory mem=currentProgram.getMemory();
  Function f=fm.getFunctionAt(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(0x2afe8L));
  if (f==null){println("no func");return;}
  println("=== string/data refs out of AirPlayInfoArrayAddScreenDisplay (key names) ===");
  InstructionIterator it=lst.getInstructions(f.getBody(),true);
  while(it.hasNext()){ Instruction ins=it.next();
    for(Reference r:ins.getReferencesFrom()){ if(r.getReferenceType().isData()){
      Address to=r.getToAddress(); String s=readCStr(mem,to);
      println(String.format("  %08x %-18s -> %08x  %s", ins.getAddress().getOffset(), ins.toString(), to.getOffset(), s==null?"":"\""+s+"\"")); }}
  }
}
String readCStr(Memory m, Address a){ try{ StringBuilder sb=new StringBuilder();
  for(int i=0;i<64;i++){ int c=m.getByte(a.add(i))&0xff; if(c==0)break; if(c<32||c>126)return null; sb.append((char)c);} 
  return sb.length()>0?sb.toString():null;}catch(Exception e){return null;} }
}
