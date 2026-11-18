package dev.controldeck.host.macro;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.util.Values;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * One touch-button action. This is executable
 * configuration: the board only ever sends the numeric id, never a command.
 *
 * @param value            url: the address; command/service: an argument list, or a map of
 *                         argument lists per platform ("linux", "macos", "windows", "default")
 * @param workingDirectory a path, or a map of paths per platform; null for none
 * @param healthUrl        service: the http(s) address that answers when the service runs
 */
public record Macro(int id, String label, String action, Object value, boolean hidden,
                    Object workingDirectory, boolean newConsole, String healthUrl) {

    public static final List<String> ACTIONS = List.of("url", "command", "service", "hotkey", "text");

    /** Validates the action and the label length. */
    public Macro {
        if (!ACTIONS.contains(action)) {
            throw new IllegalArgumentException("Unknown macro action '" + action + "'");
        }
        if (Values.codePointLength(label) > ConfigService.MAX_LABEL_LENGTH) {
            throw new IllegalArgumentException("Label must be " + ConfigService.MAX_LABEL_LENGTH + " characters or less");
        }
    }

    public static Macro fromMap(Map<String, Object> data) {
        Long id = Values.validateInt(data.get("id"));
        Object label = data.get("label");
        Object action = data.get("action");
        Object health = data.get("health_url");
        return new Macro(
                id == null ? 0 : id.intValue(),
                label == null ? "" : String.valueOf(label),
                action == null ? "command" : String.valueOf(action),
                data.containsKey("value") && data.get("value") != null ? data.get("value") : List.of(),
                ConfigService.truthy(data.get("hidden")),
                data.get("working_directory"),
                ConfigService.truthy(data.get("new_console")),
                health == null ? null : String.valueOf(health));
    }

    public Map<String, Object> toMap() {
        Map<String, Object> map = new LinkedHashMap<>();
        map.put("id", id);
        map.put("label", label);
        map.put("action", action);
        map.put("value", value);
        map.put("hidden", hidden);
        map.put("working_directory", workingDirectory);
        map.put("new_console", newConsole);
        map.put("health_url", healthUrl);
        return map;
    }

    /** What a remote peer may see: no command, path or address. */
    public Map<String, Object> toSanitizedMap() {
        Map<String, Object> map = new LinkedHashMap<>();
        map.put("id", id);
        map.put("label", label);
        map.put("action", action);
        map.put("hidden", hidden);
        return map;
    }
}
