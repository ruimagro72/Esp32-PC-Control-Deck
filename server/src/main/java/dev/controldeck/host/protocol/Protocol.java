package dev.controldeck.host.protocol;

import java.util.List;

/**
 * Constants of the transport-independent message contract (docs/04-PROTOCOL.md).
 *
 * The wire version stays 3. New message types are additive and only sent in
 * answer to a request or to a peer that announced the matching capability in
 * its {@code hello}, so a protocol-v3 firmware keeps working unchanged.
 */
public final class Protocol {

    private Protocol() {
    }

    public static final int VERSION = 4;

    /** Capabilities this host serves. */
    public static final List<String> CAPABILITIES = List.of("stats", "config", "macro", "history", "inventory");

    /** One limit on every link: a protocol 4 frame's payload (docs/04-PROTOCOL.md). */
    public static final int SERIAL_MAX_FRAME_BYTES = Frame4.MAX_PAYLOAD;
    public static final int HTTP_MAX_FRAME_BYTES = Frame4.MAX_PAYLOAD;

    public static final int DEFAULT_HISTORY_POINTS = 60;
    public static final int MIN_HISTORY_POINTS = 10;
    public static final int MAX_HISTORY_POINTS = 120;

    /** Message types a peer may send to the host. */
    public static final List<String> REQUEST_TYPES = List.of(
            "hello", "ping", "stats_request", "config_request", "macro",
            "history_request", "inventory_request", "board", "firmware_check", "catalog_request", "texts_request",
            "messages_request", "message_read", "transfer_request", "transfer_ack", "transfer_end", "upload_begin",
            "upload_data");
}
