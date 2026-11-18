package dev.controldeck.host.macro;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

/**
 * The macro definitions (create, change, delete, reorder) and the macro events. A peer only ever supplies the
 * numeric id of a macro; what runs comes from the local configuration.
 *
 * The writers are serialized, so two requests at the same time cannot lose an update.
 */
public final class MacroService {

    private static final List<String> FIELDS = List.of(
            "label", "action", "value", "hidden", "working_directory", "new_console", "health_url");

    /** No macro with that id. */
    public static final class NotFound extends RuntimeException {
        private static final long serialVersionUID = 1L;

        NotFound(String message) {
            super(message);
        }
    }

    /** A macro with that id already exists. */
    public static final class Conflict extends RuntimeException {
        private static final long serialVersionUID = 1L;

        Conflict(String message) {
            super(message);
        }
    }

    private final ConfigService config;
    private final MacroExecutor executor;
    private final Consumer<String> eventLog;

    public MacroService(ConfigService config, MacroExecutor executor) {
        this(config, executor, line -> { });
    }

    /** @param eventLog receives one line per macro event (what ran, or why not) */
    public MacroService(ConfigService config, MacroExecutor executor, Consumer<String> eventLog) {
        this.config = config;
        this.executor = executor;
        this.eventLog = eventLog;
    }

    // -- definitions ---------------------------------------------------------------

    /** All macros; {@code sanitized} hides commands, paths and addresses (for remote peers). */
    public Map<String, Object> list(boolean sanitized) {
        List<Map<String, Object>> data = new ArrayList<>();
        for (Map<String, Object> raw : config.getMacros()) {
            Macro macro = Macro.fromMap(raw);
            data.add(sanitized ? macro.toSanitizedMap() : macro.toMap());
        }
        Map<String, Object> body = success(data);
        body.put("count", data.size());
        return body;
    }

    public Map<String, Object> show(int id) {
        Macro macro = find(id);
        if (macro == null) {
            throw new NotFound("Macro with ID " + id + " not found");
        }
        return success(macro.toMap());
    }

    public synchronized Map<String, Object> create(Map<String, Object> data) {
        Long id = Values.validateInt(data.get("id"));
        if (id == null) {
            throw new IllegalArgumentException("ID is required and must be numeric");
        }
        if (id < 1 || id > ConfigService.MAX_MACRO_ID) {
            throw new IllegalArgumentException("ID must be between 1 and " + ConfigService.MAX_MACRO_ID);
        }
        if (find(id.intValue()) != null) {
            throw new Conflict("Macro with ID " + id + " already exists");
        }

        Map<String, Object> macro = new LinkedHashMap<>();
        macro.put("id", id.intValue());
        macro.put("action", data.getOrDefault("action", "command"));
        macro.put("value", data.get("value") != null ? data.get("value") : List.of());
        for (String field : FIELDS) {
            if (data.get(field) != null && !macro.containsKey(field)) {
                macro.put(field, data.get(field));
            }
        }

        Macro checked = Macro.fromMap(macro); // validates the action and the label length
        List<Map<String, Object>> macros = new ArrayList<>(config.getMacros());
        macros.add(macro);
        config.setMacros(macros);

        Map<String, Object> body = success(checked.toMap());
        body.put("message", "Macro created successfully");
        return body;
    }

    public synchronized Map<String, Object> update(int id, Map<String, Object> data) {
        List<Map<String, Object>> macros = new ArrayList<>(config.getMacros());
        int index = indexOf(macros, id);
        if (index < 0) {
            throw new NotFound("Macro with ID " + id + " not found");
        }

        Map<String, Object> merged = new LinkedHashMap<>(macros.get(index));
        boolean changed = false;
        for (String field : FIELDS) {
            if (data.containsKey(field)) {
                Object value = data.get(field);
                merged.put(field, field.equals("hidden") || field.equals("new_console")
                        ? ConfigService.truthy(value) : value);
                changed = true;
            }
        }
        if (!changed) {
            return success(Macro.fromMap(macros.get(index)).toMap());
        }

        Macro.fromMap(merged); // validates before anything is written
        merged.put("id", id);
        macros.set(index, merged);
        config.setMacros(macros);

        Map<String, Object> body = success(Macro.fromMap(merged).toMap());
        body.put("message", "Macro updated successfully");
        return body;
    }

    public synchronized Map<String, Object> delete(int id) {
        List<Map<String, Object>> macros = new ArrayList<>(config.getMacros());
        int index = indexOf(macros, id);
        if (index < 0) {
            throw new NotFound("Macro with ID " + id + " not found");
        }
        macros.remove(index);
        config.setMacros(macros);

        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("message", "Macro with ID " + id + " deleted successfully");
        return body;
    }

