package dev.controldeck.host.protocol;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.inbox.Voice;
import dev.controldeck.host.macro.MacroExecutor;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.pack.PackStore;
import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.telemetry.MetricRegistry;
import dev.controldeck.host.telemetry.Sampler;
import dev.controldeck.host.transfer.Transfers;
import dev.controldeck.host.transfer.Uploads;
import dev.controldeck.host.texts.TextTables;
import dev.controldeck.host.transport.openaction.OpenActionBridge;
import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * One handler for every transport. USB, Wi-Fi and BLE decode their own framing and
 * hand the parsed object to {@link #handle}; the replies are plain maps that the
 * transport frames again. This keeps the logical schema identical across transports.
 *
 * Invalid input never throws: it yields an {@code error} frame with a stable code,
 * and the detail never echoes the offending message.
 *
 * A {@code macro} event runs the configured macro and answers {@code macro_result}:
 * {@code ok} is true only when the action really ran; otherwise {@code detail} says why.
 */
public final class MessageHandler {

    private final ConfigService config;
    private final Sampler sampler;
    private final Inventory inventory;
    private final MacroService macros;
    private final OpenActionBridge openAction = new OpenActionBridge();
    // The description each Deck sent ("board" frame), by Deck id, newest last.
    private final Map<String, Map<String, Object>> boards = new java.util.concurrent.ConcurrentHashMap<>();
    // Where the API really listens (--bind / --port win over the settings); null/0 = settings.
    private volatile FirmwareStore firmware;
    private volatile PackStore packs;
    private volatile MessageStore inbox;
    private final Transfers transfers = new Transfers();
    private final Uploads uploads = new Uploads(Voice.MAX_BYTES);
    private final dev.controldeck.host.console.ConsoleHub console = new dev.controldeck.host.console.ConsoleHub();
    private final TextTables texts = new TextTables();
    private volatile String listenBind;
    private volatile int listenPort;

    /** A handler whose macro events run on this computer. */
    public MessageHandler(ConfigService config, Sampler sampler, Inventory inventory) {
        this(config, sampler, inventory, new MacroService(config, new MacroExecutor()));
    }

    public MessageHandler(ConfigService config, Sampler sampler, Inventory inventory, MacroService macros) {
        this.config = config;
        this.sampler = sampler;
        this.inventory = inventory;
        this.macros = macros;
    }

    /**
     * @param message       decoded request
     * @param maxFrameBytes largest reply frame the peer can accept
     * @return reply frames (possibly empty)
     */
    public List<Map<String, Object>> handle(Map<String, Object> message, int maxFrameBytes) {
        Object type = message.get("type");
        if (!(type instanceof String name) || name.isEmpty()) {
            return List.of(error("missing_type", "Message has no \"type\""));
        }

        try {
            return switch (name) {
                case "hello" -> List.of(hostHello());
                case "ping" -> List.of(map("type", "pong"));
                case "stats_request" -> withKeyImages(message, sampler.sample(1.0).frame());
                case "config_request" -> List.of(configFrame());
                case "macro" -> List.of(macroOrKey(message));
                case "error" -> {  // the Deck reports a problem of its own (no touch, no microSD card...)
                    Log.info("[deck] " + text(message, "code") + ": " + text(message, "detail"));
                    yield List.of();
                }
                case "history_request" -> List.of(historyFrame(message, maxFrameBytes));
                case "inventory_request" -> List.of(inventoryFrame(message, maxFrameBytes));
                case "firmware_check" -> List.of(firmwareOffer(message));
                case "catalog_request" -> List.of(catalogFrame(message, maxFrameBytes));
                case "texts_request" -> List.of(textsFrame(message, maxFrameBytes));
                case "messages_request" -> List.of(messagesFrame(message, maxFrameBytes));
                case "transfer_request" -> {
                    List<Map<String, Object>> frames = transferStart(message);
                    if (frames == null) {
                        throw new IllegalArgumentException("nothing to send for this request");
                    }
                    yield frames;
                }
                case "transfer_ack" -> transfers.ack(((Number) message.get("id")).intValue(),
                        ((Number) message.get("next")).longValue());
                case "transfer_end" -> {
                    transfers.end(((Number) message.get("id")).intValue());
                    Log.info("[transfer] " + message.get("id") + " ended, status " + message.get("status")
                            + (Integer.valueOf(0).equals(message.get("status")) ? " (kept and checked)" : " (not kept)"));
                    yield List.of();
                }
                case "upload_begin" -> List.of(uploads.begin(text(message, "deck"), number(message, "id"),
                        number(message, "kind"), ((Number) message.get("size")).longValue(), text(message, "to")));
                case "upload_data" -> {
                    Uploads.Done[] done = new Uploads.Done[1];
                    Map<String, Object> ack = uploads.data(text(message, "deck"), number(message, "id"),
                            ((Number) message.get("offset")).longValue(), (byte[]) message.get("bytes"), done);
                    if (done[0] != null) {
                        voiceArrived(done[0]);
                    }
                    yield List.of(ack);
                }
                case "message_read" -> {
                    if (inbox != null && message.get("deck") instanceof String deck
                            && message.get("id") instanceof Number id) {
                        inbox.markRead(deck, id.longValue());
                    }
                    yield List.of();
                }
                case "board" -> {
                    rememberBoard(message);
                    if (message.get("id") instanceof String deck) {
                        openAction.deckSeen(deck, message.get("keypad"));
                    }
                    yield List.of();
                }
                default -> List.of(error("unknown_type", "Unsupported message type"));
            };
        } catch (IllegalArgumentException e) {
            return List.of(error("invalid_request", e.getMessage()));
        } catch (RuntimeException e) {
            return List.of(error("internal_error", "The host could not process the request"));
        }
    }

    /**
     * The metrics catalog for a Deck (docs/03-SERVER.md), a page from {@code from} that
     * fits the peer's frame. A sample is taken first if none was, so the catalog is not empty.
     */
    Map<String, Object> catalogFrame(Map<String, Object> message, int maxFrameBytes) {
        Long from = Values.validateInt(message.get("from"));
        if (sampler.catalog().entries().isEmpty()) {
            sampler.sample(1.0);
        }
        return sampler.catalog().frame(from == null ? 0 : from.intValue(), maxFrameBytes);
    }

    /**
     * A page of the texts the Deck shows, in its language (its own setting, or the one for
     * all): docs/archive/design-notes/DESIGN.md.
     */
    Map<String, Object> textsFrame(Map<String, Object> message, int maxFrameBytes) {
        Long from = Values.validateInt(message.get("from"));
        String deck = message.get("deck") instanceof String id ? id : null;
        return texts.frame(config.getDeckLanguage(deck), from == null ? 0 : from.intValue(), maxFrameBytes);
    }

    /** The Decks' consoles (docs/CONSOLE.md): every link and the API share this one. */
    public dev.controldeck.host.console.ConsoleHub console() {
        return console;
    }

    /** The Decks' inbox; set by the host at start (and by the tests). */
    public void inbox(MessageStore store) {
        this.inbox = store;
    }

    public MessageStore inbox() {
        return inbox;
    }

    /** The most messages in one answer (the Deck keeps the newest few). */
    private static final int MESSAGES_PER_ANSWER = 10;

    /** A Deck's messages newer than the newest it has, as many as fit one frame. */
    Map<String, Object> messagesFrame(Map<String, Object> message, int maxFrameBytes) {
        String deck = message.get("deck") instanceof String id ? id : "";
        long after = message.get("after") instanceof Number n ? n.longValue() : 0;
        List<Map<String, Object>> items = inbox == null ? new ArrayList<>()
                : new ArrayList<>(inbox.forDeck(deck, after, MESSAGES_PER_ANSWER));
        Map<String, Object> frame = map("type", "messages", "messages", items);
        while (!items.isEmpty() && binarySize(frame) > maxFrameBytes) {
            items.remove(items.size() - 1);  // the rest comes with the next request
        }
        return frame;
    }

    /**
     * What a transfer_request asks for: kind 1 a board's firmware image, kind 2 a voice
     * message's audio, kind 4 the Deck's pack ("<board> <version it has>": nothing when the
     * same version).
     */
    private List<Map<String, Object>> transferStart(Map<String, Object> message) {
        int kind = message.get("kind") instanceof Number k ? k.intValue() : 0;
        String what = text(message, "what");
        if (kind == Transfers.KIND_PACK) {
            return packStart(text(message, "deck"), what);
        }
        if (kind == Transfers.KIND_VOICE) {
            MessageStore store = inbox;
            byte[] audio = store == null || !what.matches("\\d{1,10}") ? null : store.audio(Long.parseLong(what));
            return audio == null ? null : transfers.start(kind, audio);
        }
        return transfers.start(kind, what, firmware);
    }

    private List<Map<String, Object>> packStart(String deck, String what) {
        PackStore store = packs;
        String[] parts = what.trim().split(" ", 2);
        String board = parts[0];
        String has = parts.length > 1 ? parts[1].trim() : "";
        PackStore.Pack pack;
        try {
            pack = store == null ? null : store.forDeck(deck, board).orElse(null);
        } catch (RuntimeException e) {
            Log.info("[pack] " + e.getMessage());
            return null;
        }
        if (pack == null || pack.version().equals(has)) {
            return null;  // error 2: nothing to send (the Deck keeps what it has)
        }
        Log.info("[pack] " + pack.name() + " " + pack.version() + " (" + pack.files() + " files, " + pack.bytes().length
                + " bytes) to " + deck + " (" + board + ", had " + (has.isEmpty() ? "none" : has) + ")");
        return transfers.start(Transfers.KIND_PACK, pack.bytes());
    }

    /** A Deck's recording came whole: a voice message for whom it named ("" = the PC). */
    private void voiceArrived(Uploads.Done done) {
        MessageStore store = inbox;
        if (store == null) {
            return;
        }
        String to = done.to().isBlank() ? MessageStore.PC : done.to();
        Map<String, Object> board = boards.get(done.deck());
        if (board == null) {
            board = boards.get(done.deck().toUpperCase());
        }
        Object name = board == null ? null : board.getOrDefault("name", board.get("board"));
        String from = name instanceof String n && !n.isBlank() ? n : done.deck();
        MessageStore.Message m = store.addVoice(to, from, done.bytes());
        Log.info("[voice] message " + m.id() + " from " + from + " to " + to + ", " + m.text()
                + " (GET /api/messages/" + m.id() + "/audio)");
    }

    private static String text(Map<String, Object> message, String key) {
        return message.get(key) instanceof String s ? s : "";
    }

    private static int number(Map<String, Object> message, String key) {
        return message.get(key) instanceof Number n ? n.intValue() : 0;
    }

    /** Where the firmware images are; set by the host at start (and by the tests). */
    public void firmwareStore(FirmwareStore store) {
        this.firmware = store;
    }

    public FirmwareStore firmwareStore() {
        return firmware;
    }

    /** Where the Decks' packs are (screens and theme for the card); set by the host at start. */
    public void packStore(PackStore store) {
        this.packs = store;
    }

    public PackStore packStore() {
        return packs;
    }

    /**
     * The answer to a Deck's {@code firmware_check}: whether the server has an image for its
     * board in another version than the one it runs, and how to fetch it (over Wi-Fi, with
     * the Deck's token, from {@code path}).
     */
    Map<String, Object> firmwareOffer(Map<String, Object> message) {
        String board = message.get("board") instanceof String text ? text : null;
        String running = message.get("firmware") instanceof String text ? text : "";
        if (!FirmwareStore.validBoard(board)) {
            throw new IllegalArgumentException("firmware_check needs a board");
        }
        FirmwareStore store = firmware;
        Optional<FirmwareStore.Image> image = store == null ? Optional.empty() : store.find(board);
        if (image.isEmpty() || image.get().version().equals(running)) {
            return map("type", "firmware_offer", "available", false, "running", running);
        }
        FirmwareStore.Image found = image.get();
        return map("type", "firmware_offer", "available", true, "version", found.version(), "size", found.size(),
                "sha256", found.sha256(), "md5", found.md5(), "path", "/api/firmware/" + board);
    }

    /**
     * The Wi-Fi access a Deck receives over its USB cable: a token of its own, the port
     * and the host's LAN addresses. The cable is the proof of trust, so nothing is asked;
     * nothing of this is ever sent over the network. When Wi-Fi is off, or the host only
     * listens on this computer, the frame says why instead.
     */
    public Map<String, Object> wifiTokenFrame(String deckId) {
        ConfigService.WifiSettings wifi = config.getWifiSettings();
        if (!wifi.enabled()) {
            return map("type", "wifi_token", "available", false, "reason", "Wi-Fi is off on the host");
        }
        String bind = listenBind != null ? listenBind : wifi.bind();
        int port = listenPort > 0 ? listenPort : wifi.port();
        List<String> addresses = lanAddresses(bind);
        if (addresses.isEmpty()) {
            return map("type", "wifi_token", "available", false, "reason", "The host listens on this PC only");
        }
        try {
            String token = config.issueDeckToken(deckId);
            return map("type", "wifi_token", "available", true, "token", token, "port", port,
                    "addresses", addresses);
        } catch (IllegalArgumentException error) {
            return map("type", "wifi_token", "available", false, "reason", "Invalid Deck id");
        }
    }

    /** The address and port the API server was started on, for {@link #wifiTokenFrame}. */
    public void setListenAddress(String bind, int port) {
        this.listenBind = bind;
        this.listenPort = port;
    }

    /** Up to four IPv4 addresses a Deck on the LAN can use to reach the API, wired ones first. */
    static List<String> lanAddresses(String bind) {
        List<String> result = new ArrayList<>();
        if (bind == null || List.of("127.0.0.1", "localhost", "::1").contains(bind)) {
            return result;
        }
        if (!bind.equals("0.0.0.0") && !bind.equals("::")) {
            result.add(bind);
            return result;
        }
        try {
            List<java.net.NetworkInterface> nics = new ArrayList<>(java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces()));
            nics.sort(java.util.Comparator.comparing(nic -> nic.getName().toLowerCase(java.util.Locale.ROOT).startsWith("wl")));
            for (java.net.NetworkInterface nic : nics) {
                String name = nic.getName().toLowerCase(java.util.Locale.ROOT);
                if (!nic.isUp() || nic.isLoopback() || nic.isVirtual() || name.startsWith("docker")
                        || name.startsWith("veth") || name.startsWith("br-") || name.startsWith("virbr")
                        || name.startsWith("lxc") || name.startsWith("vmnet") || name.startsWith("vbox")
                        || name.startsWith("tun") || name.startsWith("tap") || name.startsWith("wg")) {
                    continue;
                }
                for (java.net.InetAddress address : java.util.Collections.list(nic.getInetAddresses())) {
                    if (address instanceof java.net.Inet4Address && !address.isLoopbackAddress()
                            && !address.isLinkLocalAddress() && result.size() < 4) {
                        result.add(address.getHostAddress());
                    }
                }
            }
        } catch (java.net.SocketException error) {
            // No list of interfaces: the Deck is told the host has no LAN address.
        }
        return result;
    }

    // The Deck takes 12 KB per answer and refuses a larger one: 5 frames of about 1.6 KB
    // leave room for the stats and the other replies of the same request.
    private static final int KEY_FRAMES_PER_ANSWER = 5;
    private static final java.util.regex.Pattern DECK_ID = java.util.regex.Pattern.compile("^[0-9A-Za-z:_-]{1,32}$");
    private static final List<String> BOARD_TEXT = List.of("board", "firmware", "chip", "display", "touch", "sd", "audio");
    private static final List<String> BOARD_NUMBERS = List.of("rev", "cores", "mhz", "flash", "flash_build", "psram",
            "battery_mv");
    private static final List<String> BOARD_FLAGS = List.of("psram_in_package", "rgb_led");
    private static final List<String> BOARD_LISTS = List.of("radios", "screen", "heap", "nvs");

    /**
     * A Deck's description: only the known fields, short texts and numbers, so a peer cannot
     * fill the host's memory. Printed on the console when it is new or changed.
     */
    private void rememberBoard(Map<String, Object> message) {
        if (!(message.get("id") instanceof String id) || !DECK_ID.matcher(id).matches()) {
            throw new IllegalArgumentException("board needs the Deck id");
        }
        Map<String, Object> board = new LinkedHashMap<>();
        board.put("id", id);
        for (String key : BOARD_TEXT) {
            if (message.get(key) instanceof String text) {
                board.put(key, Values.truncateCodePoints(text, 40));
            }
        }
        for (String key : BOARD_NUMBERS) {
            if (message.get(key) instanceof Number number) {
                board.put(key, number.longValue());
            }
        }
        for (String key : BOARD_FLAGS) {
            if (message.get(key) instanceof Boolean flag) {
                board.put(key, flag);
            }
        }
        for (String key : BOARD_LISTS) {
            if (message.get(key) instanceof List<?> list) {
                List<Object> copy = new ArrayList<>();
                for (Object item : list) {
                    if (copy.size() >= 4) {
                        break;
                    }
                    if (item instanceof Number number) {
                        copy.add(number.longValue());
                    } else if (item instanceof String text) {
                        copy.add(Values.truncateCodePoints(text, 8));
                    }
                }
                board.put(key, copy);
            }
        }
        Map<String, Object> previous = boards.size() < 64 || boards.containsKey(id) ? boards.put(id, board) : null;
        Map<String, Object> withoutHeap = new LinkedHashMap<>(board);
        withoutHeap.remove("heap");
        withoutHeap.remove("nvs");
        withoutHeap.remove("battery_mv");
        Map<String, Object> previousWithoutHeap = previous == null ? null : new LinkedHashMap<>(previous);
        if (previousWithoutHeap != null) {
            previousWithoutHeap.remove("heap");
            previousWithoutHeap.remove("nvs");
            previousWithoutHeap.remove("battery_mv");
        }
        if (!withoutHeap.equals(previousWithoutHeap)) {
            Log.info("[deck] " + describeBoard(board));
        }
    }

    /** The Decks that described themselves, by id. */
    public Map<String, Map<String, Object>> boards() {
        return new java.util.TreeMap<>(boards);
    }

    /** One line for the console: what the Deck is and has. */
    public static String describeBoard(Map<String, Object> b) {
        StringBuilder line = new StringBuilder();
        line.append(b.get("id")).append(": ").append(b.getOrDefault("board", "?"));
        line.append(", firmware ").append(b.getOrDefault("firmware", "?"));
        line.append(", ").append(b.getOrDefault("chip", "?")).append(" rev ").append(b.getOrDefault("rev", "?"));
        line.append(", ").append(b.getOrDefault("cores", "?")).append(" cores ").append(b.getOrDefault("mhz", "?")).append(" MHz");
        long flash = b.get("flash") instanceof Long n ? n : 0;
        long build = b.get("flash_build") instanceof Long n ? n : 0;
        if (flash > 0) {
            line.append(", flash ").append(flash >> 20).append(" MB");
            if (build > 0 && build != flash) {
                line.append(" (firmware built for ").append(build >> 20).append(" MB)");
            }
        }
        long psram = b.get("psram") instanceof Long n ? n : 0;
        if (psram > 0) {
            line.append(", PSRAM ").append(psram >> 20).append(" MB");
        } else if (Boolean.TRUE.equals(b.get("psram_in_package"))) {
            line.append(", PSRAM in the chip but not enabled by this firmware");
        }
        if (b.get("radios") instanceof List<?> radios && !radios.isEmpty()) {
            line.append(", radios ").append(String.join("+", radios.stream().map(String::valueOf).toList()));
        }
        if (b.get("screen") instanceof List<?> screen && screen.size() == 2) {
            line.append(", screen ").append(screen.get(0)).append("x").append(screen.get(1));
            line.append(" ").append(b.getOrDefault("display", "")).append("/").append(b.getOrDefault("touch", ""));
        }
        if (b.get("sd") instanceof String sd && !sd.isEmpty()) {
            line.append(", SD ").append(sd);
        }
        Object audio = b.get("audio");
        line.append(", audio ").append(audio instanceof String a && !a.isEmpty() ? a : "none");
        if (b.get("battery_mv") instanceof Long mv && mv > 0) {
            line.append(String.format(", battery %.2f V", mv / 1000.0));
        }
        return line.toString();
    }

    public static Map<String, Object> error(String code, String detail) {
        return map("type", "error", "code", code, "detail", Values.truncateCodePoints(detail, 120));
    }

    public Map<String, Object> hostHello() {
        Object interval = config.getTelemetrySettings().get("interval_seconds");
        List<Integer> windows = MetricRegistry.WINDOWS;
        return map(
                "type", "host_hello",
                "protocol", Protocol.VERSION,
                "caps", Protocol.CAPABILITIES,
                "interval_seconds", interval,
                "history_windows", windows,
                "server_time", System.currentTimeMillis() / 1000L);
    }

    /** The macro list the board shows: id and label of each configured macro. */
    public Map<String, Object> configFrame() {
        List<Map<String, Object>> macros = new ArrayList<>();
        for (Map<String, Object> macro : config.getMacros()) {
            Long id = Values.validateInt(macro.get("id"));
            Map<String, Object> entry = new LinkedHashMap<>();
            entry.put("id", id == null ? 0 : id.intValue());
            entry.put("label", Values.truncateCodePoints(String.valueOf(macro.get("label")), ConfigService.MAX_LABEL_LENGTH));
            macros.add(entry);
        }
        return map("type", "config", "macros", macros);
    }

    public OpenActionBridge openAction() {
        return openAction;
    }

    /** While OpenDeck is attached a key is a press for it, not one of the host's macros. */
    private Map<String, Object> macroOrKey(Map<String, Object> message) {
        if (!openAction.attached()) {
            return macroResult(message);
        }
        Object id = message.get("id");
        boolean pressed = id instanceof Number number
                && openAction.press(message.get("deck") instanceof String deck ? deck : null, number.intValue() - 1);
        return map("type", "macro_result", "id", id, "ok", pressed, "detail", pressed ? "OpenDeck" : "Not a device of OpenDeck yet");
    }

    /** Over Wi-Fi the Deck can only be answered: its key images travel with the answers. */
    private List<Map<String, Object>> withKeyImages(Map<String, Object> message, Map<String, Object> stats) {
        List<Map<String, Object>> replies = new ArrayList<>(List.of(stats));
        if (message.get("deck") instanceof String deck) {
            replies.addAll(openAction.take(deck, KEY_FRAMES_PER_ANSWER));
            if (openAction.hasQueued(deck)) {
                replies.add(map("type", "key_more"));  // the Deck asks again at once
            }
        }
        return replies;
    }

    private Map<String, Object> macroResult(Map<String, Object> message) {
        Long id = Values.validateInt(message.get("id"));
        if (id == null || id < 1 || id > ConfigService.MAX_MACRO_ID) {
            throw new IllegalArgumentException("Macro id must be an integer from 1 to " + ConfigService.MAX_MACRO_ID);
        }
        Map<String, Object> result = macros.handleEvent(id.intValue());
        boolean ran = Boolean.TRUE.equals(result.get("success")) && Boolean.TRUE.equals(result.get("action_executed"));
        Map<String, Object> reply = map("type", "macro_result", "id", id.intValue(), "ok", ran);
        if (ran) {
            reply.put("executed", true);
        } else {
            Object why = result.get("error") != null ? result.get("error") : result.get("message");
            reply.put("detail", Values.truncateCodePoints(String.valueOf(why == null ? "failed" : why), 100));
        }
        return reply;
    }

    private Map<String, Object> historyFrame(Map<String, Object> message, int maxFrameBytes) {
        Object metricValue = message.get("metric");
        if (!(metricValue instanceof String metric) || !MetricRegistry.has(metric)) {
            throw new IllegalArgumentException("Unknown or missing metric");
        }

        Long minutes = Values.validateInt(message.getOrDefault("minutes", 5));
        if (minutes == null || !MetricRegistry.isValidWindow(minutes)) {
            throw new IllegalArgumentException("minutes must be one of " + MetricRegistry.WINDOWS);
        }

        Long requestedPoints = Values.validateInt(message.getOrDefault("points", Protocol.DEFAULT_HISTORY_POINTS));
        if (requestedPoints == null || requestedPoints < Protocol.MIN_HISTORY_POINTS || requestedPoints > Protocol.MAX_HISTORY_POINTS) {
            throw new IllegalArgumentException("points must be between " + Protocol.MIN_HISTORY_POINTS + " and " + Protocol.MAX_HISTORY_POINTS);
        }
        int points = requestedPoints.intValue();

        // Make sure the newest sample is part of the answer.
        sampler.sample(1.0);

        int minutesValue = minutes.intValue();
        var series = sampler.history().series(metric, minutesValue, points, null);
        Map<String, Object> frame = map(
                "type", "history",
                "metric", metric,
                "unit", series.unit(),
                "minutes", minutesValue,
                "step_s", series.stepSeconds(),
                "end", series.end(),
                "values", series.values());

        // Shrink by halving the resolution until the frame fits the peer's limit.
        while (binarySize(frame) > maxFrameBytes - 8 && valuesOf(frame).size() > Protocol.MIN_HISTORY_POINTS) {
            int next = Math.max(Protocol.MIN_HISTORY_POINTS, valuesOf(frame).size() / 2);
            var smaller = sampler.history().series(metric, minutesValue, next, null);
            frame.put("values", smaller.values());
            frame.put("step_s", smaller.stepSeconds());
        }
        if (binarySize(frame) > maxFrameBytes) {
            throw new IllegalArgumentException("Reply does not fit the maximum frame size");
        }
        return frame;
    }

    private Map<String, Object> inventoryFrame(Map<String, Object> message, int maxFrameBytes) {
        Object componentValue = message.get("component");
        if (!(componentValue instanceof String component) || !MetricRegistry.COMPONENTS.contains(component)) {
            throw new IllegalArgumentException("component must be one of " + MetricRegistry.COMPONENTS);
        }

        Map<String, Object> data = inventory.component(component);
        if (data == null) {
            data = new LinkedHashMap<>();
        }
        Map<String, Object> frame = map("type", "inventory", "component", component, "data", data);

        // Drop list entries from the tail (extra disks / interfaces) until the frame fits.
        for (String listKey : List.of("disks", "interfaces")) {
            while (data.get(listKey) instanceof List<?> items && items.size() > 1
                    && binarySize(frame) > maxFrameBytes) {
                ((List<?>) data.get(listKey)).remove(items.size() - 1);
                frame.put("truncated", true);
            }
        }
        if (binarySize(frame) > maxFrameBytes) {
            throw new IllegalArgumentException("Reply does not fit the maximum frame size");
        }
        return frame;
    }

    @SuppressWarnings("unchecked")
    private static List<Object> valuesOf(Map<String, Object> frame) {
        return (List<Object>) frame.get("values");
    }

    /** The bytes a reply takes on the link: its protocol 4 payload (docs/04-PROTOCOL.md). */
    static int binarySize(Map<String, Object> frame) {
        Codec4.Out out = Codec4.encode(frame);
        return out == null ? 0 : out.payload().length;
    }

    /** Insertion-ordered map from alternating key/value arguments. */
    public static Map<String, Object> map(Object... pairs) {
        Map<String, Object> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            map.put((String) pairs[i], pairs[i + 1]);
        }
        return map;
    }
}
