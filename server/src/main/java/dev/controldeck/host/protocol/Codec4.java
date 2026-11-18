package dev.controldeck.host.protocol;

import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Protocol 4's messages (docs/04-PROTOCOL.md) to and from what the server's logic handles:
 * a Deck's frame becomes the message map {@link MessageHandler} takes, and each reply map
 * becomes its binary layout. Nothing of this is JSON on the link.
 */
public final class Codec4 {

    // Deck to server
    public static final int HELLO = 0x01, PONG = 0x02, BOARD = 0x03, STATS_REQUEST = 0x04, CONFIG_REQUEST = 0x05,
            MACRO = 0x06, HISTORY_REQUEST = 0x07, INVENTORY_REQUEST = 0x08, TRANSPORT = 0x09,
            WIFI_TOKEN_REQUEST = 0x0A, FIRMWARE_CHECK = 0x0B, CATALOG_REQUEST = 0x0C, TEXTS_REQUEST = 0x0D,
            REBOOTING = 0x0E, DISCOVER = 0x0F, MESSAGES_REQUEST = 0x10, MESSAGE_READ = 0x11,
            TRANSFER_REQUEST = 0x12, TRANSFER_ACK = 0x13, TRANSFER_END = 0x14, UPLOAD_BEGIN = 0x15, UPLOAD_DATA = 0x16,
            DECK_ERROR = 0x3F;
    /** The Deck's console (docs/CONSOLE.md): its answers and live log lines, and the lines typed. */
    public static final int CONSOLE_OUTPUT = 0x17, CONSOLE_INPUT = 0x52;
    // Server to Deck
    public static final int HOST_HELLO = 0x40, PING = 0x41, STATS = 0x42, CONFIG = 0x43, INVENTORY = 0x44,
            HISTORY = 0x45, KEY_IMAGE = 0x46, KEY_MORE = 0x47, WIFI_TOKEN = 0x48, RECALIBRATE = 0x49,
            FIRMWARE_OFFER = 0x4A, CATALOG = 0x4B, TEXTS = 0x4C, HOST_HERE = 0x4D, MESSAGES = 0x4E,
            TRANSFER_BEGIN = 0x4F, TRANSFER_DATA = 0x50, UPLOAD_ACK = 0x51, ERROR = 0x7F;

    public static final List<String> COMPONENTS = List.of("cpu", "ram", "gpu", "network", "disk");
    public static final List<String> TRANSPORTS = List.of("usb", "wifi", "ble");
    private static final List<String> ERROR_CODES = List.of("", "unknown_type", "invalid_request", "internal_error",
            "not_allowed");
    private static final List<String> CATALOG_KINDS = List.of("pct", "temp", "rate", "freq", "power", "size");

    /** The old pages' fixed values, in the order of the stats frame. */
    public static final List<String> STATS_FIELDS = List.of("cpu", "cpu_temp", "system_temp", "ram", "ram_used_gb",
            "ram_total_gb", "gpu", "vram", "vram_used_gb", "vram_total_gb", "gpu_temp", "gpu_clock_mhz",
            "gpu_power_watts", "disk_used", "disk_read_mbs", "disk_write_mbs", "disk_temp", "net_down_mbps",
            "net_up_mbps");

    /** A frame for a Deck: its type and payload (the sequence and the line are the sender's). */
    public record Out(int type, byte[] payload) {
    }

    private Codec4() {
    }

    // -- from the Deck ---------------------------------------------------------------

