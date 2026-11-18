package dev.controldeck.host.transport.serial;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.transport.TransportRegistry;
import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.function.Supplier;

/**
 * The USB serial transport: finds the board, keeps the connection open and runs a
 * {@link SerialSession} on it, reconnecting when the cable is pulled or the board
 * resets. Protocol 4's binary frames, COBS-encoded and ended by 0x00 (docs/04-PROTOCOL.md), 8N1;
 * the debug build's "# ..." text lines go to the log. Behind a USB-UART chip the configured rate
 * is tried first, then 921600 and 115200 (the boards' own), until a Deck answers.
 *
 * It runs on its own daemon thread. What it is doing is reported in the transport
 * registry (state connecting, connected, error or stopped), which the API shows.
 * A missing board is not fatal: the bridge keeps trying and logs each distinct
 * problem once.
 */
public final class SerialBridge {

    /** What the bridge needs to know, read again at the start of every connection attempt. */
    public record Settings(boolean enabled, String port, int baud, double intervalSeconds) {
    }

    private static final int MAX_LINE_LENGTH = 4096;
    private static final int WRITE_CHUNK_BYTES = 64;
    private static final long WRITE_PAUSE_MS = 20;
    // A Deck on its native USB, or on this PC, has no 115200 line to fill: key images need the speed.
    private static final int WRITE_CHUNK_BYTES_FAST = 256;
    private static final long WRITE_PAUSE_MS_FAST = 10;
    private static final long STOP_WAIT_MS = 3000;
    // A board behind a USB-UART chip runs at its own rate (the E32R35T at 921600, older firmware
    // at 115200): when no Deck answers at one within this time, the next is tried.
    static final List<Integer> UART_BAUD_RATES = List.of(921_600, 115_200);
    static final long ANSWER_WAIT_MS = 3000;

    private final Supplier<Settings> settings;
    private final MessageHandler messages;
    private final SerialSession.Stats stats;
    private final TransportRegistry registry;
    private final SerialPortProvider provider;
    private final SerialLog log;
    private final long reconnectDelayMs;
    private final PortResolver.Os os;
    private final String transportId;

    private volatile boolean running;
    private Thread thread;

    public SerialBridge(Supplier<Settings> settings, MessageHandler messages, SerialSession.Stats stats,
                        TransportRegistry registry, SerialPortProvider provider, SerialLog log,
                        long reconnectDelayMs, PortResolver.Os os) {
        this(settings, messages, stats, registry, provider, log, reconnectDelayMs, os, "usb");
    }

    /**
     * @param transportId the transport registry entry this bridge reports to: "usb", or
     *                    "ble:&lt;Deck&gt;" for a Deck that ble-bridge carries over the air
     */
    public SerialBridge(Supplier<Settings> settings, MessageHandler messages, SerialSession.Stats stats,
                        TransportRegistry registry, SerialPortProvider provider, SerialLog log,
                        long reconnectDelayMs, PortResolver.Os os, String transportId) {
        this.transportId = transportId;
        this.settings = settings;
        this.messages = messages;
        this.stats = stats;
        this.registry = registry;
        this.provider = provider;
        this.log = log;
        this.reconnectDelayMs = reconnectDelayMs;
        this.os = os;
    }

    /**
     * The settings of the host: the database, with the {@code ESP32_SERIAL} and
     * {@code ESP32_BAUD} environment variables and the command
     * line overriding the port and the baud rate.
     */
    public static Supplier<Settings> settingsFrom(ConfigService config, String portOverride, Integer baudOverride) {
        return () -> {
            Map<String, Object> serial = config.getSerialSettings();
            String port = firstText(portOverride, System.getenv("ESP32_SERIAL"),
                    serial.get("port") instanceof String text ? text : null, "auto");
            Long baudSetting = Values.validateInt(serial.get("baud"));
            Long baudEnvironment = Values.validateInt(System.getenv("ESP32_BAUD"));
            long baud = baudOverride != null ? baudOverride
                    : baudEnvironment != null ? baudEnvironment
                    : baudSetting != null ? baudSetting : 115200L;
            Object interval = config.getTelemetrySettings().get("interval_seconds");
            double seconds = interval instanceof Number number ? number.doubleValue() : 2.0;
            return new Settings(ConfigService.truthy(serial.get("enabled")), port.trim(),
                    (int) Math.min(baud, Integer.MAX_VALUE), seconds);
        };
    }

    /** Start the thread. Does nothing when it already runs. */
    public synchronized void start() {
        if (running) {
            return;
        }
        running = true;
        thread = new Thread(this::run, "control-deck-" + transportId);
        thread.setDaemon(true);
        thread.start();
    }

