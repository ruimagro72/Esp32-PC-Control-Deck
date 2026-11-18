package dev.controldeck.host.transport.serial;

import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.telemetry.MetricRegistry;
import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.LongSupplier;

/**
 * The conversation with the board over one open connection: the handshake, the
 * periodic stats and the answers to its requests. It does no input or output
 * itself; frames leave through a {@link FrameSink}, so it can be tested without a port.
 *
 * Protocol 4 only (docs/04-PROTOCOL.md): the messages arrive and leave as binary frames
 * (the bridge decodes and encodes them with Codec4); a Deck that says hello can do all of it.
 *
 * One instance per connection, used by one thread.
 */
public final class SerialSession {

    /** What every protocol 4 Deck can take (protocol 3 announced it piece by piece). */
    static final List<String> DECK_CAPABILITIES = List.of("history", "inventory", "wifi_token", "key_image");

    private static final long PING_EVERY_NANOS = 1_000_000_000L;
    private static final long DUPLICATE_HELLO_NANOS = 3_000_000_000L;
    /** How often the macro list is compared with the one the board has. */
    private static final long CONFIG_CHECK_NANOS = 2_000_000_000L;
    private static final int MAX_FIRMWARE_LENGTH = 40;

    /** Where outgoing frames go. */
    @FunctionalInterface
    public interface FrameSink {
        void send(Map<String, Object> frame) throws IOException;
    }

    /** Where connection facts are recorded (the "usb" entry of the transport registry). */
    @FunctionalInterface
    public interface StateSink {
        void update(Map<String, Object> fields);
    }

    /** The current stats frame; a sample younger than {@code maxAgeSeconds} may be reused. */
    @FunctionalInterface
    public interface Stats {
        Map<String, Object> frame(double maxAgeSeconds);
    }

    private final MessageHandler messages;
    private final Stats stats;
    private final long statsEveryNanos;
    private final FrameSink out;
    private final StateSink state;
    private final SerialLog log;
    private final LongSupplier nanoClock;

    private static final int KEY_FRAMES_PER_TICK = 4;
    private List<String> peerCaps = List.of();
    private String deckId;
    private boolean ready;
    private boolean handshaken;
    private long handshakeAt;
    private boolean pinged;
    private long nextPingAt;
    private boolean statsScheduled;
    private long nextStatsAt;
    private long lastActivitySecond;
    private String lastStatsWarning;
    private Map<String, Object> lastConfig;
    private long nextConfigCheckAt;
    // The Deck said it now talks to the host another way (Wi-Fi): the line stays open and
    // quiet (no pings, no stats) until it says hello again.
    private boolean elsewhere;

    public SerialSession(MessageHandler messages, Stats stats, double intervalSeconds, FrameSink out,
                         StateSink state, SerialLog log, LongSupplier nanoClock) {
        this.messages = messages;
        this.stats = stats;
        this.statsEveryNanos = (long) (Math.max(0.5, intervalSeconds) * 1_000_000_000L);
        this.out = out;
        this.state = state;
        this.log = log;
        this.nanoClock = nanoClock;
    }

    /** True once the board has answered (hello or pong). */
    public boolean ready() {
        return ready;
    }

    /** Capabilities announced by the board; empty for a plain protocol-v3 firmware. */
    public List<String> peerCapabilities() {
        return peerCaps;
    }

    /** Call often (every few tens of milliseconds): pings until the board answers, then sends stats on schedule. */
    public void tick() throws IOException {
        long now = nanoClock.getAsLong();
        sendConsoleInput();
        if (elsewhere) {
            return;
        }
        // Until the board answers, and after a pong with no hello (its hello was lost, as while
        // the port tried another speed): the board answers a ping with its hello until greeted.
        if ((!ready || !handshaken) && (!pinged || now - nextPingAt >= 0)) {
            pinged = true;
            nextPingAt = now + PING_EVERY_NANOS;
            send(MessageHandler.map("type", "ping"));
        }
        if (ready && (!statsScheduled || now - nextStatsAt >= 0)) {
            sendStats(statsEveryNanos / 1e9);
        }
        if (ready && deckId != null) {
            for (Map<String, Object> frame : messages.openAction().take(deckId, KEY_FRAMES_PER_TICK)) {
                send(frame);
            }
        }
        if (ready && lastConfig != null && now - nextConfigCheckAt >= 0) {
            resendConfigurationIfChanged(now);
        }
    }

