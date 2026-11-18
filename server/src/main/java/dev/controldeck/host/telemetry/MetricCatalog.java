package dev.controldeck.host.telemetry;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * What this machine can show, as the Decks learn it (docs/03-SERVER.md): one entry per
 * metric of every component instance the telemetry sources found (cpu0, gpu0, each network
 * card, ...). The firmware knows kinds of values, not hardware: it draws an entry by its
 * kind and groups entries by their group, so the same firmware fits any machine.
 *
 * <p>Entries are added the first time their source gives a value and kept for the run, so a
 * value that is missing for a moment does not change the catalog. Every change gives a new
 * {@link #version()}; the {@code stats} frame carries it ({@code cv}) with the values in
 * catalog order ({@code v}), and a Deck whose catalog is another version asks for it again.
 */
public final class MetricCatalog {

    /** How a value is drawn: a bar from 0 to 100, a temperature, or a number with its unit. */
    public enum Kind {
        PERCENT("pct"), TEMPERATURE("temp"), RATE("rate"), FREQUENCY("freq"), POWER("power"), SIZE("size");

        private final String code;

        Kind(String code) {
            this.code = code;
        }

        public String code() {
            return code;
        }
    }

    /**
     * One metric of one instance. {@code id} is stable ("cpu0.use", "net:eno1.down");
     * {@code group} is the instance as the Deck titles it ("CPU", "eno1"); {@code max} is
     * null when the scale follows the values; {@code refreshSeconds} is how often it changes
     * enough to be worth sending (refresh per metric).
     */
    public record Entry(String id, String group, String name, String shortName, String unit, Kind kind,
                        Double max, int refreshSeconds) {

        List<Object> compact() {
            List<Object> row = new ArrayList<>();
            row.add(id);
            row.add(group);
            row.add(name);
            row.add(shortName);
            row.add(unit);
            row.add(kind.code());
            row.add(max);
            row.add(refreshSeconds);
            return row;
        }
    }

    /** A value a source measured now (null: not measured this time). */
    public record Sample(Entry entry, Double value) {
    }

    private final Map<String, Entry> entries = new LinkedHashMap<>();
    private String version = versionOf(List.of());

    /** Adds what is new among the samples (only those with a value); returns the values in catalog order. */
    public synchronized List<Double> update(List<Sample> samples) {
        boolean changed = false;
        Map<String, Double> values = new LinkedHashMap<>();
        for (Sample sample : samples) {
            Entry entry = sample.entry();
            if (sample.value() != null && !entries.containsKey(entry.id())) {
                entries.put(entry.id(), entry);
                changed = true;
            }
            values.put(entry.id(), sample.value());
        }
        if (changed) {
            version = versionOf(new ArrayList<>(entries.values()));
        }
        List<Double> ordered = new ArrayList<>(entries.size());
        for (String id : entries.keySet()) {
            ordered.add(values.get(id));
        }
        return ordered;
    }

    public synchronized String version() {
        return version;
    }

    public synchronized List<Entry> entries() {
        return new ArrayList<>(entries.values());
    }

    /**
     * The catalog for a Deck, from entry {@code from} on, as many entries as fit in
     * {@code maxBytes} of JSON: {@code {"type":"catalog","cv":..,"from":..,"total":..,"m":[rows]}}.
     * A Deck that gets fewer than {@code total} asks again from where the page ended.
     */
    public synchronized Map<String, Object> frame(int from, int maxBytes) {
        List<Entry> all = new ArrayList<>(entries.values());
        int start = Math.max(0, Math.min(from, all.size()));
        List<Object> rows = new ArrayList<>();
        int bytes = 96;  // the frame around the rows
        for (int i = start; i < all.size(); i++) {
            List<Object> row = all.get(i).compact();
            int size = binarySize(all.get(i));
            if (!rows.isEmpty() && bytes + size > maxBytes) {
                break;
            }
            rows.add(row);
            bytes += size;
        }
        Map<String, Object> frame = new LinkedHashMap<>();
        frame.put("type", "catalog");
        frame.put("cv", version);
        frame.put("from", start);
        frame.put("total", all.size());
        frame.put("m", rows);
        return frame;
    }

    /** An entry's bytes in the catalog frame: five texts with their lengths, kind, max, refresh. */
    private static int binarySize(Entry e) {
        int texts = 0;
        for (String text : List.of(e.id(), e.group(), e.name(), e.shortName(), e.unit())) {
            texts += 1 + text.getBytes(StandardCharsets.UTF_8).length;
        }
        return texts + 1 + 4 + 2;
    }

    /** Eight hex digits that change with any entry: the Deck compares, nothing more. */
    static String versionOf(List<Entry> entries) {
        CRC32 crc = new CRC32();
        for (Entry entry : entries) {
            crc.update(dev.controldeck.host.util.Json.encode(entry.compact()).getBytes(StandardCharsets.UTF_8));
        }
        return String.format("%08x", crc.getValue());
    }
}