    /** Ask the thread to stop and wait for it (a few seconds at most). */
    public void stop() {
        Thread worker;
        synchronized (this) {
            running = false;
            worker = thread;
            thread = null;
        }
        if (worker != null) {
            try {
                worker.join(STOP_WAIT_MS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void run() {
        String lastWarning = null;
        boolean disabledReported = false;

        while (running) {
            SerialPortIO io = null;
            try {
                Settings current = settings.get();
                if (!current.enabled()) {
                    if (!disabledReported) {
                        log.info("The USB serial transport is switched off in the settings; waiting");
                        disabledReported = true;
                    }
                    pause(reconnectDelayMs);
                    continue;
                }
                disabledReported = false;

                registry.update(transportId, SerialSession.fields("state", "connecting", "port", null));
                String port = PortResolver.resolve(current.port(), this::listPortsUnchecked, os);
                if (current.baud() <= 0) {
                    throw new IllegalArgumentException("Serial baud rate must be positive");
                }
                PortInfo info = portInfo(port);
                fastPort = info != null && (info.vendorId() == PortInfo.VENDOR_ESPRESSIF
                        || info.vendorId() == PortInfo.VENDOR_PC_DECK);
                List<Integer> rates = baudRates(current.baud(), info != null && info.isUartBridge());
                int baud = rates.get(rateIndex % rates.size());
                io = provider.open(port, baud);

                registry.update(transportId, SerialSession.fields("state", "connected", "port", port, "last_error", null,
                        "last_activity", System.currentTimeMillis() / 1000L, "firmware", null,
                        "peer_protocol", null, "peer_capabilities", List.of()));
                log.info("Connected to " + port + " at " + baud + " baud");
                lastWarning = null;
                if (!communicate(io, current, rates.size() > 1)) {
                    rateIndex++;
                    throw new IOException("No Deck answered on " + port + " at " + baud + " baud; trying "
                            + rates.get(rateIndex % rates.size()));
                }
            } catch (Exception error) {
                if (!running) {
                    break;
                }
                String message = error.getMessage() == null ? error.toString() : error.getMessage();
                if (!message.equals(lastWarning)) {
                    log.warn(message);
                    lastWarning = message;
                }
                registry.update(transportId, SerialSession.fields("state", "error",
                        "last_error", Values.truncateCodePoints(message, 200)));
            } finally {
                if (io != null) {
                    io.close();
                }
            }

            try {
                pause(reconnectDelayMs);
            } catch (InterruptedIOException e) {
                break;
            }
        }

        registry.update(transportId, SerialSession.fields("state", "stopped"));
        log.info("Serial bridge stopped");
    }

    /** The port's entry; the same path may be listed twice (the system's and the Deck on this PC): ours wins. */
    private PortInfo portInfo(String port) {
        PortInfo found = null;
        for (PortInfo info : listPortsUnchecked()) {
            if (port.equals(info.path()) && (found == null || info.vendorId() == PortInfo.VENDOR_PC_DECK)) {
                found = info;
            }
        }
        return found;
    }

    /** The rates to try in turn: the configured one, then, behind a USB-UART chip, the boards' own. */
    public static List<Integer> baudRates(int configured, boolean uartBridge) {
        List<Integer> rates = new ArrayList<>();
        rates.add(configured);
        if (uartBridge) {
            for (int rate : UART_BAUD_RATES) {
                if (!rates.contains(rate)) {
                    rates.add(rate);
                }
            }
        }
        return rates;
    }

    private List<PortInfo> listPortsUnchecked() {
        try {
            return provider.listPorts();
        } catch (IOException e) {
            throw new IllegalStateException(e.getMessage(), e);
        }
    }

    private volatile boolean fastPort;
    private int sequence;  // of the frames sent, per connection (protocol 4's loss detection)
    private int rateIndex;  // kept across reconnections: the rate that answered is tried first
    private volatile SerialSession session;

    /**
     * Runs the session until the port closes. Returns false, at once, when the line may be at the
     * wrong rate ({@code giveUpWhenSilent}) and no Deck answered within {@link #ANSWER_WAIT_MS}.
     */
    private boolean communicate(SerialPortIO io, Settings current, boolean giveUpWhenSilent) throws IOException {
        LineSplitter lines = new LineSplitter(MAX_LINE_LENGTH);
        session = new SerialSession(messages, stats, current.intervalSeconds(),
                frame -> writeFrame(io, frame), fields -> registry.update(transportId, fields), log, System::nanoTime);
        byte[] buffer = new byte[1024];
        long openedMs = System.currentTimeMillis();

        while (running) {
            if (giveUpWhenSilent && !session.ready() && System.currentTimeMillis() - openedMs > ANSWER_WAIT_MS) {
                return false;
            }
            session.tick();
            int count = io.read(buffer);
            if (count > 0) {
                for (LineSplitter.Item item : lines.feed(buffer, count)) {
                    if (item instanceof LineSplitter.Frame frame) {
                        Map<String, Object> message = Codec4.decode(frame.frame());
                        if (message != null) {
                            session.onMessage(message);
                        }
                    } else if (item instanceof LineSplitter.Text text) {
                        log.debug("Deck: " + text.line());  // the debug build's "# ..." lines
                    } else {
                        log.debug("Damaged frame from the Deck dropped");
                    }
                }
            }
        }
        return true;
    }

    /** Frames go out in small pieces with a short pause, so the board's buffer never overflows. */
    private void writeFrame(SerialPortIO io, Map<String, Object> frame) throws IOException {
        Codec4.Out message = Codec4.encode(frame);
        if (message == null) {
            return;  // nothing the Deck takes
        }
        byte[] bytes = Frame4.encodeForLine(message.type(), 0, sequence++ & 0xFFFF, message.payload());
        boolean fast = fastPort && session != null && session.takesFastFrames();
        int chunk = fast ? WRITE_CHUNK_BYTES_FAST : WRITE_CHUNK_BYTES;
        int offset = 0;
        while (offset < bytes.length) {
            int length = Math.min(chunk, bytes.length - offset);
            io.write(bytes, offset, length);
            offset += length;
            if (offset < bytes.length) {
                pause(fast ? WRITE_PAUSE_MS_FAST : WRITE_PAUSE_MS);
            }
        }
    }

    /** Sleep, in slices short enough to notice a stop request. */
    private void pause(long millis) throws InterruptedIOException {
        long end = System.nanoTime() + millis * 1_000_000L;
        while (running) {
            long left = (end - System.nanoTime()) / 1_000_000L;
            if (left <= 0) {
                return;
            }
            try {
                Thread.sleep(Math.min(left, 50));
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new InterruptedIOException("Interrupted");
            }
        }
    }

    private static String firstText(String... candidates) {
        for (String candidate : candidates) {
            if (candidate != null && !candidate.isBlank()) {
                return candidate;
            }
        }
        return "auto";
    }
}