    /**
     * The lines typed on the Deck's console (docs/CONSOLE.md). Also while the Deck talks to the
     * server another way: the cable stays a backoffice line, and the Deck answers on it.
     */
    private void sendConsoleInput() throws IOException {
        if (deckId == null && !ready && !elsewhere) {
            return;  // no Deck has spoken on this line yet
        }
        for (Map<String, Object> frame : messages.console().take(deckId)) {
            send(frame);
        }
    }

    /** A macro created, changed or deleted on the host reaches the board without a reconnect. */
    private void resendConfigurationIfChanged(long now) throws IOException {
        nextConfigCheckAt = now + CONFIG_CHECK_NANOS;
        Map<String, Object> frame;
        try {
            frame = messages.configFrame();
        } catch (RuntimeException error) {
            return; // the database is busy or gone: try again at the next check
        }
        if (!frame.equals(lastConfig)) {
            log.info("Macro list changed: sending it to the board");
            send(frame);
            lastConfig = frame;
        }
    }

    /** Answer one message from the Deck (a decoded frame). Unknown types are ignored. */
    public void onMessage(Map<String, Object> message) throws IOException {
        String type = message.get("type") instanceof String s ? s : "";
        touchActivity();

        if (type.equals("console_output")) {
            String from = message.get("deck") instanceof String id && !id.isEmpty() ? id : deckId;
            messages.console().output(from, ((Number) message.get("id")).intValue(),
                    ((Number) message.get("flags")).intValue(), String.valueOf(message.get("text")));
            return;
        }
        if (type.equals("transport")) {
            String to = message.get("to") instanceof String s ? Values.truncateCodePoints(s, 16) : "";
            if (!to.equals("usb") && !elsewhere) {
                elsewhere = true;
                ready = false;
                state.update(fields("peer_transport", to));
                log.info("The Deck now talks to the host over " + to
                        + "; the USB line stays open and quiet until it says hello again");
            }
            return;
        }
        // A Deck on Wi-Fi that cannot reach the host asks on its cable, without leaving Wi-Fi.
        if (type.equals("wifi_token_request") && message.get("id") instanceof String id) {
            if (deckId == null && id.matches("^[0-9A-Za-z:_-]{1,32}$")) {
                deckId = id;  // its console can reach it on this cable
            }
            send(messages.wifiTokenFrame(id));
            return;
        }
        if (type.equals("hello")) {
            if (elsewhere) {
                elsewhere = false;
                state.update(fields("peer_transport", "usb"));
                log.info("The Deck is back on USB");
            }
            onHello(message);
            ready = true;
            return;
        }
        if (type.equals("pong")) {
            if (!ready) {
                log.info("Firmware ping response received");
                sendConfiguration();
                sendStats(0.5);
                ready = true;
            }
            return;
        }
        if (type.equals("error")) {  // a problem of the Deck's own (no touch, no microSD card...)
            log.info("Deck reports " + message.get("code") + ": " + message.get("detail"));
            return;
        }
        if (type.equals("rebooting")) {
            log.debug("Firmware reported " + type);
            return;
        }
        if (!Protocol.REQUEST_TYPES.contains(type)) {
            return; // stats echoes and unknown types are ignored
        }

        boolean isMacro = type.equals("macro");
        List<Map<String, Object>> replies = messages.handle(message, Protocol.SERIAL_MAX_FRAME_BYTES);
        // The macro service reports each event on the console (what ran, or why not).
        // A board that did not announce macro_result does not expect an answer.
        if (isMacro && !peerCaps.contains("macro_result")) {
            return;
        }

        for (Map<String, Object> reply : replies) {
            if ("error".equals(reply.get("type")) && peerCaps.isEmpty()) {
                continue; // never send frames a plain v3 firmware does not know
            }
            send(reply);
        }
    }

    /**
     * A Deck that can take frames quickly: one that announced key images also has the serial
     * buffer for them. Older firmware keeps a small one, which a fast stream overflows.
     */
    public boolean takesFastFrames() {
        return peerCaps.contains("key_image");
    }

