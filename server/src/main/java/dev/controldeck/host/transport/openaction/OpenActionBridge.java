package dev.controldeck.host.transport.openaction;

import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Log;
import java.awt.Color;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import javax.imageio.ImageIO;

/**
 * The host as an OpenAction device plugin: to OpenDeck each Deck is a device, so the Stream Deck
 * plugins that run there work on it. OpenDeck sends the image of each key and the Deck's taps go
 * back as key presses. Only the public plugin interface is used, between separate programs.
 *
 * <p>While attached, the Deck's keys belong to OpenDeck and do not run the host's own macros.
 */
public final class OpenActionBridge {

    public static final String NAMESPACE = "cd";
    private static final int ROWS_PER_FRAME = 6;  // a frame must fit SERIAL_MAX_FRAME_BYTES

    private record Keypad(int rows, int columns, int width, int height) {
    }

    private Consumer<String> sender;
    private final Map<String, Keypad> decks = new LinkedHashMap<>();
    private final Map<String, Map<Integer, Deque<Map<String, Object>>>> queued = new HashMap<>();
    private final Map<String, Integer> shown = new HashMap<>();

    public synchronized boolean attached() {
        return sender != null;
    }

    /** Connected to OpenDeck: register with it and announce the Decks known so far. */
    public synchronized void attach(Consumer<String> sender, String registerEvent, String uuid) {
        this.sender = sender;
        sender.accept(Json.encode(MessageHandler.map("event", registerEvent, "uuid", uuid)));
        decks.forEach(this::register);
    }

    /** OpenDeck went away: the keys show the host's own macros again. */
    public synchronized void detach() {
        sender = null;
        shown.clear();
        decks.forEach((deck, keypad) -> {
            Map<Integer, Deque<Map<String, Object>>> keys = queued.computeIfAbsent(deck, d -> new LinkedHashMap<>());
            keys.clear();
            for (int position = 0; position < keypad.rows() * keypad.columns(); ++position) {
                keys.put(position, new ArrayDeque<>(List.of(MessageHandler.map(
                        "type", "key_image", "position", position, "clear", true))));
            }
        });
    }

    /** A Deck described itself ("board" frame). Only one that can show keys becomes a device. */
    public synchronized void deckSeen(String deck, Object keypad) {
        if (!(keypad instanceof Map<?, ?> map) || !(map.get("rows") instanceof Number rows)
                || !(map.get("columns") instanceof Number columns) || !(map.get("w") instanceof Number width)
                || !(map.get("h") instanceof Number height)) {
            return;
        }
        Keypad seen = new Keypad(rows.intValue(), columns.intValue(), width.intValue(), height.intValue());
        if (seen.equals(decks.put(deck, seen)) || sender == null) {
            return;
        }
        register(deck, seen);
    }

    private void register(String deck, Keypad keypad) {
        sender.accept(Json.encode(MessageHandler.map("event", "registerDevice", "payload", MessageHandler.map(
                "id", NAMESPACE + "-" + deck, "name", "Control Deck " + deck, "rows", keypad.rows(),
                "columns", keypad.columns(), "encoders", 0, "touchpoints", 0, "infobars", 0, "type", 7))));
    }

    /** A tap on a key. A Deck reports one touch, so the press and release go together. */
    public synchronized boolean press(String deck, int position) {
        if (sender == null || deck == null || !decks.containsKey(deck)) {
            return false;
        }
        for (String event : List.of("keyDown", "keyUp")) {
            sender.accept(Json.encode(MessageHandler.map("event", event, "payload",
                    MessageHandler.map("device", NAMESPACE + "-" + deck, "position", position))));
        }
        return true;
    }

    /** One message from OpenDeck. */
    public void onMessage(String text) {
        Map<String, Object> message = Json.parseObject(text);
        if (message == null || !"setImage".equals(message.get("event"))
                || !(message.get("device") instanceof String device)
                || !device.startsWith(NAMESPACE + "-")) {
            return;
        }
        String deck = device.substring(NAMESPACE.length() + 1);
        if (message.get("position") instanceof Number position) {
            setImage(deck, position.intValue(), message.get("image") instanceof String image ? image : null);
        } else {
            clearAll(deck);
        }
    }

