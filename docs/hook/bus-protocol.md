---
title: CarplayBus - hook <-> Java localhost bus
tags: [hook, bus, ipc, verified]
status: verified-source
sources:
  - code: hook/framework/bus_protocol.h
  - code: hook/framework/bus.c
  - code: hook/framework/signal_guard.c
  - code: java_patch/com/luka/carplay/bus/CarplayBus.java
---

# CarplayBus - hook <-> Java localhost bus

The link that carries parsed iAP2 state from the C hook to the Java HMI patch.

## 📋 Context

> [iap2-interception](iap2-interception.md) -> module emits event -> **bus-protocol** - TCP :19810 -> Java module
> ([rgd-activation](../rgd/rgd-activation.md), [cover-art](cover-art.md)).

## 🌐 Topology

TCP `127.0.0.1:19810`. **Java is the long-lived server** (alive from HMI boot); the **hook is the
client**, (re)connecting once per CarPlay session. Idempotent reconnect on either side's restart.
No application heartbeat on this leg - Java relies on TCP FIN/RST + `setKeepAlive(true)`.

## 📊 Wire frame (16-byte header, big-endian)

| off | size | field |
|---:|---:|---|
| 0 | 4 | MAGIC `0x43504842` (`'CPHB'`) |
| 4 | 4 | seq (u32, per-side monotonic) |
| 8 | 2 | type (EVT_* / CMD_*) |
| 10 | 1 | flags (`BUS_FLAG_*`) |
| 11 | 1 | reserved |
| 12 | 4 | payload len |
| 16 | ... | payload |

- **BINARY** (`0x02`) - packed struct; otherwise payload is text `key:type:value` lines.
- **STICKY** (`0x01`) - the **hook** caches the latest frame per type and replays the snapshot, bracketed
  by `EVT_SYNC_BEGIN`/`EVT_SYNC_END`, right after `EVT_HELLO` on every (re)connect and again on
  `CMD_SYNC_REQ`. Replays carry **REPLAY** (`0x04`) and a fresh seq (`begin < replay < end`). Non-sticky
  frames are fire-and-forget. If copying a new frame fails, the previous cache entry is kept.
- Types are a direct-indexed table below `MAX_TYPES = 0x0120`, identical in `bus.c` and
  `CarplayBus.java` (checked by `scripts/check_local_protocols.py`).

## 🧭 Direction

- **EVT_*** hook->Java: `EVT_RGD_UPDATE` (0x0020), `EVT_COVERART` (0x0010), `EVT_HELLO`, sync markers.
- **CMD_*** Java->hook: `CMD_SYNC_REQ` (0x0100) requests a sticky snapshot. `hook/altzoom` hands
  `CMD_ALT_ZOOM` (0x0110), `CMD_ALT_ZONE` (0x0111), `CMD_ALT_RGI` (0x0116) and, from 1.5,
  `CMD_ALT_HIDDEN` (0x0119, `[u8 0 shown / 1 hidden]`: the cockpit shows no phone map) to the
  AltScreen hook in the same process (`altscreen111_*`, resolved with `dlsym`; dropped without it).
  `CMD_TEL` (0x0117) and `CMD_VEHICLE` (0x0118): [phone-and-vehicle](phone-and-vehicle.md).
  `CMD_ALT_APPEARANCE` (0x0114) and `CMD_ALT_UICTX` (0x0115) are reserved MHI3 ids with no
  handler: the CarPlay command format for a per-display appearance is not known (the phone's
  night mode is session-wide and already follows the MMI through stock `requestNightMode`).

## ⚙️ Threads (hook side)

`connector` (connect + retry, HELLO + snapshot, then reads until the peer goes away) - `writer`
(drains the outbound queue) - a 1 Hz `timer` driving `rgd_periodic_tick` (deferred `route_state=0`
flush - see [rgd-activation](../rgd/rgd-activation.md)); the timer sends no application heartbeat.

## 🔄 Connection lifecycle (fd + generation)

Every new connection bumps a **generation**. A queued frame is written only if the socket's `(fd,
generation)` is still current, checked under the same locks that serialize frames and socket
replacement, so a writer can never append to a replaced connection or a reused fd number. A failed
write **retires** the connection (`shutdown(SHUT_RDWR)`) before another writer can add bytes to a
partial frame; only the **connector** calls `close()`, after its last read, so a descriptor is never
freed under a blocked `recv`. Shutdown first wakes blocked senders (`shutdown`), then lets the connector
close. Fork children never tear down the parent's bus (owner PID).

## ⚙️ Signal policy

QNX io-pkt rejects `MSG_NOSIGNAL` (`ENOSYS`), so every hook socket write uses `flags = 0` and the bus
ignores `SIGPIPE` while it is active. `bus_init` installs this through `signal_guard`: `SIGPIPE` ->
`SIG_IGN`, and diagnostic handlers for `SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE` that write one fixed line
(async-signal-safe) and then **chain to `dio_manager`'s exact previous disposition**. The previous
`sigaction`s are saved and restored on shutdown or a failed init; control signals
(`SIGTERM/INT/QUIT/HUP/USR1/USR2`) are never touched. Host tests: `tests/signal_guard_test.c`,
`tests/bus_transport_test.c` (`scripts/run_tests.sh`).
