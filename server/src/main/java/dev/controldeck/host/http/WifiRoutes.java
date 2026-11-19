package dev.controldeck.host.http;

import static dev.controldeck.host.http.RouteSupport.castMap;

import dev.controldeck.host.Services;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Wi-Fi transport settings and the peer token (host only). */
final class WifiRoutes {

    private final Services services;

    WifiRoutes(Services services) {
        this.services = services;
    }

    Response setWifi(Map<String, Object> settings) {
        Map<String, Object> allowed = new LinkedHashMap<>();
        for (String key : List.of("enabled", "bind", "port")) {
            if (settings.containsKey(key)) {
                allowed.put(key, settings.get(key));
            }
        }
        if (allowed.isEmpty()) {
            throw HttpError.badRequest("Expected one of: enabled, bind, port");
        }
        try {
            services.config().setWifiSettings(allowed);
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Wi-Fi transport settings saved. Restart the host for a new bind address or port to take effect.");
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }

    Response rotateToken() {
        String token = services.config().rotateWifiToken();
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Store this token on the ESP32 now. It is not stored in clear text and cannot be shown again.");
        body.put("token", token);
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }

    Response clearToken() {
        services.config().clearWifiToken();
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Peer token removed; remote peers are rejected until a new token is created");
        body.put("data", castMap(services.transports().all().get("wifi")));
        return Response.ok(body);
    }
}