    private synchronized void clearAll(String deck) {
        Keypad keypad = decks.get(deck);
        if (keypad != null) {
            for (int position = 0; position < keypad.rows() * keypad.columns(); ++position) {
                setImage(deck, position, null);
            }
        }
    }

    private synchronized void setImage(String deck, int position, String image) {
        Keypad keypad = decks.get(deck);
        if (keypad == null || position < 0 || position >= keypad.rows() * keypad.columns()) {
            return;
        }
        String key = deck + "/" + position;
        Deque<Map<String, Object>> frames = new ArrayDeque<>();
        if (image == null) {
            shown.remove(key);
            frames.add(MessageHandler.map("type", "key_image", "position", position, "clear", true));
        } else {
            int[] pixels = decode(image, keypad);
            if (pixels == null) {
                return;
            }
            int hash = Arrays.hashCode(pixels);
            Integer last = shown.put(key, hash);
            if (last != null && last == hash) {
                return;  // OpenDeck repeats images: the link stays free
            }
            for (int y = 0; y < keypad.height(); y += ROWS_PER_FRAME) {
                frames.add(MessageHandler.map("type", "key_image", "position", position, "y", y,
                        "data", rows(pixels, keypad, y)));
            }
        }
        queued.computeIfAbsent(deck, d -> new LinkedHashMap<>()).put(position, frames);
    }

    /** What is queued for a Deck, at most max frames: a newer image replaces what was still queued. */
    public synchronized List<Map<String, Object>> take(String deck, int max) {
        List<Map<String, Object>> frames = new ArrayList<>();
        Map<Integer, Deque<Map<String, Object>>> keys = queued.get(deck);
        if (keys == null) {
            return frames;
        }
        var entries = keys.entrySet().iterator();
        while (entries.hasNext() && frames.size() < max) {
            Deque<Map<String, Object>> key = entries.next().getValue();
            while (!key.isEmpty() && frames.size() < max) {
                frames.add(key.poll());
            }
            if (key.isEmpty()) {
                entries.remove();
            }
        }
        return frames;
    }

    public synchronized boolean hasQueued(String deck) {
        Map<Integer, Deque<Map<String, Object>>> keys = queued.get(deck);
        return keys != null && !keys.isEmpty();
    }

    /** A data URI scaled to the key, keeping its proportions, as RGB565. Null if it cannot be read. */
    private static int[] decode(String uri, Keypad keypad) {
        int comma = uri.indexOf(',');
        if (!uri.startsWith("data:") || comma < 0 || !uri.substring(0, comma).endsWith(";base64")) {
            return null;
        }
        BufferedImage source;
        try {
            source = ImageIO.read(new ByteArrayInputStream(Base64.getDecoder().decode(uri.substring(comma + 1))));
        } catch (IOException | IllegalArgumentException error) {
            source = null;
        }
        if (source == null) {
            Log.error("[openaction] An image could not be read (" + uri.substring(0, Math.min(comma, 40)) + ")");
            return null;
        }
        BufferedImage key = new BufferedImage(keypad.width(), keypad.height(), BufferedImage.TYPE_INT_RGB);
        Graphics2D g = key.createGraphics();
        g.setColor(Color.BLACK);
        g.fillRect(0, 0, keypad.width(), keypad.height());
        g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR);
        double scale = Math.min((double) keypad.width() / source.getWidth(), (double) keypad.height() / source.getHeight());
        int w = (int) Math.round(source.getWidth() * scale);
        int h = (int) Math.round(source.getHeight() * scale);
        g.drawImage(source, (keypad.width() - w) / 2, (keypad.height() - h) / 2, w, h, null);
        g.dispose();
        int[] pixels = key.getRGB(0, 0, keypad.width(), keypad.height(), null, 0, keypad.width());
        for (int i = 0; i < pixels.length; ++i) {
            int p = pixels[i];
            pixels[i] = ((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F);
        }
        return pixels;
    }

    private static String rows(int[] pixels, Keypad keypad, int y) {
        int rows = Math.min(ROWS_PER_FRAME, keypad.height() - y);
        byte[] bytes = new byte[rows * keypad.width() * 2];
        for (int i = 0; i < rows * keypad.width(); ++i) {
            int p = pixels[y * keypad.width() + i];
            bytes[2 * i] = (byte) p;
            bytes[2 * i + 1] = (byte) (p >> 8);
        }
        return Base64.getEncoder().encodeToString(bytes);
    }
}
