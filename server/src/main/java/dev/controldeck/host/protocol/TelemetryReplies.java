package dev.controldeck.host.protocol;

import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.telemetry.MetricRegistry;
import dev.controldeck.host.telemetry.Sampler;
import dev.controldeck.host.util.Values;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static dev.controldeck.host.protocol.MessageHandler.binarySize;
import static dev.controldeck.host.protocol.MessageHandler.map;

/** The stats replies: metrics catalog, history and inventory (docs/03-SERVER.md). */
final class TelemetryReplies {

    private final Sampler sampler;
    private final Inventory inventory;

    TelemetryReplies(Sampler sampler, Inventory inventory) {
        this.sampler = sampler;
        this.inventory = inventory;
    }

    /**
     * The metrics catalog for a Deck (docs/03-SERVER.md), a page from {@code from} that
     * fits the peer's frame. A sample is taken first if none was, so the catalog is not empty.
     */
    Map<String, Object> catalogFrame(Map<String, Object> message, int maxFrameBytes) {
        Long from = Values.validateInt(message.get("from"));
        if (sampler.catalog().entries().isEmpty()) {
            sampler.sample(1.0);
        }
        return sampler.catalog().frame(from == null ? 0 : from.intValue(), maxFrameBytes);
    }

    Map<String, Object> historyFrame(Map<String, Object> message, int maxFrameBytes) {
        Object metricValue = message.get("metric");
        if (!(metricValue instanceof String metric) || !MetricRegistry.has(metric)) {
            throw new IllegalArgumentException("Unknown or missing metric");
        }

        Long minutes = Values.validateInt(message.getOrDefault("minutes", 5));
        if (minutes == null || !MetricRegistry.isValidWindow(minutes)) {
            throw new IllegalArgumentException("minutes must be one of " + MetricRegistry.WINDOWS);
        }

        Long requestedPoints = Values.validateInt(message.getOrDefault("points", Protocol.DEFAULT_HISTORY_POINTS));
        if (requestedPoints == null || requestedPoints < Protocol.MIN_HISTORY_POINTS || requestedPoints > Protocol.MAX_HISTORY_POINTS) {
            throw new IllegalArgumentException("points must be between " + Protocol.MIN_HISTORY_POINTS + " and " + Protocol.MAX_HISTORY_POINTS);
        }
        int points = requestedPoints.intValue();

        // Make sure the newest sample is part of the answer.
        sampler.sample(1.0);

        int minutesValue = minutes.intValue();
        var series = sampler.history().series(metric, minutesValue, points, null);
        Map<String, Object> frame = map(
                "type", "history",
                "metric", metric,
                "unit", series.unit(),
                "minutes", minutesValue,
                "step_s", series.stepSeconds(),
                "end", series.end(),
                "values", series.values());

        // Shrink by halving the resolution until the frame fits the peer's limit.
        while (binarySize(frame) > maxFrameBytes - 8 && valuesOf(frame).size() > Protocol.MIN_HISTORY_POINTS) {
            int next = Math.max(Protocol.MIN_HISTORY_POINTS, valuesOf(frame).size() / 2);
            var smaller = sampler.history().series(metric, minutesValue, next, null);
            frame.put("values", smaller.values());
            frame.put("step_s", smaller.stepSeconds());
        }
        if (binarySize(frame) > maxFrameBytes) {
            throw new IllegalArgumentException("Reply does not fit the maximum frame size");
        }
        return frame;
    }

    Map<String, Object> inventoryFrame(Map<String, Object> message, int maxFrameBytes) {
        Object componentValue = message.get("component");
        if (!(componentValue instanceof String component) || !MetricRegistry.COMPONENTS.contains(component)) {
            throw new IllegalArgumentException("component must be one of " + MetricRegistry.COMPONENTS);
        }

        Map<String, Object> data = inventory.component(component);
        if (data == null) {
            data = new LinkedHashMap<>();
        }
        Map<String, Object> frame = map("type", "inventory", "component", component, "data", data);

        // Drop list entries from the tail (extra disks / interfaces) until the frame fits.
        for (String listKey : List.of("disks", "interfaces")) {
            while (data.get(listKey) instanceof List<?> items && items.size() > 1
                    && binarySize(frame) > maxFrameBytes) {
                ((List<?>) data.get(listKey)).remove(items.size() - 1);
                frame.put("truncated", true);
            }
        }
        if (binarySize(frame) > maxFrameBytes) {
            throw new IllegalArgumentException("Reply does not fit the maximum frame size");
        }
        return frame;
    }

    @SuppressWarnings("unchecked")
    private static List<Object> valuesOf(Map<String, Object> frame) {
        return (List<Object>) frame.get("values");
    }
}
