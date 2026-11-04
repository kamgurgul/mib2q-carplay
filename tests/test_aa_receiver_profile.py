#!/usr/bin/env python3
"""scripts/aa_receiver_profile.py against the hook's own profile table.

Its PROFILES must equal profiles[] in aa_hook/aa_hook.c, and a gal of another build at a
built-in profile's addresses (MU1316's gal at P5152's) must read as pinned: nothing to
port. Uses tiny synthetic ELF files; the MU0918 receiver hash is pointed at the synthetic
receiver. Prints PASS.
"""
from pathlib import Path
import contextlib
import hashlib
import importlib.util
import io
import re
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("aa_receiver_profile", ROOT / "scripts/aa_receiver_profile.py")
arp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(arp)

# 1. the script's table is the hook's table
src = (ROOT / "aa_hook/aa_hook.c").read_text()
block = src[src.index("static const struct aa_profile profiles[] = {"):]
block = block[:block.index("};")]
hook = [(name, tuple(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", body)))
        for name, body in re.findall(r'\{\s*"(\w+)",([^}]*)\}', block)]
script = [(name, r + g) for name, r, g in arp.PROFILES]
assert hook and all(len(v) == 9 for _, v in hook), hook
assert hook == script, "PROFILES in aa_receiver_profile.py differ from aa_hook.c profiles[]"


def elf(symbols):
    """A 32-bit little-endian ELF with only .dynsym and .dynstr, enough for dynsyms()."""
    names, syms = b"\0", b"\0" * 16
    for name, value in symbols.items():
        syms += struct.pack("<IIIBBH", len(names), value, 0, 0x12, 0, 1)
        names += name.encode() + b"\0"
    dynsym, dynstr = 52, 52 + len(syms)
    shoff = dynstr + len(names)
    head = b"\x7fELF\x01\x01\x01" + b"\0" * 9 + struct.pack(
        "<HHIIIIIHHHHHH", 3, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 3, 0)
    sections = struct.pack("<10I", *[0] * 10)
    sections += struct.pack("<10I", 0, 11, 0, 0, dynsym, len(syms), 2, 0, 4, 16)
    sections += struct.pack("<10I", 0, 3, 0, 0, dynstr, len(names), 0, 0, 1, 0)
    return head + syms + names + sections


def run(tmp, gal_addresses):
    receiver = dict((s, 0x1000 + i) for i, s in enumerate(arp.REQUIRED))
    receiver.update(zip((s for _, s in arp.RECEIVER), arp.PROFILES[0][1]))
    gal = dict(zip((s for _, s in arp.GAL), gal_addresses))
    gal["_ZN3gal9otherbuildEv"] = 0x4242          # not a built-in gal by hash
    paths = []
    for name, data in (("gal", elf(gal)), ("libautoreceiver.so", elf(receiver))):
        (tmp / name).write_bytes(data)
        paths.append(str(tmp / name))
    arp.MU0918["libautoreceiver.so"] = hashlib.sha256((tmp / "libautoreceiver.so").read_bytes()).hexdigest()
    out = io.StringIO()
    sys.argv = ["aa_receiver_profile.py"] + paths
    with contextlib.redirect_stdout(out):
        rc = arp.main()
    return rc, out.getvalue()


with tempfile.TemporaryDirectory() as d:
    tmp = Path(d)
    # 2. MU1316: the MU0918 receiver, another gal build at P5152's addresses
    rc, out = run(tmp, (0x258580, 0x251b28))
    assert rc == 0 and "profile P5152" in out and "gal=pinned" in out and "nothing to port" in out, out
    assert "Candidate" not in out, out
    # 3. a gal at unknown addresses still runs unpinned and gets a candidate entry
    rc, out = run(tmp, (0x111110, 0x222220))
    assert rc == 2 and "gal=unpinned" in out and "Candidate" in out and "0x111110" in out, out

print("aa_receiver_profile: table equals aa_hook.c, other gal build at a profile's addresses pinned, unknown gal unpinned PASS")
