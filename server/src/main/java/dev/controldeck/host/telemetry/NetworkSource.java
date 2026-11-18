package dev.controldeck.host.telemetry;

import dev.controldeck.host.telemetry.MetricCatalog.Entry;
import dev.controldeck.host.telemetry.MetricCatalog.Kind;
import dev.controldeck.host.telemetry.MetricCatalog.Sample;
import dev.controldeck.host.util.Values;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.function.Predicate;

/**
 * Every network card of the machine, each an instance of its own ("net:eno1", "net:wlp4s0"):
 * download and upload rates from the /proc/net/dev counters of two readings. Bridges, VPNs and
 * container links are left out (they have no device behind them).
 */
public final class NetworkSource implements TelemetrySource {

    private static final int REFRESH_SECONDS = 1;
    private final Predicate<String> physical;

    public NetworkSource(Predicate<String> physical) {
        this.physical = physical;
    }

    @Override
    public List<Sample> collect(HostMetrics.Reading previous, HostMetrics.Reading current, Map<String, Object> frame) {
        List<Sample> samples = new ArrayList<>();
        if (current == null || current.net() == null || current.net().counters() == null) {
            return samples;
        }
        double elapsed = previous == null ? 0.0 : current.time() - previous.time();
        Map<String, HostMetrics.Rx> before = previous == null || previous.net() == null ? null : previous.net().counters();
        for (Map.Entry<String, HostMetrics.Rx> card : new TreeMap<>(current.net().counters()).entrySet()) {
            String name = card.getKey();
            if (!physical.test(name)) {
                continue;
            }
            Double down = null;
            Double up = null;
            HostMetrics.Rx last = before == null ? null : before.get(name);
            if (last != null && elapsed > 0.05) {
                long rx = card.getValue().rx() - last.rx();
                long tx = card.getValue().tx() - last.tx();
                if (rx >= 0 && tx >= 0) {
                    down = Values.round(rx * 8.0 / elapsed / 1_000_000.0, 6);
                    up = Values.round(tx * 8.0 / elapsed / 1_000_000.0, 6);
                }
            }
            String group = name.length() > 10 ? name.substring(0, 10) : name;
            samples.add(new Sample(new Entry("net:" + name + ".down", group, "Download", "DOWN", "Mbit/s", Kind.RATE,
                    null, REFRESH_SECONDS), down));
            samples.add(new Sample(new Entry("net:" + name + ".up", group, "Upload", "UP", "Mbit/s", Kind.RATE,
                    null, REFRESH_SECONDS), up));
        }
        return samples;
    }
}
