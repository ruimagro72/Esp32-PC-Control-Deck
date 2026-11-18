package dev.controldeck.host.pack;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Properties;
import java.util.TreeMap;
import java.util.regex.Pattern;
import java.util.stream.Stream;

/**
 * The screens and theme each Deck keeps on its microSD card (docs/04-PROTOCOL.md, "Transfers",
 * kind 4). A pack is a folder, {@code <dir>/<name>/}, holding what goes under the card's
 * {@code /deck/} (screens/settings.scr, theme...). Files under {@code <dir>/<name>/@<board>/}
 * replace the same paths for that board. Which pack a Deck gets: {@code <dir>/decks.properties}
 * ({@code <deck id>=<pack name>}), otherwise "default"; without a default folder, the one in
 * the repository (firmware/sd-card/deck).
 */
public final class PackStore {

    public static final String DEFAULT = "default";
    public static final long MAX_FILE_BYTES = 256L * 1024;   // as the Deck (app/pack.h)
    public static final long MAX_PACK_BYTES = 512L * 1024;
    private static final Pattern PATH = Pattern.compile("[a-z0-9._/-]{1,47}");
    private static final Pattern NAME = Pattern.compile("[A-Za-z0-9._-]{1,40}");
    private static final Pattern DECK = Pattern.compile("[0-9A-Fa-f]{1,24}");

    /** A pack ready to send: its version and its bytes in the Deck's format. */
    public record Pack(String name, String version, byte[] bytes, int files) {
    }

    private final Path dir;
    private final Path repositoryDefault;

    public PackStore(Path dir, Path repositoryDefault) {
        this.dir = dir;
        this.repositoryDefault = repositoryDefault;
    }

    public Path dir() {
        return dir;
    }

    /** The pack this Deck gets, built for its board; empty when there is none. */
    public Optional<Pack> forDeck(String deck, String board) {
        return packNamed(assigned(deck), board);
    }

    /** A pack by its name, built for a board; empty when there is no such pack. */
    public Optional<Pack> packNamed(String name, String board) {
        if (name == null || !NAME.matcher(name).matches()) {
            return Optional.empty();
        }
        Path folder = dir.resolve(name);
        if (!Files.isDirectory(folder) && DEFAULT.equals(name) && repositoryDefault != null) {
            folder = repositoryDefault;
        }
        if (!Files.isDirectory(folder)) {
            return Optional.empty();
        }
        TreeMap<String, Path> files = new TreeMap<>();
        collect(folder, folder, files, false);
        if (board != null && NAME.matcher(board).matches()) {
            Path own = folder.resolve("@" + board);
            if (Files.isDirectory(own)) {
                collect(own, own, files, true);
            }
        }
        return files.isEmpty() ? Optional.empty() : Optional.of(build(name, files));
    }

    /** The packs there are (folders in the packs folder; "default" also from the repository). */
    public List<String> names() {
        List<String> names = new ArrayList<>();
        if (Files.isDirectory(dir)) {
            try (Stream<Path> list = Files.list(dir)) {
                list.filter(Files::isDirectory).map(p -> p.getFileName().toString())
                        .filter(n -> NAME.matcher(n).matches()).sorted().forEach(names::add);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
        if (!names.contains(DEFAULT) && repositoryDefault != null && Files.isDirectory(repositoryDefault)) {
            names.add(0, DEFAULT);
        }
        return names;
    }

    /** Each Deck named in decks.properties and its pack. */
    public synchronized Map<String, String> assignments() {
        Map<String, String> out = new TreeMap<>();
        Properties p = load();
        for (String deck : p.stringPropertyNames()) {
            out.put(deck, p.getProperty(deck).trim());
        }
        return out;
    }

    /**
     * Gives a Deck a pack (null or "default": the default again). The Deck asks every minute,
     * so a connected one gets it within a minute; a Deck not connected at its next connection.
     */
    public synchronized void assign(String deck, String name) {
        if (deck == null || !DECK.matcher(deck).matches()) {
            throw new IllegalArgumentException("a Deck id is 1-24 hex digits");
        }
        String pack = name == null || name.isBlank() ? DEFAULT : name.trim();
        if (!NAME.matcher(pack).matches() || !names().contains(pack)) {
            throw new IllegalArgumentException("no pack '" + pack + "' (there are " + names() + ")");
        }
        Properties p = load();
        p.remove(deck);
        p.remove(deck.toUpperCase());
        if (!DEFAULT.equals(pack)) {
            p.setProperty(deck.toUpperCase(), pack);
        }
        try {
            Files.createDirectories(dir);
            try (var out = Files.newOutputStream(dir.resolve("decks.properties"))) {
                p.store(out, "Deck id = pack (docs/04-PROTOCOL.md, \"The pack\"); absent: default");
            }
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private Properties load() {
        Properties p = new Properties();
        Path map = dir.resolve("decks.properties");
        if (Files.isRegularFile(map)) {
            try (var in = Files.newInputStream(map)) {
                p.load(in);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
        return p;
    }

    /** The pack named for a Deck in decks.properties, or "default". */
    public synchronized String assigned(String deck) {
        if (deck != null) {
            Properties p;
            try {
                p = load();
            } catch (UncheckedIOException e) {
                return DEFAULT;
            }
            String name = p.getProperty(deck, p.getProperty(deck.toUpperCase(), DEFAULT)).trim();
            if (NAME.matcher(name).matches()) {
                return name;
            }
        }
        return DEFAULT;
    }

    private static void collect(Path root, Path folder, TreeMap<String, Path> files, boolean boardOwn) {
        try (Stream<Path> walk = Files.walk(folder)) {
            walk.filter(Files::isRegularFile).forEach(file -> {
                String path = root.relativize(file).toString().replace('\\', '/');
                if (!boardOwn && path.startsWith("@")) {
                    return;  // another board's files
                }
                if (PATH.matcher(path).matches() && !path.contains("..") && !path.equals("version") && !path.equals("manifest")
                        && !path.startsWith("new/")) {
                    files.put(path, file);
                }
            });
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static Pack build(String name, TreeMap<String, Path> files) {
        try {
            ByteArrayOutputStream body = new ByteArrayOutputStream();
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            for (var entry : files.entrySet()) {
                byte[] content = Files.readAllBytes(entry.getValue());
                if (content.length > MAX_FILE_BYTES) {
                    throw new IllegalStateException("pack " + name + ": " + entry.getKey() + " is over "
                            + MAX_FILE_BYTES + " bytes");
                }
                byte[] header = ("file " + entry.getKey() + " " + content.length + "\n").getBytes(StandardCharsets.US_ASCII);
                body.write(header);
                body.write(content);
                digest.update(header);
                digest.update(content);
            }
            String version = HexFormat.of().formatHex(digest.digest()).substring(0, 12);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            out.write(("DECKPACK 1\nversion " + version + "\n").getBytes(StandardCharsets.US_ASCII));
            body.writeTo(out);
            out.write("end\n".getBytes(StandardCharsets.US_ASCII));
            if (out.size() > MAX_PACK_BYTES) {
                throw new IllegalStateException("pack " + name + " is over " + MAX_PACK_BYTES + " bytes");
            }
            return new Pack(name, version, out.toByteArray(), files.size());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }
}
