package dev.controldeck.host.telemetry;

import dev.controldeck.host.telemetry.MetricCatalog.Entry;
import dev.controldeck.host.telemetry.MetricCatalog.Kind;
import dev.controldeck.host.telemetry.MetricCatalog.Sample;
import dev.controldeck.host.util.Values;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The GPU's details beyond the system plugin's use, clock, temperature and power
 * (docs/03-SERVER.md): the busy share of each engine class, the requested clock, the time
 * in RC6 (idle), the fan, the VRAM in use and the interrupts per second. All from files an
 * ordinary user can read (sysfs, hwmon, /proc/interrupts, the DRM clients' fdinfo), for the same GPU as {@link SystemSource} ("gpu0"). A value
 * the driver does not give never enters the catalog.
 */
public final class GpuSource implements TelemetrySource {

    private record Engine(String driverClass, Entry entry) {
    }

    private static final String GROUP = "GPU";
    private static final List<Engine> ENGINES = List.of(
            engine("render", "gpu0.render", "Render engine", "3D"),
            engine("video", "gpu0.video", "Video engine", "VID"),
            engine("video-enhance", "gpu0.video_enh", "Video enhance", "VE"),
            engine("copy", "gpu0.copy", "Copy engine", "COPY"),
            engine("compute", "gpu0.compute", "Compute engine", "CMP"));
    private static final Entry REQUESTED = new Entry("gpu0.clock_req", GROUP, "GPU clock requested", "REQ", "MHz",
            Kind.FREQUENCY, null, 2);
    private static final Entry RC6 = new Entry("gpu0.rc6", GROUP, "GPU idle (RC6)", "RC6", "%", Kind.PERCENT, 100.0, 2);
    // A rotation frequency: drawn as a number with its unit, like a clock (protocol 4 has no other kind for it).
    private static final Entry FAN = new Entry("gpu0.fan", GROUP, "GPU fan", "FAN", "RPM", Kind.FREQUENCY, null, 5);
    private static final Entry VRAM_USED = new Entry("gpu0.vram_used", GROUP, "VRAM used", "USED", "GB", Kind.SIZE, null, 2);
    private static final Entry IRQ = new Entry("gpu0.irq", GROUP, "GPU interrupts", "IRQ", "/s", Kind.RATE, null, 2);

    private static Engine engine(String driverClass, String id, String name, String shortName) {
        return new Engine(driverClass, new Entry(id, GROUP, name, shortName, "%", Kind.PERCENT, 100.0, 1));
    }

    @Override
    public List<Sample> collect(HostMetrics.Reading previous, HostMetrics.Reading current, Map<String, Object> frame) {
        List<Sample> samples = new ArrayList<>();
        HostMetrics.Gpu gpu = current == null ? null : current.gpu();
        if (gpu == null) {
            return samples;
        }
        Map<String, Double> engines = gpu.engines();
        for (Engine engine : ENGINES) {
            samples.add(new Sample(engine.entry(), engines == null ? null : engines.get(engine.driverClass())));
        }
        samples.add(new Sample(REQUESTED, gpu.requestedMhz() == null ? null : gpu.requestedMhz().doubleValue()));
        samples.add(new Sample(RC6, rc6Percent(previous, current)));
        samples.add(new Sample(FAN, gpu.fanRpm() == null ? null : gpu.fanRpm().doubleValue()));
        samples.add(new Sample(VRAM_USED, gpu.vramUsedGb()));
        samples.add(new Sample(IRQ, interruptRate(previous, current)));
        return samples;
    }

    /** The GPU's interrupts per second between the readings, or null. */
    static Double interruptRate(HostMetrics.Reading previous, HostMetrics.Reading current) {
        if (previous == null || current == null || previous.gpu() == null || current.gpu() == null) {
            return null;
        }
        Long before = previous.gpu().interrupts();
        Long now = current.gpu().interrupts();
        double elapsed = current.time() - previous.time();
        if (before == null || now == null || elapsed < 0.05 || now < before) {
            return null;
        }
        return Values.round((now - before) / elapsed, 1);
    }

    /** The share of the time between the readings the GT spent in RC6 (idle), or null. */
    static Double rc6Percent(HostMetrics.Reading previous, HostMetrics.Reading current) {
        if (previous == null || current == null || previous.gpu() == null || current.gpu() == null) {
            return null;
        }
        Double before = previous.gpu().idleResidencyMs();
        Double now = current.gpu().idleResidencyMs();
        double elapsedMs = (current.time() - previous.time()) * 1000.0;
        if (before == null || now == null || elapsedMs < 50.0 || now < before) {
            return null;  // no counter, too short an interval, or the counter was reset (suspend)
        }
        return Values.round(Math.min(100.0, 100.0 * (now - before) / elapsedMs), 1);
    }
}
