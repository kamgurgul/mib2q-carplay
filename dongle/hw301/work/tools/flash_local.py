#!/usr/bin/env python3
"""Flash a local HW301 OTA tar to the dongle WITHOUT the vendor server.

Mimics the dongle web UI's own update path (requestuploadappdatasDatas ->
/uploadappdatas, then requestUpdateApp -> /requestupdate -> getupdatestatus).
Each chunk is the exact JSON the vendor's `index.php?fun=appdatas` returns, so
UpdateServer reassembles /tmp/appupdate/appupdate.tar, checks md5, dd's app.img.

Progress is shown here (upload %, then flash %), by polling /getupdatestatus the
same way the web UI does, so the browser is not needed to watch it.

  flash_local.py <tar> [--host 192.168.5.1] [--version N] [--dry-run]

Run the Mac on the dongle's Wi-Fi first. --dry-run only prints the plan and
does NOT write to the device. This never contacts the vendor or the car.
"""
import argparse, base64, json, math, sys, time, urllib.request

ITEM = 32768  # vendor itemsize / chunk size

def bar(frac, n=30):
    frac = max(0.0, min(1.0, frac)); f = int(frac * n)
    return "[" + "#" * f + "-" * (n - f) + f"] {frac*100:5.1f}%"

def vendor_json(obj):
    """Serialize a chunk compactly (no space after ':' or ','), like the vendor
    server. Python's default json.dumps adds spaces, which the dongle's
    hand-rolled JSON parser mis-handles. NOTE: do NOT escape '/' as \\/ — the
    device rejects that (chunk result:0); it wants bare forward slashes."""
    return json.dumps(obj, separators=(",", ":"))

def pct_of(d):
    """Read a flash-progress percent (0..100) from a status reply, tolerant of
    firmware variants: percent/progress as int or '37%', or pos/count."""
    for k in ("percent", "progress"):
        if k in d:
            try: return float(str(d[k]).rstrip("%"))
            except ValueError: pass
    if "pos" in d and d.get("count"):
        try: return 100.0 * float(d["pos"]) / float(d["count"])
        except (ValueError, ZeroDivisionError): pass
    return None

def post(host, path, body):
    # same as the web UI's jQuery $.post(path, string): form content type, raw string body
    r = urllib.request.urlopen(
        urllib.request.Request(f"http://{host}/{path}", data=body.encode(),
            headers={"Content-Type": "application/x-www-form-urlencoded; charset=UTF-8"}),
        timeout=30)
    return json.loads(r.read() or b"{}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tar"); ap.add_argument("--host", default="192.168.5.1")
    ap.add_argument("--version", type=int, default=9999)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--poll-only", action="store_true", help="old behavior: one requestupdate then poll getupdatestatus")
    a = ap.parse_args()

    blob = open(a.tar, "rb").read()
    count = math.ceil(len(blob) / ITEM)
    print(f"tar {a.tar}: {len(blob)} bytes, {count} chunks of {ITEM}, version {a.version}")
    if a.dry_run:
        print("dry-run: not sending. Chunk 0 preview:")
        raw = blob[:ITEM]
        print(json.dumps({"result":1,"version":a.version,"pos":0,"itemsize":ITEM,
            "count":count,"filesize":len(blob),"datasize":len(raw),
            "data":f"<base64 {len(base64.b64encode(raw))} chars>"}))
        return

    print("uploading image to dongle:")
    for pos in range(count):
        raw = blob[pos*ITEM:(pos+1)*ITEM]
        chunk = {"result":1,"version":a.version,"pos":pos,"itemsize":ITEM,
                 "count":count,"filesize":len(blob),"datasize":len(raw),
                 "data":base64.b64encode(raw).decode()}
        resp = post(a.host, "uploadappdatas", vendor_json(chunk))
        if resp.get("result") != 1 or resp.get("pos") != pos:
            sys.exit(f"\nchunk {pos} rejected: {resp}")
        print(f"\r  upload {bar((pos+1)/count)}  chunk {pos+1}/{count}", end="", flush=True)
    print("\nupload complete; triggering flash (/requestupdate)")

    # The dd write is sliced (seek/skip/count). Earlier runs showed a single
    # /requestupdate then polling /getupdatestatus climbs only to ~10% and then
    # the whole state RESETS (pos:0, percent:0) -> write never persists. So drive
    # the write by calling /requestupdate repeatedly (each call advances a slice)
    # and use /getupdatestatus only to observe. --poll-only reverts to the old
    # poll-after-single-trigger behavior for comparison.
    print("writing image to flash (do NOT unplug):")
    pct = 0.0; last = None; t_moved = time.time(); stall = 0
    while True:
        try:
            if a.poll_only:
                if last is None:
                    r = post(a.host, "requestupdate", ""); print(f"  requestupdate -> {r}")
                r = post(a.host, "getupdatestatus", "")
            else:
                r = post(a.host, "requestupdate", "")   # advance one slice
        except Exception as e:
            print(f"\n  endpoint stopped responding ({e}); likely finished + reset. Re-plug and verify.")
            break
        if r != last:
            print(f"  -> {r}"); last = r
        new = pct_of(r)
        if new is None:
            print(f"\n  no progress field in reply: {r}"); break
        print(f"\r  flash  {bar(new/100)}", end="", flush=True)
        if new >= 100:
            print("\n  flash reported 100%."); break
        # detect the reset-to-0 that marks an on-device abort
        if new + 1 < pct:
            print(f"\n  progress RESET ({pct:.0f}% -> {new:.0f}%): the device aborted the "
                  f"write (last reply {r}).")
            print("  almost certainly umount /mnt/app failing because CPAAProxyEx is "
                  "running. Need to stop the proxy before flashing.")
            break
        if new != pct:
            pct = new; t_moved = time.time()
        elif time.time() - t_moved > 60:
            print(f"\n  no progress for 60s; last reply {r}."); break
        time.sleep(0.25)
    print("\nverify with: curl http://192.168.5.1/clbuild.txt  (then re-plug)")

if __name__ == "__main__":
    main()
