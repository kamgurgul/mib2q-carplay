# CarPlay child supervisor deployment

Android Auto uses the same pattern for `children.gal`: [`aa_child.json`](aa_child.json)
starts [`aa_startup.sh`](aa_startup.sh), which publishes `/tmp/aa_supervisor.owner`, starts
`carplay_monitor.sh` for that generation (renderers' library path, no `LD_PRELOAD`) and
`exec`s the stock `gal`, preloading `libaa_cluster_hook.so` only when it is installed and
`/mnt/app/root/aa_cluster.off` is absent. Every other field equals the stock gal child
([`aa_child_stock.json`](aa_child_stock.json)). See `docs/android-auto/overview.md`.

Replace `children.carplay` in
`/mnt/system/etc/eso/production/smartphone_integrator.json` with
[`carplay_child.json`](carplay_child.json).

The important ownership rule is that `children.carplay.envs` contains **no**
`LD_PRELOAD`. `carplay_startup.sh` applies the hook only to its direct
`dio_manager` child. This prevents the hook from loading into `/bin/sh` or
`maneuver_render`.

Keep `IPL_CONFIG_DIR_DIO_MANAGER=/etc/eso/production` in the child environment.
The supervisor inherits it and passes it unchanged to `dio_manager`; only the
hook variable is added on the final `dio_manager` command.

Install these executable files under `/mnt/app/root/hooks`:

- `carplay_startup.sh`
- `carplay_monitor.sh`
- `carplay_cleanup.sh`
- `carplay_processes.sh`
- `libcarplay_hook.so`
- `maneuver_render`
- `flag_atlas.rgba`

Do not overwrite `/etc/scripts/carplay_cleanup.sh`: the custom cleanup calls that
stock script only for Audi's mdnsd/PPS cleanup. The renderer is a persistent
service; it exits only on its own failure, a reboot or an explicit maintenance action.

Lifecycle semantics:

- `carplay_startup.sh` checks `dio_manager`, the hook and both helper scripts,
  atomically writes its PID to `/tmp/carplay_supervisor.owner`, starts
  `carplay_monitor.sh` in the background with `LD_PRELOAD` cleared, then `exec`s
  `dio_manager`, so smartphone_integrator tracks the exact dio PID;
- the monitor acts only while the owner file names its dio PID and that PID is
  alive; a newer generation takes over by rewriting the file;
- if `maneuver_render` already exists it is adopted, not restarted; an adopted
  renderer that fails the identity check is re-checked after 2 s, then replaced;
- the renderer PID is kept in `/tmp/carplay_maneuver_render.pid`; one finite
  `pidin ar` scan adopts a renderer from an older generation, then the monitor uses
  constant-time `/proc/<pid>` checks every 2 s;
- only a missing/crashed renderer is restarted, after a 5 s backoff;
- every `dio_manager` exit leaves `maneuver_render` alive, so a replacement
  inherits the existing EGL allocations instead of re-entering fragile Qualcomm
  `eglInitialize`;
- `maneuver_render` is a client of Java's route-scoped `:19800` listener, so it is
  preserved by process identity while RGI/Java intentionally has the listener closed;
- external cleanup is identity-less (smartphone_integrator does not tell it which dio
  generation it runs for), so it does NOT stop renderers by the shared PID file,
  which could hit an already-started replacement generation. Cleanup only invokes
  Audi's `/etc/scripts/carplay_cleanup.sh` for the stock mdnsd/PPS cleanup;
- the supervisor never signals `dio_manager` and never touches USB/OTG.

Verify on the unit in `/tmp/carplay_wrapper.log`: `[monitor] adopted maneuver_render`
or `[monitor] starting maneuver_render`, followed on dio exit by
`renderer remains persistent`.

CarPlay uses `restartDelay: 3000`, `stopTimeout: 8000`; `portResetTime` remains 250 ms.
The full settling delay is intentional: rapid OTG stop/start cycles can leave the
iAP2 NCM accessory with only one of its two interfaces enabled. Renderer ownership
is independent of it and must not be used as a reason to shorten the USB/device-stack
delay.

## 🔍 MU1316 QNX 6.5 compatibility audit

The supervisor deliberately uses only facilities present in the extracted P5087
firmware:

- `/bin/sh` is QNX PD KSH 5.2.14 (`VERSION=650-4423`) and supports the functions,
  traps, command substitution, background jobs, `read`, and `exec` used here;
- `kill` is a shell builtin; the scripts use **numeric** signals (`kill -15`, `kill -9`)
  because that form is accepted by every kill implementation. Stock scripts kill by name
  via `slay -f -s SIGTERM -m name` and, in one spot, `kill -sigkill <pid>`; we kill one
  recorded PID after an identity check instead (slay-by-name would also hit the replacement
  generation), so only the signal token had to be a universally-accepted numeric one;
- `pidin ar` / `pidin -p <pid> ar` supply finite process snapshots; no blocking
  `/proc/*/cmdline` walk and no GNU process utilities are used. They are used only
  to adopt a pre-existing renderer and to confirm identity before a signal; normal
  checks use the recorded PID;
- `rm -f` and integer `sleep` are stock facilities; the original firmware uses
  `sleep` throughout `startup.sh` and `/etc/scripts`;
- the shared process helper appends a safe subset of the stock MMX command search
  locations and retains the inherited head, so its utilities do not depend on
  `smartphone_integrator` preserving `PATH` and dio keeps its original resolution order.

No GNU-only commands or options are used: there is no `pgrep`, `pkill`, `wait -n`,
GNU `stat`, `readlink -f`, `timeout`, fractional sleep, or dependency on `tr`.
