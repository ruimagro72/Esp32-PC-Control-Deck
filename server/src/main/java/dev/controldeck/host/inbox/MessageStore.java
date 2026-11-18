package dev.controldeck.host.inbox;

import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Values;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The Decks' inbox (docs/04-PROTOCOL.md, messages): messages left on the server for one Deck
 * or for all, and the ones the Decks leave for the PC ("pc"), text or voice, each read or not
 * per Deck. Kept in {@code var/messages.json} (the audio in {@code var/voice/<id>.pcm}) so a
 * restart keeps them; the newest {@link #MAX_KEPT}.
 */
public final class MessageStore {

    public static final int MAX_KEPT = 100;
    public static final int MAX_TEXT = 500;
    public static final int MAX_FROM = 24;
    public static final String ALL = "all";
    /** Messages a Deck leaves for the people at the PC (not shown on any Deck). */
    public static final String PC = "pc";

    /** One message. {@code to} is a Deck id or "all"; {@code readBy} the Decks that read it. */
    public record Message(long id, long time, String kind, String from, String to, String text, List<String> readBy) {

        boolean forDeck(String deck) {
            return ALL.equals(to) || to.equalsIgnoreCase(deck);  // "pc": never a Deck's
        }

        Map<String, Object> toMap() {
            Map<String, Object> map = new LinkedHashMap<>();
            map.put("id", id);
            map.put("time", time);
            map.put("kind", kind);
            map.put("from", from);
            map.put("to", to);
            map.put("text", text);
            map.put("read_by", readBy);
            return map;
        }
    }

    private final Path file;
    private final List<Message> messages = new ArrayList<>();
    private long nextId = 1;

    public MessageStore(Path file) {
        this.file = file;
        load();
    }

    /** Leaves a text message; returns it. */
    public synchronized Message add(String to, String from, String kind, String text) {
        if ("voice".equals(kind)) {
            throw new IllegalArgumentException("a voice message needs its audio (\"file\": a WAV on this PC)");
        }
        return store(to, from, "text", text);
    }

    /** Leaves a voice message: 16 kHz, 16-bit, mono PCM ({@link Voice}), kept in voice/ beside the file. */
    public synchronized Message addVoice(String to, String from, byte[] pcm) {
        if (pcm == null || pcm.length < 2 || pcm.length > Voice.MAX_BYTES) {
            throw new IllegalArgumentException("the audio must be 1 byte to " + Voice.MAX_SECONDS + " s long");
        }
        Message message = store(to, from, "voice", Voice.lengthText(pcm.length));
        try {
            Files.createDirectories(voiceDir());
            Files.write(voiceDir().resolve(message.id() + ".pcm"), pcm);
        } catch (IOException e) {
            messages.remove(message);
            save();
            throw new IllegalStateException("could not keep the audio: " + e.getMessage(), e);
        }
        return message;
    }

    /** The audio of a voice message, or null. */
    public synchronized byte[] audio(long id) {
        try {
            Path path = voiceDir().resolve(id + ".pcm");
            return Files.isRegularFile(path) ? Files.readAllBytes(path) : null;
        } catch (IOException e) {
            return null;
        }
    }

    private Path voiceDir() {
        return file.toAbsolutePath().getParent().resolve("voice");
    }

    private Message store(String to, String from, String kind, String text) {
        if (text == null || text.isBlank()) {
            throw new IllegalArgumentException("text is empty");
        }
        String target = to == null || to.isBlank() ? ALL : to.trim();
        if (!ALL.equals(target) && !PC.equals(target) && !target.matches("[0-9A-Fa-f]{12}")) {
            throw new IllegalArgumentException("to must be a Deck id (12 hex digits), \"all\" or \"pc\"");
        }
        String what = "voice".equals(kind) ? "voice" : "text";
        Message message = new Message(nextId++, System.currentTimeMillis() / 1000L, what,
                Values.truncateCodePoints(from == null || from.isBlank() ? "server" : from.trim(), MAX_FROM), target,
                Values.truncateCodePoints(text.trim(), MAX_TEXT), new ArrayList<>());
        messages.add(message);
        while (messages.size() > MAX_KEPT) {
            deleteAudio(messages.remove(0).id());
        }
        save();
        return message;
    }

    /** A Deck's messages newer than {@code after}, oldest first, at most {@code max}. */
    public synchronized List<Map<String, Object>> forDeck(String deck, long after, int max) {
        List<Map<String, Object>> out = new ArrayList<>();
        for (Message m : messages) {
            if (m.id() > after && m.forDeck(deck) && out.size() < max) {
                Map<String, Object> item = new LinkedHashMap<>();
                item.put("id", m.id());
                item.put("time", m.time());
                item.put("kind", m.kind());
                item.put("read", m.readBy().contains(deck.toUpperCase()));
                item.put("from", m.from());
                item.put("text", m.text());
                out.add(item);
            }
        }
        return out;
    }

    /** A Deck read a message: kept so the other Decks still see theirs unread. */
    public synchronized boolean markRead(String deck, long id) {
        for (Message m : messages) {
            if (m.id() == id && m.forDeck(deck)) {
                String who = deck.toUpperCase();
                if (!m.readBy().contains(who)) {
                    m.readBy().add(who);
                    save();
                }
                return true;
            }
        }
        return false;
    }

    public synchronized boolean delete(long id) {
        boolean removed = messages.removeIf(m -> m.id() == id);
        if (removed) {
            deleteAudio(id);
            save();
        }
        return removed;
    }

    private void deleteAudio(long id) {
        try {
            Files.deleteIfExists(voiceDir().resolve(id + ".pcm"));
        } catch (IOException e) {
            // Left behind; harmless.
        }
    }

    public synchronized List<Map<String, Object>> all() {
        List<Map<String, Object>> out = new ArrayList<>();
        messages.forEach(m -> out.add(m.toMap()));
        return out;
    }

    private void load() {
        try {
            if (!Files.isRegularFile(file)) {
                return;
            }
            Object parsed = Json.parseValue(Files.readString(file));
            if (!(parsed instanceof List<?> list)) {
                return;
            }
            for (Object item : list) {
                if (!(item instanceof Map<?, ?> m) || !(m.get("id") instanceof Number id)) {
                    continue;
                }
                List<String> readBy = new ArrayList<>();
                if (m.get("read_by") instanceof List<?> who) {
                    who.forEach(w -> readBy.add(String.valueOf(w)));
                }
                messages.add(new Message(id.longValue(), m.get("time") instanceof Number t ? t.longValue() : 0,
                        String.valueOf(m.get("kind")), String.valueOf(m.get("from")), String.valueOf(m.get("to")),
                        String.valueOf(m.get("text")), readBy));
                nextId = Math.max(nextId, id.longValue() + 1);
            }
        } catch (IOException | IllegalArgumentException e) {
            // A damaged file: the inbox starts empty, and the next message rewrites it.
        }
    }

    private void save() {
        try {
            Files.createDirectories(file.getParent());
            Json.writeAtomic(file, Json.encode(all()));
        } catch (IOException e) {
            // Kept in memory; written with the next change.
        }
    }
}
