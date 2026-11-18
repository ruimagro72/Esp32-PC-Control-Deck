package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.telemetry.HostMetrics;
import dev.controldeck.host.telemetry.MetricCatalog;
import dev.controldeck.host.telemetry.MetricCatalog.Entry;
import dev.controldeck.host.telemetry.MetricCatalog.Kind;
import dev.controldeck.host.telemetry.MetricCatalog.Sample;
import dev.controldeck.host.telemetry.NetworkSource;
import dev.controldeck.host.telemetry.SystemSource;
import dev.controldeck.host.util.Json;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The metrics catalog (docs/03-SERVER.md): what enters it, its version, its pages. */
final class CatalogTests {

    private CatalogTests() {
    }

    static void run(Runner r) {
        r.test("catalog: a metric enters with its first value, stays, and changes the version", () -> {
            MetricCatalog catalog = new MetricCatalog();
            Entry cpu = new Entry("cpu0.use", "CPU", "CPU usage", "USE", "%", Kind.PERCENT, 100.0, 1);
            Entry gpu = new Entry("gpu0.use", "GPU", "GPU usage", "USE", "%", Kind.PERCENT, 100.0, 1);
            String empty = catalog.version();
            equal(List.of(12.5), catalog.update(List.of(new Sample(cpu, 12.5), new Sample(gpu, null))), "no GPU value: not in it");
            String one = catalog.version();
            check(!one.equals(empty), "a new entry, a new version");
            equal(List.of(10.0, 50.0), catalog.update(List.of(new Sample(cpu, 10.0), new Sample(gpu, 50.0))), "values in catalog order");
            String two = catalog.version();
            check(!two.equals(one), "the GPU came");
            List<Double> gone = catalog.update(List.of(new Sample(cpu, 11.0), new Sample(gpu, null)));
            equal(2, gone.size(), "a missing value keeps its place");
            equal(null, gone.get(1), "as null");
            equal(two, catalog.version(), "and the version");
        });
        r.test("catalog: pages fit the frame; every entry is sent once across them", () -> {
            MetricCatalog catalog = new MetricCatalog();
            List<Sample> samples = new ArrayList<>();
            for (int i = 0; i < 40; i++) {
                samples.add(new Sample(new Entry("net:eth" + i + ".down", "eth" + i, "Download", "DOWN", "Mbit/s",
                        Kind.RATE, null, 1), 1.0));
            }
            catalog.update(samples);
            int from = 0;
            int pages = 0;
            while (from < 40) {
                Map<String, Object> frame = catalog.frame(from, 600);
                int size = dev.controldeck.host.protocol.Codec4.encode(frame).payload().length;
                check(size <= 600, "page fits: " + size);
                equal(40, ((Number) frame.get("total")).intValue(), "total");
                List<?> rows = (List<?>) frame.get("m");
                check(!rows.isEmpty(), "progress");
                from += rows.size();
                pages++;
            }
            check(pages > 1, "more than one page");
        });
        r.test("catalog: the system plugin lists only what has values; the network plugin each card, not bridges", () -> {
            Map<String, Object> frame = new HashMap<>();
            frame.put("cpu", 20.0);
            frame.put("ram", 40.0);
            frame.put("gpu", null);
            List<String> ids = new ArrayList<>();
            for (Sample s : new SystemSource().collect(null, null, frame)) {
                if (s.value() != null) {
                    ids.add(s.entry().id());
                }
            }
            equal(List.of("cpu0.use", "ram0.use"), ids, "no GPU on this machine: nothing of it");

            Map<String, HostMetrics.Rx> before = new LinkedHashMap<>();
            before.put("eno1", new HostMetrics.Rx(0, 0));
            before.put("docker0", new HostMetrics.Rx(0, 0));
            before.put("lo", new HostMetrics.Rx(0, 0));
            Map<String, HostMetrics.Rx> after = new LinkedHashMap<>();
            after.put("eno1", new HostMetrics.Rx(1_000_000, 500_000));
            after.put("docker0", new HostMetrics.Rx(5, 5));
            after.put("lo", new HostMetrics.Rx(5, 5));
            HostMetrics.Reading r0 = reading(0.0, before);
            HostMetrics.Reading r1 = reading(1.0, after);
            List<Sample> net = new NetworkSource(name -> name.equals("eno1")).collect(r0, r1, Map.of());
            equal(2, net.size(), "one card, two rates");
            equal("net:eno1.down", net.get(0).entry().id(), "id");
            equal(8.0, net.get(0).value(), "1 MB in 1 s = 8 Mbit/s");
            equal(4.0, net.get(1).value(), "upload");
        });
    }

    private static HostMetrics.Reading reading(double time, Map<String, HostMetrics.Rx> counters) {
        return new HostMetrics.Reading(time, true, null, null, new HostMetrics.Net("eno1", counters), null, null, null, 0, "00:00");
    }
}
