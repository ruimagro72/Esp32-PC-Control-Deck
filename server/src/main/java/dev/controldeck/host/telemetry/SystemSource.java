package dev.controldeck.host.telemetry;

import dev.controldeck.host.telemetry.MetricCatalog.Entry;
import dev.controldeck.host.telemetry.MetricCatalog.Kind;
import dev.controldeck.host.telemetry.MetricCatalog.Sample;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The first telemetry plugin: the Linux reader the host has always had (/proc and /sys), one
 * CPU, the RAM, the first GPU and the disk the host is set to, read from the protocol-3 frame.
 * A part whose values never come (no GPU, no sensor) never enters the catalog.
 */
public final class SystemSource implements TelemetrySource {

    private record Field(String key, Entry entry) {
    }

    private static final double TEMP_MAX = 110.0;
    private static final List<Field> FIELDS = List.of(
            field("cpu", "cpu0.use", "CPU", "CPU usage", "USE", "%", Kind.PERCENT, 100.0, 1),
            field("cpu_temp", "cpu0.temp", "CPU", "CPU temperature", "TEMP", "C", Kind.TEMPERATURE, TEMP_MAX, 5),
            field("system_temp", "board.temp", "CPU", "System temperature", "SYS", "C", Kind.TEMPERATURE, TEMP_MAX, 5),
            field("ram", "ram0.use", "RAM", "RAM usage", "USE", "%", Kind.PERCENT, 100.0, 2),
            field("ram_used_gb", "ram0.used", "RAM", "RAM used", "USED", "GB", Kind.SIZE, null, 5),
            field("gpu", "gpu0.use", "GPU", "GPU usage", "USE", "%", Kind.PERCENT, 100.0, 1),
            field("vram", "gpu0.vram", "GPU", "VRAM usage", "VRAM", "%", Kind.PERCENT, 100.0, 2),
            field("gpu_temp", "gpu0.temp", "GPU", "GPU temperature", "TEMP", "C", Kind.TEMPERATURE, TEMP_MAX, 5),
            field("gpu_clock_mhz", "gpu0.clock", "GPU", "GPU clock", "CLK", "MHz", Kind.FREQUENCY, null, 2),
            field("gpu_power_watts", "gpu0.power", "GPU", "GPU power", "PWR", "W", Kind.POWER, null, 2),
            field("disk_used", "disk0.used", "DISK", "Disk used", "USED", "%", Kind.PERCENT, 100.0, 60),
            field("disk_read_mbs", "disk0.read", "DISK", "Disk read", "RD", "MB/s", Kind.RATE, null, 1),
            field("disk_write_mbs", "disk0.write", "DISK", "Disk write", "WR", "MB/s", Kind.RATE, null, 1),
            field("disk_temp", "disk0.temp", "DISK", "Disk temperature", "TEMP", "C", Kind.TEMPERATURE, TEMP_MAX, 10));

    private static Field field(String key, String id, String group, String name, String shortName, String unit,
                               Kind kind, Double max, int refreshSeconds) {
        return new Field(key, new Entry(id, group, name, shortName, unit, kind, max, refreshSeconds));
    }

    @Override
    public List<Sample> collect(HostMetrics.Reading previous, HostMetrics.Reading current, Map<String, Object> frame) {
        List<Sample> samples = new ArrayList<>(FIELDS.size());
        for (Field field : FIELDS) {
            Object value = frame.get(field.key());
            samples.add(new Sample(field.entry(), value instanceof Number number ? number.doubleValue() : null));
        }
        return samples;
    }
}
