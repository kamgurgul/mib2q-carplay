# Ghidra post-script (Jython). Find functions referencing the setup-handler
# strings and decompile them, so we can see why stream 111 is dropped.
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

targets = [0x14fa88, 0x156ea8, 0x156e9c, 0x156ec8, 0x156d2c, 0x156bd8]
fm = currentProgram.getFunctionManager()
rm = currentProgram.getReferenceManager()
af = currentProgram.getAddressFactory()

funcs = set()
for t in targets:
    a = af.getAddress("0x%x" % t)
    n = 0
    it = rm.getReferencesTo(a)
    for r in it:
        fr = r.getFromAddress()
        fn = fm.getFunctionContaining(fr)
        nm = fn.getName() if fn else "?"
        ep = ("0x%s" % fn.getEntryPoint()) if fn else "?"
        print("REF 0x%x <- %s  func=%s @ %s" % (t, fr, nm, ep))
        if fn: funcs.add(fn)
        n += 1
    if n == 0:
        print("REF 0x%x <- (none found)" % t)

di = DecompInterface(); di.openProgram(currentProgram); mon = ConsoleTaskMonitor()
for fn in funcs:
    print("\n================ DECOMPILE %s @ 0x%s ================" % (fn.getName(), fn.getEntryPoint()))
    res = di.decompileFunction(fn, 120, mon)
    if res and res.getDecompiledFunction():
        print(res.getDecompiledFunction().getC())
    else:
        print("(decompile failed)")
