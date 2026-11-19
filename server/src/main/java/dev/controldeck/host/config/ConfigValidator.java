package dev.controldeck.host.config;

import dev.controldeck.host.util.Values;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * Checks for values written to the configuration. Each check throws
 * {@link IllegalArgumentException} with the message the API reports; a value that
 * passes is returned in the form the configuration stores.
 */
final class ConfigValidator {

    private static final Pattern DECK_ID = Pattern.compile("^[0-9A-Za-z:_-]{1,32}$");
    private static final Pattern IPV4 = Pattern.compile("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$");
    private static final Pattern IPV6 = Pattern.compile("^[0-9A-Fa-f:.]+$");

    private ConfigValidator() {
    }

    /** Macro list: numeric ids from 1 to {@link ConfigService#MAX_MACRO_ID}, no duplicates, a label of 1 to the maximum length. */
    static void macros(List<Map<String, Object>> macros) {
        Set<Long> seen = new HashSet<>();
        for (Map<String, Object> macro : macros) {
            Long id = macro == null ? null : Values.validateInt(macro.get("id"));
            if (id == null) {
                throw new IllegalArgumentException("Macro ID must be numeric");
            }
            if (id < 1 || id > ConfigService.MAX_MACRO_ID) {
                throw new IllegalArgumentException("Macro ID must be between 1 and " + ConfigService.MAX_MACRO_ID);
            }
            if (!seen.add(id)) {
                throw new IllegalArgumentException("Duplicate macro ID " + id);
            }
            if (!(macro.get("label") instanceof String label) || label.isBlank()) {
                throw new IllegalArgumentException("Macro label is required");
            }
            if (Values.codePointLength(label) > ConfigService.MAX_LABEL_LENGTH) {
                throw new IllegalArgumentException("Label exceeds " + ConfigService.MAX_LABEL_LENGTH + " characters");
            }
        }
    }

    /** Serial port: "auto" or a device name, 1 to 255 characters. Returns it trimmed. */
    static String serialPort(Object port) {
        if (!(port instanceof String text) || text.isBlank() || text.length() > 255) {
            throw new IllegalArgumentException("port must be \"auto\" or a device name such as /dev/ttyACM0 or COM4");
        }
        return text.trim();
    }

    /** Baud rate: a positive integer up to 4000000. */
    static int baud(Object value) {
        Long baud = Values.validateInt(value);
        if (baud == null || baud <= 0 || baud > 4_000_000) {
            throw new IllegalArgumentException("baud must be a positive integer");
        }
        return baud.intValue();
    }

    /** Telemetry history: 20 to 240 minutes. */
    static int historyMinutes(int minutes) {
        if (minutes < 20 || minutes > 240) {
            throw new IllegalArgumentException("history_minutes must be an integer between 20 and 240");
        }
        return minutes;
    }

    /** Language code: two lower-case letters. */
    static String language(String language) {
        if (language == null || !language.matches("[a-z]{2}")) {
            throw new IllegalArgumentException("language must be two lower-case letters (en, pt, es ...)");
        }
        return language;
    }

    /** Deck id: 1 to 32 letters, digits, ':', '_' or '-'. */
    static String deckId(String deckId) {
        if (deckId == null || !DECK_ID.matcher(deckId).matches()) {
            throw new IllegalArgumentException("Invalid Deck id");
        }
        return deckId;
    }

    /** Wi-Fi bind address: an IP address or "localhost". */
    static String bind(Object bind) {
        if (!(bind instanceof String text) || !(isIpAddress(text) || "localhost".equals(text))) {
            throw new IllegalArgumentException(
                    "bind must be an IP address (for example 127.0.0.1, 0.0.0.0 or a LAN address)");
        }
        return text;
    }

    /** TCP port: an integer from 1 to 65535. */
    static int port(Object value) {
        Long port = Values.validateInt(value);
        if (port == null || port < 1 || port > 65535) {
            throw new IllegalArgumentException("port must be an integer between 1 and 65535");
        }
        return port.intValue();
    }

    /** IPv4 dotted address (each octet up to 255) or a loose IPv6 literal. */
    static boolean isIpAddress(String text) {
        if (IPV4.matcher(text).matches()) {
            for (String octet : text.split("\\.")) {
                if (Integer.parseInt(octet) > 255) {
                    return false;
                }
            }
            return true;
        }
        // IPv6 literal (loose check: hex digits, colons and an optional embedded IPv4).
        return text.contains(":") && IPV6.matcher(text).matches();
    }
}
