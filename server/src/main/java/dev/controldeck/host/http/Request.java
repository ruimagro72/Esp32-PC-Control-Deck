package dev.controldeck.host.http;

import dev.controldeck.host.util.Json;

import java.net.InetAddress;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * One HTTP request, detached from the server library so the router and the tests
 * can build requests by hand.
 *
 * @param method        upper-case HTTP method
 * @param path          absolute path without the query string, e.g. "/api/stats"
 * @param query         decoded query parameters (last value wins)
 * @param headers       header names in lower case
 * @param body          the body as text, at most {@link #MAX_BODY_BYTES} + 1 bytes
 * @param remoteAddress literal IP address of the peer
 * @param raw           the body's bytes (a Deck's binary frames, POST /api/deck)
 */
public record Request(String method, String path, Map<String, String> query, Map<String, String> headers,
                      String body, String remoteAddress, byte[] raw) {

    public static final int MAX_BODY_BYTES = 16384;

    private static final Pattern BEARER = Pattern.compile("^Bearer\\s+(\\S+)$", Pattern.CASE_INSENSITIVE);

    public Request {
        method = method.toUpperCase(Locale.ROOT);
        path = "/" + path.replaceAll("^/+|/+$", "");
        query = query == null ? Map.of() : query;
        headers = lowerKeys(headers);
        body = body == null ? "" : body;
        raw = raw == null ? body.getBytes(StandardCharsets.UTF_8) : raw;
    }

    /** A request whose body is text (the REST API, the tests). */
    public Request(String method, String path, Map<String, String> query, Map<String, String> headers,
                   String body, String remoteAddress) {
        this(method, path, query, headers, body, remoteAddress, null);
    }

    /** Parse "a=1&b=two" into a map. Keys and values are URL-decoded; the last duplicate wins. */
    public static Map<String, String> parseQuery(String raw) {
        Map<String, String> query = new LinkedHashMap<>();
        if (raw == null || raw.isEmpty()) {
            return query;
        }
        for (String pair : raw.split("&")) {
            if (pair.isEmpty()) {
                continue;
            }
            int eq = pair.indexOf('=');
            String key = URLDecoder.decode(eq < 0 ? pair : pair.substring(0, eq), StandardCharsets.UTF_8);
            String value = eq < 0 ? "" : URLDecoder.decode(pair.substring(eq + 1), StandardCharsets.UTF_8);
            query.put(key, value);
        }
        return query;
    }

    public String header(String name) {
        return headers.get(name.toLowerCase(Locale.ROOT));
    }

    /** The token of an "Authorization: Bearer <token>" header, or null. */
    public String bearerToken() {
        String header = header("authorization");
        if (header == null) {
            return null;
        }
        Matcher m = BEARER.matcher(header.trim());
        return m.matches() ? m.group(1) : null;
    }

    /**
     * True only for a loopback peer. Proxy headers are deliberately never trusted.
     */
    public boolean isLoopback() {
        try {
            return InetAddress.getByName(remoteAddress).isLoopbackAddress();
        } catch (java.net.UnknownHostException e) {
            return false;
        }
    }

    /** The body as a JSON object. An empty body is an empty object. */
    public Map<String, Object> json() {
        if (body.getBytes(StandardCharsets.UTF_8).length > MAX_BODY_BYTES) {
            throw new HttpError(413, "Request body too large");
        }
        if (body.isBlank()) {
            return new LinkedHashMap<>();
        }
        Map<String, Object> data = Json.parseObject(body);
        if (data == null) {
            throw HttpError.badRequest("Request body must be a valid JSON object");
        }
        return data;
    }

    /** The body as any JSON value (object or list); null for an empty body. */
    public Object jsonAny() {
        if (body.getBytes(StandardCharsets.UTF_8).length > MAX_BODY_BYTES) {
            throw new HttpError(413, "Request body too large");
        }
        if (body.isBlank()) {
            return null;
        }
        try {
            return Json.parseValue(body);
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest("Request body must be valid JSON");
        }
    }

    private static Map<String, String> lowerKeys(Map<String, String> source) {
        Map<String, String> lower = new LinkedHashMap<>();
        if (source != null) {
            source.forEach((k, v) -> lower.put(k.toLowerCase(Locale.ROOT), v));
        }
        return lower;
    }
}
