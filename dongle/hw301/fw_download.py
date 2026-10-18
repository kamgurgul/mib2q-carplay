#!/usr/bin/env python3
"""Download HW301 dongle firmware from the vendor update server (same chunked API the web UI uses).

usage: fw_download.py <versioncode> [...]   -> fw/hw301_v<code>.tar
"""
import base64, json, os, sys, urllib.request

SERVER = "http://43.138.184.52/index.php?fun=appdatas&hw=301&version={v}&pos={p}"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fw")

def chunk(v, p):
    with urllib.request.urlopen(SERVER.format(v=v, p=p), timeout=30) as r:
        d = json.load(r)
    if d.get("result") != 1:
        raise SystemExit(f"v{v} pos {p}: {d}")
    return d

for v in sys.argv[1:]:
    first = chunk(v, 0)
    count, size = first["count"], first["filesize"]
    print(f"v{v}: {count} chunks, {size} bytes")
    data = bytearray()
    for p in range(count):
        d = first if p == 0 else chunk(v, p)
        raw = base64.b64decode(d["data"])
        assert len(raw) == d["datasize"], (p, len(raw), d["datasize"])
        data += raw
    assert len(data) == size, (len(data), size)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f"hw301_v{v}.tar")
    open(path, "wb").write(data)
    print("  ->", path)