    /** A Deck's frame as a message map; null for a type the server does not take. */
    public static Map<String, Object> decode(Frame4.Frame frame) {
        Wire.Reader in = new Wire.Reader(frame.payload());
        Map<String, Object> m = new LinkedHashMap<>();
        switch (frame.type()) {
            case HELLO -> {
                m.put("type", "hello");
                m.put("protocol", in.u8());
                m.put("id", in.str());
                m.put("firmware", in.str());
                m.put("board", in.str());
            }
            case PONG -> m.put("type", "pong");
            case BOARD -> decodeBoard(in, m);
            case STATS_REQUEST -> {
                m.put("type", "stats_request");
                m.put("deck", in.str());
            }
            case CONFIG_REQUEST -> m.put("type", "config_request");
            case MACRO -> {
                m.put("type", "macro");
                m.put("id", in.u8());
                m.put("deck", in.str());
            }
            case HISTORY_REQUEST -> {
                m.put("type", "history_request");
                m.put("metric", in.str());
                m.put("minutes", in.u8());
                m.put("points", in.u16());
            }
            case INVENTORY_REQUEST -> {
                m.put("type", "inventory_request");
                int c = in.u8();
                m.put("component", c < COMPONENTS.size() ? COMPONENTS.get(c) : "");
            }
            case TRANSPORT -> {
                m.put("type", "transport");
                int t = in.u8();
                m.put("to", t < TRANSPORTS.size() ? TRANSPORTS.get(t) : "");
            }
            case WIFI_TOKEN_REQUEST -> {
                m.put("type", "wifi_token_request");
                m.put("id", in.str());
            }
            case FIRMWARE_CHECK -> {
                m.put("type", "firmware_check");
                m.put("board", in.str());
                m.put("firmware", in.str());
            }
            case CATALOG_REQUEST -> {
                m.put("type", "catalog_request");
                m.put("from", in.u16());
            }
            case TEXTS_REQUEST -> {
                m.put("type", "texts_request");
                m.put("from", in.u16());
                m.put("deck", in.str());
            }
            case REBOOTING -> m.put("type", "rebooting");
            case DISCOVER -> m.put("type", "discover");
            case MESSAGES_REQUEST -> {
                m.put("type", "messages_request");
                m.put("deck", in.str());
                m.put("after", in.u32());
            }
            case MESSAGE_READ -> {
                m.put("type", "message_read");
                m.put("deck", in.str());
                m.put("id", in.u32());
            }
            case TRANSFER_REQUEST -> {
                m.put("type", "transfer_request");
                m.put("kind", in.u8());
                m.put("deck", in.str());
                m.put("what", in.str());
            }
            case TRANSFER_ACK -> {
                m.put("type", "transfer_ack");
                m.put("id", in.u16());
                m.put("next", in.u32());
            }
            case TRANSFER_END -> {
                m.put("type", "transfer_end");
                m.put("id", in.u16());
                m.put("status", in.u8());
            }
            case UPLOAD_BEGIN -> {
                m.put("type", "upload_begin");
                m.put("id", in.u16());
                m.put("kind", in.u8());
                m.put("size", in.u32());
                m.put("deck", in.str());
                m.put("to", in.str());
            }
            case UPLOAD_DATA -> {
                m.put("type", "upload_data");
                m.put("id", in.u16());
                m.put("deck", in.str());
                m.put("offset", in.u32());
                m.put("bytes", in.bytes(in.left()));
            }
            case CONSOLE_OUTPUT -> {
                m.put("type", "console_output");
                m.put("deck", in.str());
                m.put("id", in.u16());
                m.put("flags", in.u8());
                m.put("text", new String(in.bytes(in.u16()), java.nio.charset.StandardCharsets.UTF_8));
            }
            case DECK_ERROR -> {
                m.put("type", "error");
                int code = in.u8();
                m.put("code", code < ERROR_CODES.size() ? ERROR_CODES.get(code) : "");
                m.put("detail", in.str());
            }
            default -> {
                return null;
            }
        }
        return m;
    }

