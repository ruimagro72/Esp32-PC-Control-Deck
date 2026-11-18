package dev.controldeck.host.selftest;

import java.util.Objects;

/**
 * A minimal test runner for the self-test battery (no JUnit, no test dependency).
 * A test that throws fails; a test that throws {@link Skip} is reported as skipped
 * (used when a dependency such as the SQLite driver is not on the class path).
 */
public final class Runner {

    @FunctionalInterface
    public interface Body {
        void run() throws Exception;
    }

    /** Thrown by a test that cannot run in this environment. */
    public static final class Skip extends RuntimeException {
        private static final long serialVersionUID = 1L;

        public Skip(String reason) {
            super(reason);
        }
    }

    private int passed;
    private int failed;
    private int skipped;

    public void test(String name, Body body) {
        try {
            body.run();
            passed++;
            System.out.println("  PASS  " + name);
        } catch (Skip skip) {
            skipped++;
            System.out.println("  SKIP  " + name + " (" + skip.getMessage() + ")");
        } catch (Throwable error) {
            failed++;
            System.out.println("  FAIL  " + name);
            System.out.println("        " + error);
        }
    }

    public static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    public static void equal(Object expected, Object actual, String what) {
        if (!Objects.equals(expected, actual)) {
            throw new AssertionError(what + ": expected <" + expected + "> but was <" + actual + ">");
        }
    }

    public static void close(double expected, Double actual, double tolerance, String what) {
        if (actual == null || Math.abs(expected - actual) > tolerance) {
            throw new AssertionError(what + ": expected ~" + expected + " but was <" + actual + ">");
        }
    }

    public int failed() {
        return failed;
    }

    /** Prints the totals and returns the process exit code: 0 when nothing failed. */
    public int summary() {
        System.out.println();
        System.out.println("Passed " + passed + ", failed " + failed + ", skipped " + skipped);
        return failed == 0 ? 0 : 1;
    }
}
