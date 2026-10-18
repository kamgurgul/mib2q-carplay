#!/usr/bin/env python3
"""Read-only dump of the dongle's web UI (192.168.5.1). Run while the Mac is on the dongle Wi-Fi.

Only GETs static files (html/js/css/json/txt) with no query string, and skips any path that
looks like an action. Also does a TCP connect check on common service ports.
"""
import os, re, socket, sys, time, urllib.request, urllib.parse

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.5.1"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dongle_dump")
STATIC = (".html", ".htm", ".js", ".css", ".json", ".txt", ".xml", ".map")
DANGER = re.compile(r"reset|reboot|restart|upgrade|update|flash|switch|set|save|delete|erase|format|factory|cgi|api|action", re.I)
PORTS = [21, 22, 23, 53, 80, 443, 5000, 5555, 7000, 7100, 8000, 8080, 8888, 9000, 10000]

os.makedirs(OUT, exist_ok=True)
logf = open(os.path.join(OUT, "log.txt"), "a")
def log(*a):
    s = " ".join(str(x) for x in a); print(s); logf.write(s + "\n"); logf.flush()

log("=== start", time.ctime(), "host", HOST)
for _ in range(60):
    try:
        socket.create_connection((HOST, 80), 2).close(); break
    except OSError:
        log("waiting for", HOST, "..."); time.sleep(2)
else:
    log("host not reachable, is the Mac on the dongle Wi-Fi?"); sys.exit(1)

for p in PORTS:
    try:
        s = socket.create_connection((HOST, p), 1.5); s.settimeout(1.5)
        try: banner = s.recv(200)
        except OSError: banner = b""
        s.close(); log("port", p, "OPEN", repr(banner))
    except OSError:
        pass

seen, queue = set(), ["/", "/index.html"]
LINK = re.compile(r"""(?:href|src)\s*=\s*["']([^"'#]+)["']|["']([\w./-]+\.(?:html?|js|css|json|txt|xml))["']""", re.I)
while queue and len(seen) < 300:
    path = queue.pop(0)
    if path in seen: continue
    seen.add(path)
    url = f"http://{HOST}{path}"
    try:
        r = urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (iPhone)"}), timeout=8)
        body, final = r.read(), r.geturl()
    except Exception as e:
        log("GET", path, "->", e); continue
    fpath = urllib.parse.urlparse(final).path
    log("GET", path, "->", len(body), "bytes", final if final != url else "")
    local = os.path.join(OUT, "www", fpath.lstrip("/") or "index")
    if local.endswith("/"): local += "index"
    os.makedirs(os.path.dirname(local), exist_ok=True)
    open(local, "wb").write(body)
    text = body.decode("utf-8", "replace")
    base = fpath if fpath.endswith("/") else fpath.rsplit("/", 1)[0] + "/"
    for m in LINK.finditer(text):
        link = m.group(1) or m.group(2)
        if link.startswith(("http://", "https://", "//", "data:", "javascript:", "mailto:")):
            if HOST not in link: log("external ref:", link)
            continue
        u = urllib.parse.urlparse(urllib.parse.urljoin(base, link))
        p = u.path
        if u.query or not p.lower().endswith(STATIC) or DANGER.search(os.path.basename(p)):
            if u.query or DANGER.search(p): log("skipped:", link)
            continue
        if p not in seen: queue.append(p)

# every URL-ish string in the scripts (update server, API endpoints) for offline review
urls = set()
for root, _, files in os.walk(os.path.join(OUT, "www")):
    for f in files:
        t = open(os.path.join(root, f), "rb").read().decode("utf-8", "replace")
        urls.update(re.findall(r"""https?://[^\s"'<>)]+""", t))
        urls.update(re.findall(r"""["'](/[\w./?=&-]{2,})["']""", t))
open(os.path.join(OUT, "urls.txt"), "w").write("\n".join(sorted(urls)) + "\n")
log("=== done:", len(seen), "paths tried,", len(urls), "url strings ->", OUT)
