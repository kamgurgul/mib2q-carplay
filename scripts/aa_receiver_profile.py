#!/usr/bin/env python3
"""Android Auto receiver profile for the cluster-display hook (aa_hook/aa_hook.c).

    python3 scripts/aa_receiver_profile.py <card>/carplay_logs/aa/gal <card>/carplay_logs/aa/libautoreceiver.so

The logging M.I.B. mod copies both files from the unit once per card. This script

  1. compares them with the receiver pair validated on the car (MU0918). Identical
     files mean the built-in profile already applies: nothing to port;
  2. otherwise reads the symbols the hook checks from the ELF dynamic symbol tables and
     prints a profile entry for aa_hook.c, plus the matching abi.fingerprint values
     that /tmp/aa_cluster_hook.log shows on the unit.

A different receiver may also have different object layouts, which symbols cannot show.
Treat a new entry as a trial until its layouts were checked against the disassembly
(docs/android-auto/firmware-porting.md). Read-only; nothing is written.
"""
import hashlib
import struct
import sys

MU0918 = {
    "gal": "6fffb3608a5c504ec245b90ec4aed2646209dffd0644c5fa0504c29d0603a1b4",
    "libautoreceiver.so": "6ef4abea8264ab7787b0ecb3fe278a65488301f703148d0175010f7b228bdaf6",
}

RECEIVER = [
    ("reg", "_ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase"),
    ("sink_vt", "_ZTV9VideoSink"),
    ("base_vt", "_ZTV20ProtocolEndpointBase"),
    ("config", "_ZN9VideoSink25addSupportedConfigurationEiiiiiii"),
    ("send_config", "_ZN13MediaSinkBase10sendConfigEi"),
    ("set_focus", "_ZN9VideoSink13setVideoFocusEib"),
    ("ack", "_ZN13MediaSinkBase9ackFramesEij"),
]
# Interposed or called by name; all must exist for the hook to load at all.
REQUIRED = [s for _, s in RECEIVER] + [
    "_ZN9VideoSink16addDiscoveryInfoEP24ServiceDiscoveryResponse",
    "_ZN11InputSource16addDiscoveryInfoEP24ServiceDiscoveryResponse",
    "_ZN13MessageRouter20handleChannelOpenReqEhRK18ChannelOpenRequest",
    "_ZN13MessageRouter12routeMessageEhRK10shared_ptrI8IoBufferE",
    "_ZN13MessageRouter24queueOutgoingUnencryptedEhPvj",
    "_ZN13MessageRouter13queueOutgoingEhPvj",
    "_ZN13MessageRouter21sendUnexpectedMessageEh",
    "_ZN10Controller18sendVersionRequestEv",
    "_ZN10Controller21handleVersionResponseEPvj",
    "_ZN20ProtocolEndpointBase15onChannelClosedEh",
    "_ZN9VideoSinkD1Ev",
    "_ZN9VideoSinkD0Ev",
]
GAL = [
    ("gal_controller", "_ZN3gal14CGALController10s_instanceE"),
    ("gal_vsink_vt", "_ZTVN3gal14CVideoSinkImplE"),
]


def dynsyms(path):
    """{name: st_value} from .dynsym (32-bit little-endian ARM ELF)."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("%s: not a 32-bit little-endian ELF" % path)
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for sh in sections:
        if sh[1] != 11:                          # SHT_DYNSYM
            continue
        strtab = sections[sh[6]]                 # sh_link -> .dynstr
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value, _, _, _, st_shndx = struct.unpack_from("<IIIBBH", data, off)
            if not st_name or not st_shndx:      # undefined imports do not count
                continue
            end = data.index(b"\0", strtab[4] + st_name)
            out[data[strtab[4] + st_name:end].decode("ascii", "replace")] = st_value
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: aa_receiver_profile.py <gal> <libautoreceiver.so>")
    gal, recv = sys.argv[1], sys.argv[2]
    hashes = {}
    for name, path in (("gal", gal), ("libautoreceiver.so", recv)):
        hashes[name] = hashlib.sha256(open(path, "rb").read()).hexdigest()
        print("%-20s sha256 %s%s" % (name, hashes[name],
                                     "  (= MU0918)" if hashes[name] == MU0918[name] else ""))
    if hashes == MU0918:
        print("\nIdentical to the receiver validated on the car (MU0918): the built-in profile applies.")
        return 0

    if hashes["libautoreceiver.so"] == MU0918["libautoreceiver.so"]:
        print("\nThe receiver is identical to MU0918, so its object layouts are the validated ones;"
              "\nonly gal differs. The entry below (gal addresses from this gal) is safe to add.")
    g, r = dynsyms(gal), dynsyms(recv)
    missing = [s for s in REQUIRED if s not in r] + [s for _, s in GAL if s not in g]
    if missing:
        print("\nMissing symbols, the hook cannot run on this receiver:")
        for s in missing:
            print("  " + s)
        return 1
    v = dict((k, r[s]) for k, s in RECEIVER)
    v.update((k, g[s]) for k, s in GAL)
    print("\nSymbol offsets (compare with abi.fingerprint in /tmp/aa_cluster_hook.log):")
    print("  " + " ".join("%s=0x%x" % (k, v[k]) for k, _ in RECEIVER + GAL))
    print("\nCandidate aa_hook.c profile entry (UNVERIFIED object layouts, see firmware-porting.md):")
    print('    { "<firmware>", 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,\n      0x%x, 0x%x },'
          % tuple(v[k] for k, _ in RECEIVER + GAL))
    return 2


if __name__ == "__main__":
    sys.exit(main())
