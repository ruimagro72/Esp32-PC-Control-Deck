package dev.controldeck.host.http;

import static dev.controldeck.host.http.RouteSupport.intParam;
import static dev.controldeck.host.http.RouteSupport.success;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.telemetry.HistoryStore;
import dev.controldeck.host.telemetry.MetricRegistry;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Telemetry, history, metrics and the PC inventory (peer reads). */
final class TelemetryRoutes {

    private final Services services;

    TelemetryRoutes(Services services) {
        this.services = services;
    }

    Response stats() {
        var sample = services.sampler().sample(1.0);
        Map<String, Object> body = success(sample.frame());
        body.put("timestamp", (long) sample.time());
        return Response.ok(body);
    }

    Response gpu() {
        var sample = services.sampler().sample(1.0);
        List<String> fields = List.of("gpu", "vram", "vram_used_gb", "vram_total_gb", "gpu_temp", "gpu_clock_mhz", "gpu_power_watts");
        Map<String, Object> data = new LinkedHashMap<>();
        for (String field : fields) {
            data.put(field, sample.frame().get(field));
        }
        Map<String, Object> body = success(data);
        body.put("timestamp", (long) sample.time());
        body.put("note", "null means the driver does not expose the value; nothing is estimated.");
        return Response.ok(body);
    }

    Response metrics() {
        HistoryStore history = services.sampler().history();
        List<Map<String, Object>> metrics = new ArrayList<>();
        for (MetricRegistry.Metric metric : MetricRegistry.all()) {
            Map<String, Object> entry = metric.toMap();
            entry.put("available", history.hasData(metric.id()));
            metrics.add(entry);
        }
        Map<String, Object> defaults = new LinkedHashMap<>();
        for (String component : MetricRegistry.COMPONENTS) {
            defaults.put(component, MetricRegistry.defaultFor(component));
        }
        Map<String, Object> data = new LinkedHashMap<>();
        data.put("metrics", metrics);
        data.put("components", MetricRegistry.COMPONENTS);
        data.put("default_metric", defaults);
        data.put("windows_minutes", MetricRegistry.WINDOWS);
        data.put("retention_minutes", history.retentionSeconds() / 60);
        return Response.ok(success(data));
    }

    /** GET /api/history?metric=cpu_usage&minutes=10&points=60 or ?component=network&minutes=5 */
    Response history(Map<String, String> query) {
        HistoryStore history = services.sampler().history();
        int retentionMinutes = history.retentionSeconds() / 60;
        int minutes = intParam(query, "minutes", 5, 1, retentionMinutes);
        int points = intParam(query, "points", Protocol.DEFAULT_HISTORY_POINTS, 2, 240);

        // Record a sample now so the newest point is part of the answer.
        services.sampler().sample(1.0);

        try {
            if (query.containsKey("metric")) {
                String id = query.get("metric");
                if (!MetricRegistry.has(id)) {
                    throw HttpError.notFound("Unknown metric '" + id + "'");
                }
                return Response.ok(success(history.series(id, minutes, points, null).toMap()));
            }
            if (query.containsKey("component")) {
                String component = query.get("component");
                if (!MetricRegistry.COMPONENTS.contains(component)) {
                    throw HttpError.notFound("Unknown component '" + component + "'");
                }
                List<Map<String, Object>> series = new ArrayList<>();
                for (String id : MetricRegistry.forComponent(component)) {
                    series.add(history.series(id, minutes, points, null).toMap());
                }
                Map<String, Object> data = new LinkedHashMap<>();
                data.put("component", component);
                data.put("series", series);
                return Response.ok(success(data));
            }
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
        throw HttpError.badRequest("Specify ?metric=<id> or ?component=<name> (see GET /api/metrics)");
    }

    Response inventory(String component) {
        if (component == null) {
            return Response.ok(success(services.inventory().all()));
        }
        Map<String, Object> data = services.inventory().component(component);
        if (data == null) {
            throw HttpError.notFound("Unknown component '" + component + "'");
        }
        Map<String, Object> wrapped = new LinkedHashMap<>();
        wrapped.put(component, data);
        return Response.ok(success(wrapped));
    }
}