    private static void decodeBoard(Wire.Reader in, Map<String, Object> m) {
        m.put("type", "board");
        m.put("id", in.str());
        m.put("board", in.str());
        m.put("firmware", in.str());
        m.put("chip", in.str());
        m.put("rev", (long) in.u8());
        m.put("cores", (long) in.u8());
        m.put("mhz", (long) in.u16());
        m.put("flash", in.u32());
        m.put("flash_build", in.u32());
        m.put("psram", in.u32());
        int flags = in.u8();
        m.put("psram_in_package", (flags & 1) != 0);
        m.put("rgb_led", (flags & 2) != 0);
        List<Object> radios = new ArrayList<>();
        if ((flags & 4) != 0) {
            radios.add("wifi");
        }
        if ((flags & 8) != 0) {
            radios.add("ble");
        }
        if ((flags & 16) != 0) {
            radios.add("bt");
        }
        m.put("radios", radios);
        m.put("screen", List.of((long) in.u16(), (long) in.u16()));
        m.put("display", in.str());
        m.put("touch", in.str());
        m.put("sd", in.str());
        m.put("audio", in.str());
        long battery = in.u16();
        if ((flags & 32) != 0) {
            m.put("battery_mv", battery);
        }
        m.put("heap", List.of(in.u32(), in.u32(), in.u32()));
        m.put("nvs", List.of((long) in.u16(), (long) in.u16(), (long) in.u16()));
        m.put("keypad", Map.of("rows", in.u8(), "columns", in.u8(), "w", in.u16(), "h", in.u16()));
    }

    // -- to the Deck -----------------------------------------------------------------

