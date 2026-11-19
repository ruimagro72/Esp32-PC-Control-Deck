package dev.controldeck.host.http;

import java.util.LinkedHashMap;
import java.util.Map;

/** Helpers shared by the route groups (see {@link Router}). */
final class RouteSupport {

    private RouteSupport() {
    }

    static int intParam(Map<String, String> query, String name, int fallback, int min, int max) {
        String raw = query.get(name);
        if (raw == null) {
            return fallback;
        }
        Long value = dev.controldeck.host.util.Values.validateInt(raw);
        if (value == null || value < min || value > max) {
            throw HttpError.badRequest(name + " must be an integer between " + min + " and " + max);
        }
        return value.intValue();
    }

    static Map<String, Object> success(Map<String, Object> data) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("data", data);
        return body;
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> castMap(Object value) {
        return value instanceof Map<?, ?> ? (Map<String, Object>) value : new LinkedHashMap<>();
    }
}
