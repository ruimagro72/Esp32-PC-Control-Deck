package dev.controldeck.host.http;

import dev.controldeck.host.Services;
import dev.controldeck.host.util.Values;

import java.util.List;
import java.util.Map;

/** Macro events (by id only, peer): run one macro or a batch. */
final class MacroRoutes {

    private final Services services;

    MacroRoutes(Services services) {
        this.services = services;
    }

    /** POST /api/event {"id": 3}: run macro 3. */
    Response event(Request request) {
        Map<String, Object> data = request.json();
        Long id = Values.isNumeric(data.get("id")) ? Values.validateInt(data.get("id")) : null;
        if (id == null || id > Integer.MAX_VALUE || id < Integer.MIN_VALUE) {
            throw HttpError.badRequest("Invalid event format. Expected {\"id\": <numeric_id>}");
        }
        Map<String, Object> result = services.macros().handleEvent(id.intValue());
        if (Boolean.TRUE.equals(result.get("success"))) {
            return Response.ok(result);
        }
        boolean missing = String.valueOf(result.get("error")).contains("not found");
        return Response.of(missing ? 404 : 400, result);
    }

    /** POST /api/event/batch: {"events": [1, 2]}, {"events": [{"id": 1}]} or a bare list. */
    Response eventBatch(Request request) {
        Object data = request.jsonAny();
        Object events = data instanceof Map<?, ?> map ? map.get("events") : data;
        if (!(events instanceof List<?> list)) {
            throw HttpError.badRequest("Invalid batch format. Expected {\"events\": [<id>, ...]}");
        }
        Map<String, Object> result = services.macros().batch(list);
        return Response.of(Boolean.TRUE.equals(result.get("success")) ? 200 : 400, result);
    }
}