    /** Body: [3, 1, 2] or {"ids": [3, 1, 2]}; it must list every existing macro exactly once. */
    public synchronized Map<String, Object> reorder(Object body) {
        Object ids = body instanceof Map<?, ?> map ? map.get("ids") : body;
        if (!(ids instanceof List<?> list) || list.isEmpty()) {
            throw new IllegalArgumentException("Expected a list of macro IDs");
        }
        List<Integer> requested = new ArrayList<>();
        for (Object item : list) {
            Long id = Values.validateInt(item);
            if (id == null) {
                throw new IllegalArgumentException("Invalid macro ID");
            }
            requested.add(id.intValue());
        }

        List<Map<String, Object>> macros = config.getMacros();
        List<Integer> existing = new ArrayList<>();
        for (Map<String, Object> macro : macros) {
            Long id = Values.validateInt(macro.get("id"));
            existing.add(id == null ? 0 : id.intValue());
        }
        List<Integer> sortedExisting = new ArrayList<>(existing);
        List<Integer> sortedRequested = new ArrayList<>(requested);
        sortedExisting.sort(null);
        sortedRequested.sort(null);
        if (!sortedExisting.equals(sortedRequested)) {
            throw new IllegalArgumentException("The reorder list must contain every macro ID exactly once");
        }

        List<Map<String, Object>> ordered = new ArrayList<>();
        for (int id : requested) {
            ordered.add(macros.get(existing.indexOf(id)));
        }
        config.setMacros(ordered);

        List<Map<String, Object>> data = new ArrayList<>();
        for (Map<String, Object> macro : ordered) {
            data.add(Macro.fromMap(macro).toMap());
        }
        Map<String, Object> result = success(data);
        result.put("message", "Macros reordered successfully");
        return result;
    }

    /** Sanitized view of one macro (no command, path or address). */
    public Map<String, Object> eventConfig(int id) {
        Macro macro = find(id);
        if (macro == null) {
            Map<String, Object> body = new LinkedHashMap<>();
            body.put("success", false);
            body.put("error", "Macro with ID " + id + " not found");
            return body;
        }
        return success(macro.toSanitizedMap());
    }

    // -- events ----------------------------------------------------------------------

    /**
     * "Macro N was pressed". Never throws: a failure is a result with success false.
     * success true means the macro exists and its action type is supported;
     * {@code action_executed} says whether it really ran.
     */
    public Map<String, Object> handleEvent(int id) {
        Map<String, Object> result = new LinkedHashMap<>();
        if (id < 1 || id > ConfigService.MAX_MACRO_ID) {
            result.put("success", false);
            result.put("error", "Macro ID must be between 1 and " + ConfigService.MAX_MACRO_ID);
            result.put("macro_id", id);
            return result;
        }
        try {
            Macro macro = find(id);
            if (macro == null) {
                result.put("success", false);
                result.put("error", "Macro with ID " + id + " not found");
                result.put("macro_id", id);
                eventLog.accept("Macro " + id + ": not configured");
                return result;
            }

            MacroExecutor.Result run = executor.execute(macro);
            eventLog.accept("Macro " + id + " (" + macro.label() + "): " + run.message());
            if (run.unsupported()) {
                result.put("success", false);
                result.put("error", run.message());
                result.put("macro_id", id);
                result.put("label", macro.label());
                result.put("action_executed", false);
                return result;
            }
            result.put("success", true);
            result.put("macro_id", id);
            result.put("label", macro.label());
            result.put("action_executed", run.executed());
            result.put("status", run.status());
            result.put("message", run.message());
            return result;
        } catch (RuntimeException error) {
            result.clear();
            result.put("success", false);
            result.put("error", String.valueOf(error.getMessage()));
            result.put("macro_id", id);
            return result;
        }
    }

    /** Accepts [1, 2], [{"id": 1}, {"id": 2}] or a mix; at most six events. */
    public Map<String, Object> batch(List<?> events) {
        Map<String, Object> body = new LinkedHashMap<>();
        if (events.isEmpty()) {
            body.put("success", false);
            body.put("error", "No events provided");
            return body;
        }
        if (events.size() > ConfigService.MAX_MACRO_ID) {
            body.put("success", false);
            body.put("error", "A batch holds at most " + ConfigService.MAX_MACRO_ID + " events");
            return body;
        }

        List<Map<String, Object>> results = new ArrayList<>();
        int processed = 0;
        for (Object event : events) {
            Object raw = event instanceof Map<?, ?> map ? map.get("id") : event;
            Long id = Values.isNumeric(raw) ? Values.validateInt(raw) : null;
            if (id == null) {
                Map<String, Object> invalid = new LinkedHashMap<>();
                invalid.put("success", false);
                invalid.put("error", "Invalid event: expected a numeric id or {\"id\": <number>}");
                results.add(invalid);
                continue;
            }
            Map<String, Object> result = handleEvent(id.intValue());
            if (Boolean.TRUE.equals(result.get("success"))) {
                processed++;
            }
            results.add(result);
        }
        body.put("success", true);
        body.put("processed", processed);
        body.put("total", events.size());
        body.put("results", results);
        return body;
    }

    // -- helpers ---------------------------------------------------------------------

    /** The macro with this id, or null. */
    public Macro find(int id) {
        for (Map<String, Object> raw : config.getMacros()) {
            Long macroId = Values.validateInt(raw.get("id"));
            if (macroId != null && macroId == id) {
                return Macro.fromMap(raw);
            }
        }
        return null;
    }

    private static int indexOf(List<Map<String, Object>> macros, int id) {
        for (int i = 0; i < macros.size(); i++) {
            Long macroId = Values.validateInt(macros.get(i).get("id"));
            if (macroId != null && macroId == id) {
                return i;
            }
        }
        return -1;
    }

    private static Map<String, Object> success(Object data) {
        Map<String, Object> body = new LinkedHashMap<>();
        body.put("success", true);
        body.put("data", data);
        return body;
    }
}