    private void onHello(Map<String, Object> message) throws IOException {
        List<String> caps = DECK_CAPABILITIES;

        // The board answers a ping with a hello while it has not yet processed our
        // host_hello, so a second hello often follows a handshake that has just
        // completed. Repeating it would resend the configuration, the stats and the
        // hardware inventory (about 2 KB over the port) for nothing. A real reboot
        // takes longer than the window below.
        long now = nanoClock.getAsLong();
        if (handshaken && now - handshakeAt < DUPLICATE_HELLO_NANOS && peerCaps.equals(caps)) {
            return;
        }
        handshaken = true;
        handshakeAt = now;
        peerCaps = List.copyOf(caps);
        deckId = message.get("id") instanceof String id ? id : null;

        String firmware = message.get("firmware") == null ? null
                : Values.truncateCodePoints(String.valueOf(message.get("firmware")), MAX_FIRMWARE_LENGTH);
        Long protocol = Values.validateInt(message.get("protocol"));
        state.update(fields("firmware", firmware, "peer_protocol", protocol, "peer_capabilities", peerCaps));
        log.info("Firmware handshake received: firmware " + firmware + ", protocol " + protocol + ", "
                + peerCaps.size() + " capabilities");

        if (!peerCaps.isEmpty()) {
            send(messages.hostHello());
        }
        // A Deck on its cable gets its own Wi-Fi token: no one has to type it.
        if (peerCaps.contains("wifi_token") && message.get("id") instanceof String deckId) {
            Map<String, Object> access = messages.wifiTokenFrame(deckId);
            send(access);
            log.info(Boolean.TRUE.equals(access.get("available"))
                    ? "Wi-Fi access sent to the Deck " + Values.truncateCodePoints(deckId, 32) + " (its own token)"
                    : "No Wi-Fi access for the Deck: " + access.get("reason"));
        }
        sendConfiguration();
        if (peerCaps.contains("inventory")) {
            sendInventory();
        }
        sendStats(0.5);
    }

    private void sendConfiguration() throws IOException {
        Map<String, Object> frame = messages.configFrame();
        send(frame);
        lastConfig = frame;
        nextConfigCheckAt = nanoClock.getAsLong() + CONFIG_CHECK_NANOS;
    }

    /**
     * The hardware characteristics are pushed once at the start of the connection to a
     * peer that announced the {@code inventory} capability; the firmware keeps them in
     * RAM only and never asks again.
     */
    private void sendInventory() throws IOException {
        for (String component : MetricRegistry.COMPONENTS) {
            Map<String, Object> request = MessageHandler.map("type", "inventory_request", "component", component);
            for (Map<String, Object> frame : messages.handle(request, Protocol.SERIAL_MAX_FRAME_BYTES)) {
                if ("inventory".equals(frame.get("type"))) {
                    send(frame);
                }
            }
        }
    }

    private void sendStats(double maxAgeSeconds) throws IOException {
        // Schedule the next one first: a sampling failure must not turn into a retry on every tick.
        statsScheduled = true;
        nextStatsAt = nanoClock.getAsLong() + statsEveryNanos;
        Map<String, Object> frame;
        try {
            frame = stats.frame(maxAgeSeconds);
        } catch (RuntimeException error) {
            // Said once per distinct reason, not on every interval.
            String warning = "Could not sample the telemetry: " + error;
            if (!warning.equals(lastStatsWarning)) {
                log.warn(warning);
                lastStatsWarning = warning;
            }
            return;
        }
        lastStatsWarning = null;
        send(frame);
    }

    private void send(Map<String, Object> frame) throws IOException {
        out.send(frame);
        touchActivity();
    }

    /** Record the time of the last traffic, at most once per second. */
    private void touchActivity() {
        long second = System.currentTimeMillis() / 1000L;
        if (second != lastActivitySecond) {
            lastActivitySecond = second;
            state.update(fields("last_activity", second));
        }
    }

    public static Map<String, Object> fields(Object... pairs) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            map.put((String) pairs[i], pairs[i + 1]);
        }
        return map;
    }
}