    /** A reply map in its binary layout; null for one a Deck does not take (macro_result). */
    public static Out encode(Map<String, Object> m) {
        String type = String.valueOf(m.get("type"));
        Wire.Writer w = new Wire.Writer();
        switch (type) {
            case "host_hello" -> {
                w.u8(4);
                Object interval = m.get("interval_seconds");
                w.u16((int) Math.round((interval instanceof Number n ? n.doubleValue() : 2.0) * 1000));
                List<?> windows = m.get("history_windows") instanceof List<?> l ? l : List.of();
                w.u8(windows.size());
                for (Object minutes : windows) {
                    w.u8(minutes instanceof Number n ? n.intValue() : 0);
                }
                w.u32(m.get("server_time") instanceof Number n ? n.longValue() : 0);
                return new Out(HOST_HELLO, w.toBytes());
            }
            case "ping" -> {
                return new Out(PING, new byte[0]);
            }
            case "stats" -> {
                return new Out(STATS, stats(m, w));
            }
            case "config" -> {
                List<?> macros = m.get("macros") instanceof List<?> l ? l : List.of();
                w.u8(macros.size());
                for (Object item : macros) {
                    Map<?, ?> macro = (Map<?, ?>) item;
                    w.u8(macro.get("id") instanceof Number n ? n.intValue() : 0);
                    w.str(String.valueOf(macro.get("label")));
                }
                return new Out(CONFIG, w.toBytes());
            }
            case "inventory" -> {
                int component = COMPONENTS.indexOf(String.valueOf(m.get("component")));
                List<String> lines = inventoryLines(m.get("data") instanceof Map<?, ?> d ? d : Map.of());
                boolean cut = Boolean.TRUE.equals(m.get("truncated")) || lines.size() > INVENTORY_LINES;
                w.u8(Math.max(component, 0)).u8(cut ? 1 : 0);
                List<String> shown = lines.subList(0, Math.min(lines.size(), INVENTORY_LINES));
                w.u8(shown.size());
                shown.forEach(w::str);
                return new Out(INVENTORY, w.toBytes());
            }
            case "history" -> {
                w.str(String.valueOf(m.get("metric"))).str(String.valueOf(m.getOrDefault("unit", "")));
                w.u8(number(m.get("minutes"))).u16(number(m.get("step_s")));
                w.u32(m.get("end") instanceof Number n ? n.longValue() : 0);
                List<?> values = m.get("values") instanceof List<?> l ? l : List.of();
                w.u16(values.size());
                for (Object v : values) {
                    w.f32(v instanceof Number n ? n.doubleValue() : null);
                }
                return new Out(HISTORY, w.toBytes());
            }
            case "key_image" -> {
                boolean clear = Boolean.TRUE.equals(m.get("clear"));
                w.u8(number(m.get("position"))).u8(clear ? 1 : 0).u16(number(m.get("y")));
                if (!clear && m.get("data") instanceof String rows) {
                    w.bytes(Base64.getDecoder().decode(rows));
                }
                return new Out(KEY_IMAGE, w.toBytes());
            }
            case "key_more" -> {
                return new Out(KEY_MORE, new byte[0]);
            }
            case "wifi_token" -> {
                boolean available = Boolean.TRUE.equals(m.get("available"));
                w.u8(available ? 1 : 0);
                if (available) {
                    w.str(String.valueOf(m.get("token"))).u16(number(m.get("port")));
                    List<?> addresses = m.get("addresses") instanceof List<?> l ? l : List.of();
                    w.u8(addresses.size());
                    addresses.forEach(a -> w.str(String.valueOf(a)));
                } else {
                    w.str(String.valueOf(m.getOrDefault("reason", "")));
                }
                return new Out(WIFI_TOKEN, w.toBytes());
            }
            case "recalibrate" -> {
                return new Out(RECALIBRATE, new byte[0]);
            }
            case "firmware_offer" -> {
                boolean available = Boolean.TRUE.equals(m.get("available"));
                w.u8(available ? 1 : 0);
                if (available) {
                    w.str(String.valueOf(m.get("version")));
                    w.u32(m.get("size") instanceof Number n ? n.longValue() : 0);
                    w.str(String.valueOf(m.get("md5"))).str(String.valueOf(m.get("path")));
                }
                return new Out(FIRMWARE_OFFER, w.toBytes());
            }
            case "catalog" -> {
                w.u32(hex(m.get("cv"))).u16(number(m.get("from"))).u16(number(m.get("total")));
                List<?> rows = m.get("m") instanceof List<?> l ? l : List.of();
                w.u8(rows.size());
                for (Object item : rows) {
                    List<?> row = (List<?>) item;
                    for (int i = 0; i < 5; i++) {
                        w.str(String.valueOf(row.get(i)));
                    }
                    w.u8(Math.max(0, CATALOG_KINDS.indexOf(String.valueOf(row.get(5)))));
                    w.f32(row.get(6) instanceof Number n ? n.doubleValue() : null);
                    w.u16(number(row.get(7)));
                }
                return new Out(CATALOG, w.toBytes());
            }
            case "texts" -> {
                w.u32(hex(m.get("tv"))).u32(hex(m.get("ids"))).str(String.valueOf(m.get("lang")));
                w.u16(number(m.get("from"))).u16(number(m.get("total")));
                List<?> texts = m.get("t") instanceof List<?> l ? l : List.of();
                w.u8(texts.size());
                for (Object text : texts) {
                    str16(w, String.valueOf(text));
                }
                return new Out(TEXTS, w.toBytes());
            }
            case "host_here" -> {
                w.u16(number(m.get("port"))).str(String.valueOf(m.getOrDefault("name", "")));
                return new Out(HOST_HERE, w.toBytes());
            }
            case "messages" -> {
                List<?> items = m.get("messages") instanceof List<?> l ? l : List.of();
                w.u8(items.size());
                for (Object item : items) {
                    Map<?, ?> msg = (Map<?, ?>) item;
                    w.u32(msg.get("id") instanceof Number n ? n.longValue() : 0);
                    w.u32(msg.get("time") instanceof Number n ? n.longValue() : 0);
                    w.u8("voice".equals(msg.get("kind")) ? 1 : 0);
                    w.u8(Boolean.TRUE.equals(msg.get("read")) ? 1 : 0);
                    w.str(String.valueOf(msg.get("from")));
                    str16(w, String.valueOf(msg.get("text")));
                }
                return new Out(MESSAGES, w.toBytes());
            }
            case "transfer_begin" -> {
                w.u16(number(m.get("id"))).u8(number(m.get("kind")));
                w.u32(m.get("size") instanceof Number n ? n.longValue() : 0).str(String.valueOf(m.get("md5")));
                return new Out(TRANSFER_BEGIN, w.toBytes());
            }
            case "transfer_data" -> {
                w.u16(number(m.get("id"))).u32(m.get("offset") instanceof Number n ? n.longValue() : 0);
                if (m.get("bytes") instanceof byte[] bytes) {
                    w.bytes(bytes);
                }
                return new Out(TRANSFER_DATA, w.toBytes());
            }
            case "upload_ack" -> {
                w.u16(number(m.get("id"))).u32(m.get("next") instanceof Number n ? n.longValue() : 0);
                return new Out(UPLOAD_ACK, w.toBytes());
            }
            case "console_input" -> {
                w.u16(number(m.get("id"))).str(String.valueOf(m.get("line")));
                return new Out(CONSOLE_INPUT, w.toBytes());
            }
            case "error" -> {
                int code = ERROR_CODES.indexOf(String.valueOf(m.get("code")));
                w.u8(code < 0 ? 3 : code).str(String.valueOf(m.getOrDefault("detail", "")));
                return new Out(ERROR, w.toBytes());
            }
            default -> {
                return null;  // macro_result and the like: the console says it, the Deck needs nothing
            }
        }
    }

