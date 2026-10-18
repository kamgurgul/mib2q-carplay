#!/usr/bin/env python3
"""Apply byte patches to a file. Args: <file> <off>:<hexbyte> [<off>:<hexbyte> ...]
off is a file offset (hex ok). Verifies and reports old->new."""
import sys
f=sys.argv[1]; data=bytearray(open(f,'rb').read())
for spec in sys.argv[2:]:
    off,nb=spec.split(':'); off=int(off,0); nb=int(nb,16)
    print(f"  {f} @ {off:#x}: {data[off]:#04x} -> {nb:#04x}")
    data[off]=nb
open(f,'wb').write(data)
print("patched")
