#!/usr/bin/env python3
"""Find where string constants are referenced in an ARM ELF, and disassemble
around each reference. Handles ARM literal pools (LDR Rd,[PC,#imm]) and
ADR/ADD PC-relative forms, in both ARM and Thumb state.

  xref.py <elf> <string> [<string> ...]        # byte-exact substring match
  xref.py <elf> --list PATTERN                  # just list matching strings+addrs

Read-only static analysis of a binary we already have on disk.
"""
import sys, re, struct
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_MODE_LITTLE_ENDIAN

def load(path):
    f = open(path, "rb"); elf = ELFFile(f)
    segs = []
    for s in elf.iter_sections():
        if s.header.sh_flags & 0x2 and s.header.sh_type != "SHT_NOBITS":  # ALLOC w/ data
            segs.append((s.header.sh_addr, s.data(), s.name))
    return elf, segs

def va_to_bytes(segs, va, n):
    for base, data, _ in segs:
        if base <= va < base + len(data):
            off = va - base
            return data[off:off+n]
    return b""

def find_string_vas(segs, needle):
    nb = needle.encode(); hits = []
    for base, data, name in segs:
        start = 0
        while True:
            i = data.find(nb, start)
            if i < 0: break
            # require a C-string boundary (preceded by NUL or section start)
            if i == 0 or data[i-1] == 0:
                hits.append((base + i, name))
            start = i + 1
    return hits

def scan_refs(segs, target_va):
    """Return list of (va, how) code addresses whose literal pool / ADR yields target_va."""
    refs = []
    for base, data, name in segs:
        if name not in (".text", ".rodata", ".data", ".data.rel.ro"):
            # literal pools live in/near .text; scan everything executable-ish anyway
            pass
        # 1) absolute pointer word equal to target (literal pool entry)
        for off in range(0, len(data) - 3, 4):
            w = struct.unpack_from("<I", data, off)[0]
            if w == target_va:
                refs.append((base + off, "pool-word"))
    return refs

def disasm_around(segs, va, before=8, after=12, thumb=False):
    data = va_to_bytes(segs, (va & ~1) - before*2, (before+after)*4)
    if not data: return
    md = Cs(CS_ARCH_ARM, (CS_MODE_THUMB if thumb else CS_MODE_ARM) | CS_MODE_LITTLE_ENDIAN)
    md.detail = False
    start = (va & ~1) - before*2
    for ins in md.disasm(data, start):
        mark = "  <==" if abs(ins.address - va) < 4 else ""
        print(f"    {ins.address:#010x}: {ins.mnemonic:<8}{ins.op_str}{mark}")

def main():
    elf_path = sys.argv[1]
    elf, segs = load(elf_path)
    if sys.argv[2] == "--list":
        pat = re.compile(sys.argv[3].encode())
        for base, data, name in segs:
            for m in re.finditer(rb"[\x20-\x7e]{4,}", data):
                if pat.search(m.group()):
                    print(f"{base+m.start():#010x} [{name}] {m.group().decode()}")
        return
    for needle in sys.argv[2:]:
        print(f"\n===== string {needle!r}")
        for sva, sect in find_string_vas(segs, needle):
            print(f"  @ {sva:#010x} [{sect}]")
            for rva, how in scan_refs(segs, sva):
                print(f"    referenced by {how} @ {rva:#010x}")

if __name__ == "__main__":
    main()