    private static byte[] stats(Map<String, Object> m, Wire.Writer w) {
        w.u32(m.get("clock_seconds") instanceof Number n ? n.longValue() : 0);
        String date = m.get("date") instanceof String d ? d : "";
        int day = 0, month = 0;
        if (date.matches("\\d{2}/\\d{2}")) {
            day = Integer.parseInt(date.substring(0, 2));
            month = Integer.parseInt(date.substring(3, 5));
        }
        w.u8(day).u8(month);
        for (String field : STATS_FIELDS) {
            w.f32(m.get(field) instanceof Number n ? n.doubleValue() : null);
        }
        w.u32(hex(m.get("cv")));
        List<?> values = m.get("v") instanceof List<?> l ? l : List.of();
        int n = Math.min(values.size(), 255);
        w.u8(n);
        for (int i = 0; i < n; i++) {
            w.f32(values.get(i) instanceof Number v ? v.doubleValue() : null);
        }
        return w.toBytes();
    }

    /** Lines the Deck shows on a component page, at most this many (its screen). */
    public static final int INVENTORY_LINES = 8;
    private static final int INVENTORY_COLUMNS = 52;

    /**
     * The inventory as the Deck shows it (it used to format it itself): "key: value", a list
     * of entries (disks, interfaces) as a heading and one line per entry, N/A for null.
     */
    public static List<String> inventoryLines(Map<?, ?> data) {
        List<String> lines = new ArrayList<>();
        for (Map.Entry<?, ?> pair : data.entrySet()) {
            String key = String.valueOf(pair.getKey()).replace('_', ' ');
            Object value = pair.getValue();
            if (value instanceof List<?> list && !list.isEmpty() && list.get(0) instanceof Map<?, ?>) {
                lines.add(cut(key + ":"));
                for (Object item : list) {
                    StringBuilder line = new StringBuilder("- ");
                    boolean first = true;
                    for (Object field : ((Map<?, ?>) item).values()) {
                        if (!first) {
                            line.append(' ');
                        }
                        first = false;
                        line.append(text(field));
                    }
                    lines.add(cut(line.toString()));
                }
            } else {
                lines.add(cut(key + ": " + text(value)));
            }
        }
        return lines;
    }

    private static String text(Object v) {
        if (v == null) {
            return "N/A";
        }
        if (v instanceof Boolean b) {
            return b ? "yes" : "no";
        }
        if (v instanceof List<?> list) {
            if (list.isEmpty()) {
                return "-";
            }
            List<String> parts = new ArrayList<>();
            list.forEach(item -> parts.add(text(item)));
            return String.join(",", parts);
        }
        if (v instanceof Number n) {
            double d = n.doubleValue();
            return d == Math.floor(d) && Math.abs(d) < 1e9 ? Long.toString((long) d)
                    : String.format(Locale.ROOT, "%.1f", d);
        }
        return String.valueOf(v);
    }

    private static String cut(String line) {
        return line.length() <= INVENTORY_COLUMNS ? line : line.substring(0, INVENTORY_COLUMNS);
    }

    private static void str16(Wire.Writer w, String text) {
        byte[] bytes = text.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        w.u16(bytes.length).bytes(bytes);
    }

    private static int number(Object v) {
        return v instanceof Number n ? n.intValue() : 0;
    }

    private static long hex(Object v) {
        if (v instanceof String s && s.matches("[0-9a-fA-F]{1,8}")) {
            return Long.parseLong(s, 16);
        }
        return 0;
    }
}
