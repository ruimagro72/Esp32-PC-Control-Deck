package dev.controldeck.host.protocol;

import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The description each Deck sent ("board" frame), by Deck id, newest last. */
final class BoardRegistry {

    private final Map<String, Map<String, Object>> boards = new java.util.concurrent.ConcurrentHashMap<>();
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
    void remember(Map<String, Object> message) {
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

    /** The description of one Deck, or null when it has not described itself. */
    Map<String, Object> get(String deck) {
        return boards.get(deck);
    }

    /** A sorted copy of every description. */
    Map<String, Map<String, Object>> snapshot() {
        return new java.util.TreeMap<>(boards);
    }

    /** One line for the console: what the Deck is and has. */
    static String describeBoard(Map<String, Object> b) {
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
}
