import com.luka.carplay.input.TouchpadController;
import java.util.ArrayList;

/** Touchpad input must still navigate focus after removing the HU overlay. */
public final class TouchpadControllerTest {
    private static final class Sink implements TouchpadController.TouchSink {
        final ArrayList<Integer> keys = new ArrayList<Integer>();
        public void postDpad(int key) { keys.add(Integer.valueOf(key)); }
    }
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        TouchpadController input = TouchpadController.getInstance();
        Sink first = new Sink(); input.setTouchSink(first);
        input.onOneFinger(1000, 1000);
        input.onOneFinger(1010, 1010);
        check(first.keys.isEmpty(), "touch-down/jitter emitted a navigation key");
        int[] dx = {-1000, 1000, 0, 0}, dy = {0, 0, -1000, 1000};
        int[] keys = {TouchpadController.KEY_DPAD_LEFT, TouchpadController.KEY_DPAD_RIGHT,
                      TouchpadController.KEY_DPAD_UP, TouchpadController.KEY_DPAD_DOWN};
        for (int direction = 0; direction < keys.length; direction++) {
            input.onTouchEnd(); first.keys.clear();
            input.onOneFinger(2000, 2000);
            check(first.keys.isEmpty(), "new gesture retained prior movement");
            input.onOneFinger(2000 + dx[direction], 2000 + dy[direction]);
            check(first.keys.size() >= 5, "long swipe did not traverse multiple items");
            for (Integer key : first.keys) check(key.intValue() == keys[direction], "wrong DPAD direction");
        }
        /* A slow vertical swipe drifting sideways (1 px right per 4 px up) must stay vertical:
         * no phantom LEFT/RIGHT at the top of a list. */
        input.onTouchEnd(); first.keys.clear();
        int x = 3000, y = 6000;
        input.onOneFinger(x, y);
        for (int i = 0; i < 120; i++) { x += 10; y -= 40; input.onOneFinger(x, y); }
        check(!first.keys.isEmpty(), "slow diagonal-drift swipe moved nothing");
        for (Integer key : first.keys)
            check(key.intValue() == TouchpadController.KEY_DPAD_UP, "drift emitted a sideways key " + key);
        int oldCount = first.keys.size();
        input.setTouchSink(null);
        input.onOneFinger(0, 0);
        Sink next = new Sink(); input.setTouchSink(next);
        input.onOneFinger(8000, 8000);
        check(next.keys.isEmpty(), "reconnected sink inherited an old touch anchor");
        input.onOneFinger(9000, 8000);
        check(!next.keys.isEmpty() && first.keys.size() == oldCount, "event reached the previous session");
        input.onTouchEnd(); input.setTouchSink(null);
        System.out.println("TouchpadControllerTest: jitter, four directions, repeated ticks, sideways drift and sink reconnect PASS");
    }
}
