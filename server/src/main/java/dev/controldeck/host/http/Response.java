package dev.controldeck.host.http;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * A reply. The body is any JSON-serializable value (normally a map), or a {@code byte[]} sent
 * as it is (a firmware image); headers are extra response headers, e.g. Allow for 405.
 */
public record Response(int status, Object body, Map<String, String> headers) {

    public Response {
        headers = headers == null ? Map.of() : Map.copyOf(headers);
    }

    public static Response ok(Object body) {
        return new Response(200, body, Map.of());
    }

    /** Bytes sent as they are, as application/octet-stream. */
    public static Response bytes(byte[] body) {
        return new Response(200, body, Map.of("Content-Type", "application/octet-stream"));
    }

    /** Plain text (UTF-8), as text/plain. */
    public static Response text(String body) {
        return new Response(200, body.getBytes(java.nio.charset.StandardCharsets.UTF_8),
                Map.of("Content-Type", "text/plain; charset=utf-8"));
    }

    public static Response of(int status, Object body) {
        return new Response(status, body, Map.of());
    }

    public static Response error(int status, String message) {
        return error(status, message, Map.of());
    }

    public static Response error(int status, String message, Map<String, String> headers) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", false);
        body.put("error", message);
        return new Response(status, body, headers);
    }
}
