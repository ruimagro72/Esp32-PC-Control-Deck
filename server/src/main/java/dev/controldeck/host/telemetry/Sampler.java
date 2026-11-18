package dev.controldeck.host.telemetry;

import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The single source of telemetry for every consumer (HTTP, USB, Wi-Fi, BLE).
 *
 * Rates are differences between two counter readings. The previous reading is
 * persisted in var/ (sample_state.json), and the latest frame in var/latest.json,
 * so a restart does not lose the baseline. A fresh sample is appended to the history.
 * A caller within {@code maxAge} seconds of the previous sample gets that sample
 * back instead of forcing another one, so polling clients cannot distort rates.
 *
 * The host is one process, so the sampler serializes calls with a monitor. Do not run
 * two hosts against the same var/ at the same time.
 */
public final class Sampler {

    private static final double STATE_STALE_AFTER = 30.0;
    private static final long BASELINE_DELAY_MS = 150;

    /** Result of one sample: the stats frame, its time and whether it was taken now. */
    public record Result(Map<String, Object> frame, double time, boolean fresh) {
    }

    private final HostMetrics metrics;
    private final MetricCatalog catalog = new MetricCatalog();
    private final List<TelemetrySource> sources;
    private final HistoryStore history;
    private final Path statePath;
    private final Path latestPath;

    public Sampler(HostMetrics metrics, HistoryStore history, Path varDir) {
        this.metrics = metrics;
        this.sources = List.of(new SystemSource(), new NetworkSource(metrics::physicalInterface), new GpuSource());
        this.history = history;
        this.statePath = varDir.resolve("sample_state.json");
        this.latestPath = varDir.resolve("latest.json");
    }

    public HistoryStore history() {
        return history;
    }

    public HostMetrics metrics() {
        return metrics;
    }

    public MetricCatalog catalog() {
        return catalog;
    }

    public synchronized Result sample(double maxAge) {
        double now = HostMetrics.nowSeconds();

        Map<String, Object> cached = readMap(latestPath);
        if (cached != null && cached.get("time") instanceof Number latestTime && cached.get("frame") instanceof Map<?, ?>) {
            double time = latestTime.doubleValue();
            if (now >= time && now - time < maxAge) {
                @SuppressWarnings("unchecked")
                Map<String, Object> frame = (Map<String, Object>) cached.get("frame");
                return new Result(frame, time, false);
            }
        }

        HostMetrics.Reading counters = metrics.read();
        HostMetrics.Reading previous = readReading(statePath);
        if (previous == null || counters.time() - previous.time() > STATE_STALE_AFTER || counters.time() < previous.time()) {
            // No usable baseline: take one, wait briefly, then read again so rates can be computed.
            previous = counters;
            sleepQuietly(BASELINE_DELAY_MS);
            counters = metrics.read();
        }

        Map<String, Object> frame = metrics.compute(previous, counters);
        // The catalog's values (docs/03-SERVER.md): the version and the values in its order.
        List<MetricCatalog.Sample> samples = new ArrayList<>();
        for (TelemetrySource source : sources) {
            samples.addAll(source.collect(previous, counters, frame));
        }
        List<Double> values = catalog.update(samples);
        frame.put("cv", catalog.version());
        frame.put("v", values);
        writeJson(statePath, Json.encode(counters));
        writeJson(latestPath, Json.encode(Map.of("time", counters.time(), "frame", frame)));
        history.append(counters.time(), frame);

        return new Result(frame, counters.time(), true);
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static HostMetrics.Reading readReading(Path path) {
        String content = readText(path);
        return content == null ? null : Json.decode(content, HostMetrics.Reading.class);
    }

    private static Map<String, Object> readMap(Path path) {
        String content = readText(path);
        return content == null ? null : Json.parseObject(content);
    }

    private static String readText(Path path) {
        try {
            return Files.readString(path);
        } catch (IOException | SecurityException e) {
            return null;
        }
    }

    private static void writeJson(Path path, String json) {
        try {
            Json.writeAtomic(path, json);
        } catch (IOException e) {
            // State is an optimisation for rates; a failed write only delays the next baseline.
        }
    }
}
