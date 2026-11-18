package dev.controldeck.host.texts;

import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.zip.CRC32;

/**
 * The texts the Decks show once they have a server (docs/archive/design-notes/DESIGN.md), one table
 * per language in the resources ({@code texts/<lang>.json}). The English table is the master:
 * its keys, in order, are the ids the firmware was built with ({@code app/text_ids.h}, written
 * from it by {@code tools/gen_text_ids.py}). Another language gives the same keys in the same
 * order; a key it lacks falls back to English, so a half-translated table still works.
 */
public final class TextTables {

    public static final String MASTER = "en";

    /** One language's texts in the master's order, its version and the ids' check. */
    public record Table(String language, List<String> texts, String version, String ids) {
    }

    private final Map<String, Table> tables = new ConcurrentHashMap<>();

    /** The table of a language, or English when there is none for it. */
    public Table table(String language) {
        String wanted = language == null ? MASTER : language;
        Table found = tables.computeIfAbsent(wanted, TextTables::load);
        return found != null ? found : tables.computeIfAbsent(MASTER, TextTables::load);
    }

    /**
     * A page of a table for a Deck, from {@code from} on, as many texts as fit
     * {@code maxBytes} of JSON: {@code {"type":"texts","tv":..,"ids":..,"lang":..,
     * "from":..,"total":..,"t":[texts]}}. A Deck that gets fewer than {@code total} asks again.
     */
    public Map<String, Object> frame(String language, int from, int maxBytes) {
        Table table = table(language);
        List<String> all = table.texts();
        int start = Math.max(0, Math.min(from, all.size()));
        List<String> page = new ArrayList<>();
        int bytes = 128;  // the frame around the texts
        for (int i = start; i < all.size(); i++) {
            int size = all.get(i).getBytes(StandardCharsets.UTF_8).length + 2;  // str16
            if (!page.isEmpty() && bytes + size > maxBytes) {
                break;
            }
            page.add(all.get(i));
            bytes += size;
        }
        Map<String, Object> frame = new LinkedHashMap<>();
        frame.put("type", "texts");
        frame.put("tv", table.version());
        frame.put("ids", table.ids());
        frame.put("lang", table.language());
        frame.put("from", start);
        frame.put("total", all.size());
        frame.put("t", page);
        return frame;
    }

    private static Table load(String language) {
        Map<String, Object> master = read(MASTER);
        if (master == null) {
            throw new IllegalStateException("the English text table is missing from the resources");
        }
        Map<String, Object> own = MASTER.equals(language) ? master : read(language);
        if (own == null) {
            return null;
        }
        List<String> texts = new ArrayList<>(master.size());
        CRC32 version = new CRC32();
        for (Map.Entry<String, Object> entry : master.entrySet()) {
            Object text = own.get(entry.getKey());
            String value = text instanceof String s ? s : String.valueOf(entry.getValue());
            texts.add(value);
            version.update(value.getBytes(StandardCharsets.UTF_8));
            version.update(0);
        }
        CRC32 ids = new CRC32();
        ids.update(String.join("\n", master.keySet()).getBytes(StandardCharsets.UTF_8));
        return new Table(language, List.copyOf(texts), String.format("%08x", version.getValue()),
                String.format("%08X", ids.getValue()));
    }

    private static Map<String, Object> read(String language) {
        if (!language.matches("[a-z]{2}")) {
            return null;
        }
        try (InputStream in = TextTables.class.getResourceAsStream("/texts/" + language + ".json")) {
            return in == null ? null : Json.parseObject(new String(in.readAllBytes(), StandardCharsets.UTF_8));
        } catch (IOException e) {
            return null;
        }
    }
}
