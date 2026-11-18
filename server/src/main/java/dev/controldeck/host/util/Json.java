package dev.controldeck.host.util;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Map;

/** JSON encoding/decoding and atomic file replacement shared by the whole host. */
public final class Json {

    private Json() {
    }

    private static final ObjectMapper MAPPER = new ObjectMapper()
            .configure(DeserializationFeature.USE_BIG_DECIMAL_FOR_FLOATS, false)
            .configure(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS, true);

    private static final ObjectMapper PRETTY = MAPPER.copy()
            .enable(SerializationFeature.INDENT_OUTPUT);

    /** Compact JSON, like json_encode() with JSON_UNESCAPED_SLASHES. */
    public static String encode(Object value) {
        try {
            return MAPPER.writeValueAsString(value);
        } catch (JsonProcessingException e) {
            throw new IllegalStateException("JSON encode failed", e);
        }
    }

    /** Indented JSON for configuration files. */
    public static String encodePretty(Object value) {
        try {
            return PRETTY.writeValueAsString(value);
        } catch (JsonProcessingException e) {
            throw new IllegalStateException("JSON encode failed", e);
        }
    }

    /** UTF-8 byte length of the compact encoding. Used for frame-size limits. */
    public static int encodedSize(Object value) {
        return encode(value).getBytes(StandardCharsets.UTF_8).length;
    }

    /**
     * Parse a JSON object. Returns null when the text is not valid JSON or its
     * top-level value is not an object.
     */
    @SuppressWarnings("unchecked")
    public static Map<String, Object> parseObject(String text) {
        try {
            Object value = MAPPER.readValue(text, Object.class);
            return value instanceof Map<?, ?> ? (Map<String, Object>) value : null;
        } catch (IOException e) {
            return null;
        }
    }

    /**
     * Parse any JSON value (object, array, number, string, boolean or null).
     *
     * @throws IllegalArgumentException when the text is not valid JSON
     */
    public static Object parseValue(String text) {
        try {
            return MAPPER.readValue(text, Object.class);
        } catch (IOException e) {
            throw new IllegalArgumentException("Invalid JSON: " + e.getMessage(), e);
        }
    }

    /** Decode JSON into a typed value (records included). Returns null when the text does not match. */
    public static <T> T decode(String text, Class<T> type) {
        try {
            return MAPPER.readValue(text, type);
        } catch (IOException e) {
            return null;
        }
    }

    /** Write text next to the target and move it over the target (atomic on one filesystem). */
    public static void writeAtomic(Path target, String content) throws IOException {
        Path dir = target.toAbsolutePath().getParent();
        Files.createDirectories(dir);
        Path temporary = Files.createTempFile(dir, target.getFileName().toString(), ".tmp");
        try {
            Files.writeString(temporary, content, StandardCharsets.UTF_8);
            try {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING);
            }
        } finally {
            Files.deleteIfExists(temporary);
        }
    }
}
