package dev.controldeck.host;

import java.util.List;

/** Values shared by the host's production code. Change a default here, not in each caller. */
public final class Defaults {
    /** Default API port (saved in config as api.port, overridable with --port). */
    public static final int API_PORT = 8080;
    /** Default API bind address: this computer only. */
    public static final String LOOPBACK_BIND = "127.0.0.1";
    /** Hosts that count as this computer (used by the bind check and the WebSocket bridge). */
    public static final List<String> LOOPBACK_HOSTS = List.of("127.0.0.1", "localhost", "::1");
    /** Delay before the serial link retries after a failure, in milliseconds. */
    public static final long RECONNECT_DELAY_MS = 2000;
    /** Maximum open uploads or transfers at once. */
    public static final int MAX_OPEN_TRANSFERS = 8;

    private Defaults() {
    }
}
