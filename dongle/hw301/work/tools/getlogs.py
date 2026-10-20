#!/usr/bin/env python3
"""Pull the dongle's logs to this machine, WITHOUT uploading to the vendor.

The web UI's "report" button calls /getlogs (which builds applogs-*.tar on the
device from /mnt/UDISK/logs/app.log + dmesg) and THEN PUTs that tar to the
vendor server. We do only the first half: call /getlogs, then GET the tar off
the dongle's own web root and save it locally.

  getlogs.py [--host 192.168.5.1] [--out work/logs]

Run the Mac on the dongle's Wi-Fi. Nothing is sent to the vendor or the car.
"""
import argparse, datetime, json, os, sys, tarfile, urllib.parse, urllib.request

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.5.1")
    ap.add_argument("--out", default="work/logs")
    a = ap.parse_args()

    # dummy report fields (the device just needs them non-empty to build the tar).
    # the device puts datetime straight into the tar FILENAME, so keep it free of
    # spaces/colons or the resulting URL/path is invalid.
    now = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    body = json.dumps({"datetime": now, "carbrand": "debug",
                       "carmodel": "debug", "carmakeyear": "2025",
                       "carproblem": "local log pull"})
    r = urllib.request.urlopen(urllib.request.Request(
        f"http://{a.host}/getlogs", data=body.encode(),
        headers={"Content-Type": "application/x-www-form-urlencoded; charset=UTF-8"}),
        timeout=30)
    resp = json.loads(r.read() or b"{}")
    url = resp.get("logsurl")
    if not url:
        sys.exit(f"/getlogs gave no logsurl: {resp}")
    print(f"device built: {url}")

    os.makedirs(a.out, exist_ok=True)
    tarpath = os.path.join(a.out, os.path.basename(url))
    # URL-encode the path (belt-and-suspenders for any spaces the device added)
    safe = urllib.parse.quote(url)
    data = urllib.request.urlopen(f"http://{a.host}/{safe}", timeout=60).read()
    open(tarpath, "wb").write(data)
    print(f"saved {len(data)} bytes -> {tarpath}")

    with tarfile.open(tarpath) as t:
        names = t.getnames()
        t.extractall(a.out, filter="data")
    print("extracted:", ", ".join(n for n in names if n not in (".", "./")))
    print(f"\nlook for shim output:\n  grep -a cluster_dual {a.out}/dmesg.log")

if __name__ == "__main__":
    main()
