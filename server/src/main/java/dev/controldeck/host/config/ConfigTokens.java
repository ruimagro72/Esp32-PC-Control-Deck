package dev.controldeck.host.config;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.HexFormat;

/**
 * Peer token generation and hashing. Only the SHA-256 of a token is ever stored;
 * the plain value is handed out once.
 */
final class ConfigTokens {

    private static final SecureRandom RANDOM = new SecureRandom();

    private ConfigTokens() {
    }

    /** A new random token: 24 bytes as 48 hex characters. */
    static String newToken() {
        byte[] bytes = new byte[24];
        RANDOM.nextBytes(bytes);
        return HexFormat.of().formatHex(bytes);
    }

    /** SHA-256 of the UTF-8 text, as lower-case hex. */
    static String sha256Hex(String text) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));
            return HexFormat.of().formatHex(digest);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is not available", e);
        }
    }
}
