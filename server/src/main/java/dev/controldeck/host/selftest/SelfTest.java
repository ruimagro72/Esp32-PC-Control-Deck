package dev.controldeck.host.selftest;

/**
 * The self-test battery: {@code java -jar control-deck-host-0.6.0-all.jar --selftest}.
 *
 * Unit checks run anywhere. Checks that need the SQLite driver are reported as
 * skipped when it is not on the class path, which is the case for a plain
 * {@code javac} run; the shaded jar from {@code mvn package} contains it.
 */
public final class SelfTest {

    private SelfTest() {
    }

    public static int run() {
        System.out.println("ESP32 PC Control Deck - Java host self-test");
        System.out.println();
        Runner runner = new Runner();
        UnitTests.run(runner);
        IntegrationTests.run(runner);
        SerialTests.run(runner);
        MacroTests.run(runner);
        FirmwareTests.run(runner);
        CatalogTests.run(runner);
        GpuTests.run(runner);
        TextTests.run(runner);
        Frame4Tests.run(runner);
        InboxTests.run(runner);
        TransferTests.run(runner);
        PackTests.run(runner);
        ConsoleTests.run(runner);
        return runner.summary();
    }
}
