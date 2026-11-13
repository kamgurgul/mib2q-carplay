package com.luka.carplay.core;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;

/**
 * AltScreenModule's video liveness (the renderer's /tmp/altscreen_render.live): a file with
 * "seq=" is live while seq changes within ~1 s of the module's own polls, whatever the wall
 * clock or the file's mtime say; a file without seq (an older renderer) falls back to mtime.
 */
public final class AltScreenLivenessTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }

    static void write(File f, String text) throws Exception {
        FileOutputStream out = new FileOutputStream(f);
        out.write(text.getBytes("US-ASCII"));
        out.close();
    }

    public static void main(String[] args) throws Exception {
        File live = new File(args[0], "altscreen_render.live");
        System.setProperty("carplay.altscreen.liveFile", live.getPath());
        AltScreenModule m = new AltScreenModule();
        Method videoLive = AltScreenModule.class.getDeclaredMethod("videoLive", new Class[0]);
        videoLive.setAccessible(true);
        String pid = "pid=1\n";   /* pid 1 exists on any host with /proc; without /proc mtime/seq decide */

        check(!((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "no file = not live");

        /* seq: live while it changes, even with an mtime far in the past (clock stepped). */
        for (int i = 0; i < 10; i++) {
            write(live, pid + "seq=" + i + "\n");
            check(live.setLastModified(1000L), "set an old mtime");
            check(((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "seq changing = live " + i);
        }
        /* Unchanged seq: live for the next four polls (~1 s), then stale. */
        for (int i = 0; i < 4; i++)
            check(((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "unchanged seq, poll " + i);
        check(!((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "seq stuck for >1 s = stale");
        write(live, pid + "seq=11\n");
        check(((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "seq moves again = live");

        /* No seq (older renderer): the mtime rule. */
        write(live, pid);
        check(((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "fresh mtime = live");
        check(live.setLastModified(System.currentTimeMillis() - 5000L), "age the file");
        check(!((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "old mtime = stale");

        /* Malformed pid = not live. */
        write(live, "pid=x\nseq=12\n");
        check(!((Boolean) videoLive.invoke(m, new Object[0])).booleanValue(), "bad pid");
        check(AltScreenModule.field("pid=42\nseq=7\n", "seq=") == 7L, "field parse");
        check(AltScreenModule.field("seq=\n", "seq=") == -1L, "empty field");
        System.out.println("AltScreenLivenessTest: seq heartbeat independent of the wall clock, "
            + "1 s stale limit, mtime fallback, bad pid PASS");
    }
}
