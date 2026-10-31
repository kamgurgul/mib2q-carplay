import com.luka.carplay.core.RollerClickDetector;

public final class RollerClickDetectorTest {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        RollerClickDetector d = new RollerClickDetector(350L);

        d.onPressed(1000L);
        check(d.onReleased(1050L, true) == RollerClickDetector.NONE, "single fired early");
        check(d.poll(1399L) == RollerClickDetector.NONE, "single before deadline");
        check(d.poll(1400L) == RollerClickDetector.SINGLE, "single missing");

        d.onPressed(2000L);
        d.onReleased(2050L, true);
        d.onPressed(2200L);
        check(d.onReleased(2250L, true) == RollerClickDetector.DOUBLE, "double missing");
        check(d.poll(3000L) == RollerClickDetector.NONE, "double leaked a single");

        d.onPressed(4000L);
        d.onReleased(4050L, true);
        d.onPressed(4200L);
        d.onLongPress();
        check(d.onReleased(5000L, false) == RollerClickDetector.NONE, "long became short");
        check(d.poll(5000L) == RollerClickDetector.NONE, "long leaked first click");

        d.onPressed(6000L);
        d.onReleased(6050L, true);
        d.cancel();
        check(d.poll(7000L) == RollerClickDetector.NONE, "cancel leaked click");

        d.onPressed(8000L);
        d.onReleased(8050L, true);
        d.onPressed(8500L); /* worker deliberately has not polled the expired first click */
        d.onReleased(8550L, true);
        check(d.poll(8550L) == RollerClickDetector.SINGLE, "late worker lost first click");
        check(d.poll(8900L) == RollerClickDetector.SINGLE, "spaced second click missing");

        System.out.println("RollerClickDetectorTest: single, double, long, cancel and scheduler race PASS");
    }
}
