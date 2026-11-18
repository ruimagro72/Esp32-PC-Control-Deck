package dev.controldeck.host.security;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.http.HttpError;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Peer authentication for the LAN (Wi-Fi) transport.
 *
 *  - A request from the loopback interface is the local user and needs no token.
 *  - Any other peer must present {@code Authorization: Bearer <token>}: the shared token
 *    (transports.wifi.token_hash) or the token of one Deck (transports.wifi.deck_tokens,
 *    handed to the Deck over USB). Only SHA-256 hashes are stored.
 *  - Without a token, or with the Wi-Fi transport disabled, remote peers are
 *    rejected outright.
 *  - Repeated failures from one address are throttled (10 in 60 seconds, HTTP 429).
 */
public final class PeerAuth {

    private static final int MAX_FAILURES = 10;
    private static final double FAILURE_WINDOW_SECONDS = 60.0;

    private final ConfigService config;
    private final Path failuresFile;

    public PeerAuth(ConfigService config, Path varDir) {
        this.config = config;
        this.failuresFile = varDir.resolve("auth_failures.json");
    }

    /** @throws HttpError when the peer is not allowed */
    public synchronized void authorize(Request request) {
        if (request.isLoopback()) {
            return;
        }

        ConfigService.WifiSettings wifi = config.getWifiSettings();
        if (!wifi.enabled()) {
            throw HttpError.forbidden("The Wi-Fi transport is disabled on this host");
        }
        if (!wifi.anyToken()) {
            throw HttpError.forbidden("No peer token is configured on this host");
        }

        assertNotThrottled(request.remoteAddress());

        String token = request.bearerToken();
        if (token == null) {
            recordFailure(request.remoteAddress());
            throw HttpError.unauthorized("Missing bearer token");
        }
        byte[] presented = ConfigService.sha256Hex(token).getBytes(StandardCharsets.UTF_8);
        List<String> accepted = new ArrayList<>(wifi.deckTokenHashes());
        if (wifi.tokenHash() != null && !wifi.tokenHash().isEmpty()) {
            accepted.add(wifi.tokenHash());
        }
        boolean match = false;
        for (String hash : accepted) {  // every hash is compared: no early exit
            match |= MessageDigest.isEqual(presented, hash.getBytes(StandardCharsets.UTF_8));
        }
        if (!match) {
            recordFailure(request.remoteAddress());
            throw HttpError.unauthorized("Invalid bearer token");
        }
    }

    private void assertNotThrottled(String address) {
        double cutoff = now() - FAILURE_WINDOW_SECONDS;
        int recent = 0;
        for (Double time : failures().getOrDefault(address, List.of())) {
            if (time > cutoff) {
                recent++;
            }
        }
        if (recent >= MAX_FAILURES) {
            throw HttpError.tooManyRequests("Too many failed authentication attempts; try again later");
        }
    }

    private void recordFailure(String address) {
        double now = now();
        double cutoff = now - FAILURE_WINDOW_SECONDS;
        Map<String, List<Double>> failures = failures();

        List<Double> own = new ArrayList<>();
        for (Double time : failures.getOrDefault(address, List.of())) {
            if (time > cutoff) {
                own.add(time);
            }
        }
        own.add(now);
        failures.put(address, own);

        failures.entrySet().removeIf(entry -> entry.getValue().isEmpty()
                || entry.getValue().stream().mapToDouble(Double::doubleValue).max().orElse(0) < cutoff);
        try {
            Json.writeAtomic(failuresFile, Json.encode(failures));
        } catch (IOException e) {
            // Losing a failure record only weakens the throttle; the request is still rejected.
        }
    }

    private Map<String, List<Double>> failures() {
        Map<String, List<Double>> result = new LinkedHashMap<>();
        try {
            if (!Files.isRegularFile(failuresFile)) {
                return result;
            }
            Map<String, Object> parsed = Json.parseObject(Files.readString(failuresFile));
            if (parsed == null) {
                return result;
            }
            parsed.forEach((address, times) -> {
                List<Double> list = new ArrayList<>();
                if (times instanceof List<?> items) {
                    for (Object item : items) {
                        if (item instanceof Number n) {
                            list.add(n.doubleValue());
                        }
                    }
                }
                result.put(address, list);
            });
        } catch (IOException e) {
            // Unreadable record: start from an empty throttle.
        }
        return result;
    }

    private static double now() {
        return System.currentTimeMillis() / 1000.0;
    }
}
