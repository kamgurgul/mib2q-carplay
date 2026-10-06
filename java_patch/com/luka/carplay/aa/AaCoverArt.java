/*
 * AaCoverArt — Android Auto album art -> the Virtual Cockpit media screen.
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster AaCoverArt (GPL-3.0-or-later).
 *
 * gal writes the phone's album art to /tmp/gal_albumArt_<n>.png and reports it through
 * DSIAndroidAuto2 coverArtUrl (updateCoverArtUrl(ResourceLocator, valid); valid=2 or no URL =
 * no art), which the stock HMI ignores. AaBridge forwards that event here. A worker thread
 * normalises the file to the verified cover format (PngCover: 256x256 RGB PNG; a small
 * complete JPEG is passed through), writes it next to the CarPlay cover in
 * /var/app/icab/tmp/37/ and hands it to CoverArt, whose CarPlay path then merges it into the
 * now-playing info for the VC (TerminalModeBapCombi, AppConnectorTerminalMode).
 *
 * Every picture gets a new file name (a new picture id forces the VC to fetch it); the
 * current and the previous file are kept because the VC may still be reading the old one.
 * A file that is missing, short or still being written is retried with backoff
 * (100/200/400/800 ms) and abandoned at once when newer art arrives. A "no art" report is
 * applied after a 400 ms grace, because gal clears and re-sets the art on every track change.
 *
 * Threading: the DSI and HMI threads only record requests; all file I/O runs on the worker.
 * Java 1.4.  SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.coverart.CoverArt;
import com.luka.carplay.framework.Log;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class AaCoverArt {
    private static final String TAG = "AaCover";
    public static final String DIR = "/var/app/icab/tmp/37";
    private static final String PREFIX = "aa_cover_";
    private static final long CLEAR_GRACE_MS = 400L;
    private static final long[] RETRY_MS = {100L, 200L, 400L, 800L};
    private static final int MIN_FILE_BYTES = 16;
    private static final long JPEG_MAX_BYTES = 512L * 1024L;
    private static final int JPEG_MAX_SIDE = 1024;

    private static final Object LOCK = new Object();
    private static boolean session;
    private static int sessionGen;
    private static boolean hasRequest;
    private static String requestUrl;
    private static long requestSeq;
    private static long currentCrc;
    private static int lastId;
    private static String currentFile, previousFile;
    private static Thread worker;

    private AaCoverArt() {}

    /** DSI thread (AaBridge.updateCoverArtUrl): record the request only. */
    public static void request(String url, int valid) {
        if (valid != 1 || url == null || url.length() == 0) url = null;
        synchronized (LOCK) {
            requestUrl = url;
            requestSeq++;
            hasRequest = true;
            ensureWorkerLocked();
            LOCK.notifyAll();
        }
        Log.i(TAG, "coverArtUrl valid=" + valid + " url=" + url);
    }

    /** Android Auto session start: a request that arrived before it is processed now. */
    public static void sessionStart() {
        synchronized (LOCK) {
            session = true;
            sessionGen++;
            currentCrc = 0;
            ensureWorkerLocked();
            LOCK.notifyAll();
        }
    }

    /** Android Auto session end: drop the art and our files. */
    public static void sessionEnd() {
        synchronized (LOCK) {
            session = false;
            sessionGen++;
            hasRequest = false;
            requestUrl = null;
            currentCrc = 0;
            LOCK.notifyAll();
        }
    }

    private static void ensureWorkerLocked() {
        if (worker != null && worker.isAlive()) return;
        Thread t = new Thread("carplay-aa-coverart") {
            public void run() {
                while (true) {
                    try {
                        step();
                    } catch (InterruptedException e) {
                        return;
                    } catch (Throwable e) {
                        Log.w(TAG, "worker: " + e);
                    }
                }
            }
        };
        t.setDaemon(true);
        worker = t;
        t.start();
    }

    private static void step() throws InterruptedException {
        String url;
        long seq;
        int gen;
        boolean cleanup = false;
        synchronized (LOCK) {
            while (!(hasRequest && session) && !(!session && currentFile != null)) LOCK.wait();
            if (!session) {
                cleanup = true;
                url = null; seq = 0; gen = sessionGen;
            } else {
                url = requestUrl; seq = requestSeq; gen = sessionGen;
                hasRequest = false;
            }
        }
        if (cleanup) {
            deleteFile(currentFile); deleteFile(previousFile);
            synchronized (LOCK) { currentFile = null; previousFile = null; }
            return;
        }
        if (url == null) {
            if (!waitQuiet(CLEAR_GRACE_MS, gen, seq)) return;
            if (CoverArt.getInstance().clearLocal()) Log.i(TAG, "art cleared (phone reports none)");
            return;
        }
        for (int attempt = 0; ; attempt++) {
            try {
                process(url, gen, seq);
                return;
            } catch (PngCover.Rejected e) {
                Log.w(TAG, url + " rejected: " + e.getMessage());
                return;
            } catch (Throwable e) {
                if (attempt >= RETRY_MS.length) {
                    Log.w(TAG, url + " unreadable: " + e);
                    return;
                }
                if (!waitQuiet(RETRY_MS[attempt], gen, seq)) return;
            }
        }
    }

    /** false when a newer request or a session change arrived while waiting. */
    private static boolean waitQuiet(long ms, int gen, long seq) throws InterruptedException {
        long end = System.currentTimeMillis() + ms;
        synchronized (LOCK) {
            while (true) {
                if (gen != sessionGen || !session || seq != requestSeq) return false;
                long left = end - System.currentTimeMillis();
                if (left <= 0) return true;
                LOCK.wait(left);
            }
        }
    }

    private static void process(String url, int gen, long seq) throws IOException {
        File f = new File(url);
        if (!f.isFile()) throw new IOException("missing");
        long len = f.length(), mtime = f.lastModified();
        if (len < MIN_FILE_BYTES) throw new IOException("only " + len + " bytes (still being written?)");
        if (len > PngCover.MAX_FILE_BYTES) throw new PngCover.Rejected("file too large: " + len + " bytes");
        byte[] head = new byte[MIN_FILE_BYTES];
        InputStream in = new FileInputStream(f);
        try { PngCover.readFully(in, head, 0, head.length); } finally { close(in); }
        byte[] out;
        String ext;
        if (PngCover.isPng(head)) {
            in = new BufferedInputStream(new FileInputStream(f), 8192);
            try { out = PngCover.toCover(in, new int[2]); } finally { close(in); }
            ext = ".png";
        } else if (PngCover.isJpeg(head)) {
            /* No JPEG decoder on J9: a small complete JPEG goes over unchanged. */
            if (len > JPEG_MAX_BYTES) throw new PngCover.Rejected("JPEG too large: " + len + " bytes");
            int[] wh;
            in = new BufferedInputStream(new FileInputStream(f), 4096);
            try { wh = PngCover.jpegSize(in, len); } finally { close(in); }
            if (wh == null) throw new IOException("JPEG without frame header (truncated?)");
            if (wh[0] <= 0 || wh[1] <= 0 || wh[0] > JPEG_MAX_SIDE || wh[1] > JPEG_MAX_SIDE)
                throw new PngCover.Rejected("JPEG " + wh[0] + "x" + wh[1] + " too large to pass through");
            out = new byte[(int) len];
            in = new FileInputStream(f);
            try { PngCover.readFully(in, out, 0, out.length); } finally { close(in); }
            if (!PngCover.jpegComplete(out, out.length)) throw new IOException("JPEG incomplete");
            ext = ".jpg";
        } else {
            throw new PngCover.Rejected("unsupported image format " + PngCover.sniff(head, head.length));
        }
        if (f.length() != len || f.lastModified() != mtime) throw new IOException("file changed while reading");

        long crc = PngCover.crc32(0, out, 0, out.length) & 0xFFFFFFFFL;
        int id;
        synchronized (LOCK) {
            if (gen != sessionGen || !session || seq != requestSeq) return;   /* superseded */
            if (crc == currentCrc) return;                                     /* same picture */
            id = (int) (crc & 0x7FFFFFFFL);
            if (id == lastId) id = (id + 1) & 0x7FFFFFFF;    /* same art after a clear: refresh */
            if (id == 0) id = 1;
            lastId = id;
        }
        File dir = new File(DIR);
        if (!dir.exists()) dir.mkdirs();
        File dst = new File(dir, PREFIX + Integer.toHexString(id) + ext);
        /* Written in place (tmpfs: no rename); nothing reads it before CoverArt learns its name. */
        FileOutputStream fo = new FileOutputStream(dst);
        boolean written = false;
        try { fo.write(out); written = true; } finally { close(fo); if (!written) dst.delete(); }

        String drop;
        synchronized (LOCK) {
            if (gen != sessionGen || !session || seq != requestSeq) { dst.delete(); return; }
            drop = previousFile;
            previousFile = currentFile;
            currentFile = dst.getPath();
            currentCrc = crc;
        }
        if (drop != null && !drop.equals(currentFile) && !drop.equals(previousFile)) deleteFile(drop);
        Log.i(TAG, url + " -> " + dst.getPath() + " (" + out.length + " bytes, id " + id + ")");
        CoverArt.getInstance().publishLocal(dst.getPath(), id);
    }

    private static void deleteFile(String path) {
        if (path == null) return;
        try { new File(path).delete(); } catch (Throwable t) { /* ignore */ }
    }

    private static void close(InputStream in) {
        try { in.close(); } catch (Throwable t) { /* ignore */ }
    }

    private static void close(FileOutputStream o) {
        try { o.close(); } catch (Throwable t) { /* ignore */ }
    }
}
