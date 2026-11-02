/* SPDX-License-Identifier: GPL-3.0-or-later */
package com.luka.carplay.aa;

import java.io.RandomAccessFile;

/**
 * aa_trip.c -> Java. AAT1, 320 bytes, big-endian:
 * magic[0], sequence u32[4], state/position system milliseconds u64[8/16],
 * final-destination meters/seconds i32[24/28] (-1 unknown), NUL-terminated
 * UTF-8 destination[32..287], reserved zero bytes[288..315], FNV-1a u32[316].
 * Independent sample times stop fresh road/name messages reviving old estimates.
 */
public final class AaTripFeed {
    public static final String PATH = "/tmp/aa_trip";
    static final int RECORD_SIZE = 320;
    static final long MAX_AGE_MS = 30000L;

    public static final class Trip {
        public final String destination;
        public final int meters, seconds;
        public final long stateMs, positionMs;

        Trip(String destination, int meters, int seconds, long stateMs, long positionMs) {
            this.destination = destination;
            this.meters = meters;
            this.seconds = seconds;
            this.stateMs = stateMs;
            this.positionMs = positionMs;
        }
    }

    private AaTripFeed() { }

    private static int u32(byte[] b, int p) {
        return (b[p] & 255) << 24 | (b[p + 1] & 255) << 16 | (b[p + 2] & 255) << 8 | (b[p + 3] & 255);
    }

    private static long u64(byte[] b, int p) {
        return ((long) u32(b, p) << 32) | (u32(b, p + 4) & 0xffffffffL);
    }

    public static Trip parse(byte[] b) {
        try {
            if (b == null || b.length != RECORD_SIZE || u32(b, 0) != 0x41415431) return null;
            int hash = 0x811c9dc5;
            for (int i = 0; i < RECORD_SIZE - 4; i++) hash = (hash ^ (b[i] & 255)) * 16777619;
            if (hash != u32(b, RECORD_SIZE - 4)) return null;
            long state = u64(b, 8), position = u64(b, 16);
            int meters = u32(b, 24), seconds = u32(b, 28), end = 32;
            if (state < 0 || position < 0 || meters < -1 || seconds < -1) return null;
            while (end < 288 && b[end] != 0) end++;
            if (end == 288) return null;
            for (int i = end; i < RECORD_SIZE - 4; i++) if (b[i] != 0) return null;
            String destination = new String(b, 32, end - 32, "UTF-8");
            // Foundation 1.1 has no CharsetDecoder; a round trip detects invalid UTF-8.
            byte[] utf8 = destination.getBytes("UTF-8");
            if (utf8.length != end - 32) return null;
            for (int i = 0; i < utf8.length; i++) if (utf8[i] != b[32 + i]) return null;
            return new Trip(destination, meters, seconds, state, position);
        } catch (Exception e) { return null; }
    }

    public static Trip read(String path) {
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(path, "r");
            if (f.length() != RECORD_SIZE) return null;
            byte[] b = new byte[RECORD_SIZE];
            f.readFully(b);
            return parse(b);
        } catch (Exception e) { return null; }
        finally { if (f != null) try { f.close(); } catch (Exception e) { } }
    }

    static boolean fresh(long sample, long now, long after) {
        return sample > after && sample <= now && now - sample <= MAX_AGE_MS;
    }
}
