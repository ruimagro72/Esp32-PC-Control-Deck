package dev.controldeck.host.firmware;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HexFormat;
import java.util.Optional;
import java.util.regex.Pattern;

/**
 * The firmware images the server offers to the Decks, one per board: {@code <dir>/<board>.bin}
 * (for example {@code var/firmware/es3c28p.bin}, copied there by {@code ./start.sh firmware publish}).
 *
 * <p>An image says which version it is: the firmware carries the text {@code DECKFW:} followed
 * by its version ({@code app/state.h}), so nothing beside the file has to be kept in step.
 */
public final class FirmwareStore {

    /** The largest image: an app slot of the ES3C28P (6 MB, the Deck refuses more). */
    public static final long MAX_IMAGE_BYTES = 6L * 1024 * 1024;

    private static final Pattern BOARD = Pattern.compile("[a-z0-9_]{1,24}");
    private static final byte[] TAG = "DECKFW:".getBytes(StandardCharsets.US_ASCII);
    private static final int MAX_VERSION_LENGTH = 40;

    /** An image found for a board. */
    public record Image(String board, String version, byte[] bytes, String sha256, String md5) {
        public long size() {
            return bytes.length;
        }
    }

    private final Path dir;
    private volatile Image cached;
    private volatile long cachedModified;

    public FirmwareStore(Path dir) {
        this.dir = dir;
    }

    public Path dir() {
        return dir;
    }

    /** True for a board name the store may look up (a file name, never a path). */
    public static boolean validBoard(String board) {
        return board != null && BOARD.matcher(board).matches();
    }

    /** The board's image, or empty when there is none (or it is not a Deck image). */
    public Optional<Image> find(String board) {
        if (!validBoard(board)) {
            return Optional.empty();
        }
        Path file = dir.resolve(board + ".bin");
        try {
            if (!Files.isRegularFile(file) || Files.size(file) > MAX_IMAGE_BYTES) {
                return Optional.empty();
            }
            long modified = Files.getLastModifiedTime(file).toMillis();
            Image last = cached;
            if (last != null && last.board().equals(board) && cachedModified == modified) {
                return Optional.of(last);
            }
            byte[] bytes = Files.readAllBytes(file);
            if (!FirmwareSigner.hasTrailer(bytes)) {   // only signed images are offered (docs/firmware/OTA.md)
                return Optional.empty();
            }
            String version = versionOf(bytes);
            if (version == null) {
                return Optional.empty();
            }
            Image image = new Image(board, version, bytes, sha256(bytes), digest("MD5", bytes));
            cached = image;
            cachedModified = modified;
            return Optional.of(image);
        } catch (IOException e) {
            return Optional.empty();
        }
    }

    /** The version after {@code DECKFW:} in the image, or null when the tag is missing. */
    public static String versionOf(byte[] image) {
        outer:
        for (int i = 0; i + TAG.length < image.length; i++) {
            for (int j = 0; j < TAG.length; j++) {
                if (image[i + j] != TAG[j]) {
                    continue outer;
                }
            }
            StringBuilder version = new StringBuilder();
            for (int k = i + TAG.length; k < image.length && version.length() <= MAX_VERSION_LENGTH; k++) {
                int c = image[k] & 0xFF;
                if (c == 0) {
                    return version.length() == 0 ? null : version.toString();
                }
                if (c < 0x21 || c > 0x7E) {
                    break;
                }
                version.append((char) c);
            }
        }
        return null;
    }

    public static String sha256(byte[] bytes) {
        return digest("SHA-256", bytes);
    }

    /** MD5 is what the ESP32's update library checks the image against (integrity, not trust). */
    static String digest(String algorithm, byte[] bytes) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance(algorithm).digest(bytes));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }
}
