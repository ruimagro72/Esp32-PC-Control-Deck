package dev.controldeck.host.selftest;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.close;
import static dev.controldeck.host.selftest.Runner.equal;

import dev.controldeck.host.telemetry.GpuSource;
import dev.controldeck.host.telemetry.HostMetrics;
import dev.controldeck.host.telemetry.MetricCatalog.Sample;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

/**
 * The Intel GPU readings without privileges (docs/03-SERVER.md), from fake sysfs and
 * /proc trees in a temporary folder: i915 (Arc A770) and xe, the engines from the DRM
 * clients' fdinfo, the clocks, RC6, the fan, and the GPU plugin's catalog entries.
 */
final class GpuTests {

    private GpuTests() {
    }

    private static final String PCI = "0000:03:00.0";
    private static final long SECOND = 1_000_000_000L;

    static void run(Runner r) {
        r.test("gpu: i915 Arc A770, engines per class from fdinfo (capacity counted), clocks, fan, no temperature", () -> {
            Path root = card("cd-gpu-i915", "i915");
            Path card = root.resolve("sys/class/drm/card0");
            write(card, "gt_act_freq_mhz", "600\n");
            write(card, "gt_cur_freq_mhz", "650\n");
            write(card, "gt/gt0/rc6_residency_ms", "1000\n");
            write(card, "gt/gt1/rc6_residency_ms", "3000\n");  // a second GT (render + media): the mean
            write(root, "proc/interrupts", "           CPU0       CPU1\n"
                    + "  0:         20          0  IR-IO-APIC    2-edge      timer\n"
                    + "152:   12993588       1545  IR-PCI-MSI-" + PCI + "    0-edge      i915\n"
                    + "153:        100          0  IR-PCI-MSI-0000:00:02.0    0-edge      i915\n");
            Path hwmon = card.resolve("device/hwmon/hwmon2");
            write(hwmon, "name", "i915\n");
            write(hwmon, "energy1_input", "5000000\n");
            write(hwmon, "fan1_input", "1200\n");
            client(root, 100, 5, i915Fdinfo(0, 0, 0, 1_048_576));
            client(root, 100, 6, i915Fdinfo(0, 0, 0, 1_048_576));  // the same client again: counted once

            AtomicLong clock = new AtomicLong(SECOND);
            HostMetrics metrics = new HostMetrics(root.toString(), "auto", null, clock::get);
            HostMetrics.Gpu first = metrics.read().gpu();
            check(first != null, "the card is found");
            equal(null, first.engines(), "no engine figures before a second reading");
            equal(null, first.usagePercent(), "no utilization before a second reading");

            // One second later: render busy 0.5 s, video 1 s over its two engines, copy idle.
            client(root, 100, 5, i915Fdinfo(500_000_000L, 1_000_000_000L, 0, 1_048_576));
            client(root, 100, 6, i915Fdinfo(500_000_000L, 1_000_000_000L, 0, 1_048_576));
            clock.addAndGet(SECOND);
            HostMetrics.Gpu gpu = metrics.read().gpu();
            equal("i915", gpu.driver(), "driver");
            equal(50.0, gpu.engines().get("render"), "render: 0.5 s of 1 s");
            equal(50.0, gpu.engines().get("video"), "video: 1 s over two engines in 1 s");
            equal(0.0, gpu.engines().get("copy"), "copy idle");
            equal(0.0, gpu.engines().get("video-enhance"), "video enhance idle");
            equal(0.0, gpu.engines().get("compute"), "compute idle");
            equal(50.0, gpu.usagePercent(), "utilization: the busiest class");
            equal(1.0, gpu.vramUsedGb(), "VRAM used: the client's resident local memory, once");
            equal(null, gpu.vramTotalGb(), "VRAM size: not in sysfs, none configured");
            equal(600, gpu.clockMhz(), "actual clock");
            equal(650, gpu.requestedMhz(), "requested clock");
            equal(2000.0, gpu.idleResidencyMs(), "RC6 residency counter: the mean of the GTs");
            equal(12_995_133L, gpu.interrupts(), "interrupts of this card only, all CPUs");
            equal(1200, gpu.fanRpm(), "fan");
            equal(null, gpu.tempCelsius(), "i915 on an Arc gives no temperature");
            equal(5_000_000.0, gpu.energyUj(), "energy counter");
        });
        r.test("gpu: a client that opens the GPU between two readings does not count its past", () -> {
            Path root = card("cd-gpu-newclient", "i915");
            client(root, 100, 5, i915Fdinfo(0, 0, 0, 0));
            AtomicLong clock = new AtomicLong(SECOND);
            HostMetrics metrics = new HostMetrics(root.toString(), "auto", null, clock::get);
            metrics.read();
            client(root, 100, 5, i915Fdinfo(100_000_000L, 0, 0, 0));
            client(root, 200, 3, i915Fdinfo(900_000_000_000L, 0, 0, 0).replace("drm-client-id:\t7", "drm-client-id:\t8"));
            clock.addAndGet(20 * SECOND);  // past the rescan: the new process is found
            HostMetrics.Gpu gpu = metrics.read().gpu();
            equal(0.5, gpu.engines().get("render"), "only the known client: 0.1 s of 20 s");
        });
        r.test("gpu: xe, busy cycles against total cycles, tile0 clocks and idle, package temperature", () -> {
            Path root = card("cd-gpu-xe", "xe");
            Path device = root.resolve("sys/class/drm/card0/device");
            write(device, "tile0/gt0/freq0/act_freq", "1800\n");
            write(device, "tile0/gt0/freq0/cur_freq", "2000\n");
            write(device, "tile0/gt0/gtidle/idle_residency_ms", "777\n");
            Path hwmon = device.resolve("hwmon/hwmon3");
            write(hwmon, "name", "xe\n");
            write(hwmon, "temp2_input", "45000\n");
            write(hwmon, "energy2_input", "123\n");
            client(root, 100, 5, xeFdinfo(0, 0, 0, 0, 2048));
            AtomicLong clock = new AtomicLong(SECOND);
            HostMetrics metrics = new HostMetrics(root.toString(), "auto", null, clock::get);
            metrics.read();
            client(root, 100, 5, xeFdinfo(250, 1000, 1000, 1000, 2048));
            clock.addAndGet(SECOND);
            HostMetrics.Gpu gpu = metrics.read().gpu();
            equal("xe", gpu.driver(), "driver");
            equal(25.0, gpu.engines().get("render"), "rcs: 250 of 1000 cycles");
            equal(50.0, gpu.engines().get("video"), "vcs: 1000 cycles of 2 x 1000");
            equal(1800, gpu.clockMhz(), "actual clock");
            equal(2000, gpu.requestedMhz(), "requested clock");
            equal(777.0, gpu.idleResidencyMs(), "idle residency counter");
            equal(45.0, gpu.tempCelsius(), "package temperature");
            equal(123.0, gpu.energyUj(), "package energy");
            close(2048.0 / 1048576.0, gpu.vramUsedGb(), 0.01, "VRAM used from drm-resident-vram0");
        });
        r.test("gpu: the GPU plugin lists engines, requested clock, RC6, fan and VRAM used; RC6 from two readings", () -> {
            Map<String, Double> engines = new LinkedHashMap<>();
            engines.put("render", 12.5);
            engines.put("video", 0.0);
            HostMetrics.Reading before = reading(10.0, gpu(1000.0, engines, 500L));
            HostMetrics.Reading after = reading(11.0, gpu(1600.0, engines, 1700L));
            Map<String, Double> values = new LinkedHashMap<>();
            for (Sample s : new GpuSource().collect(before, after, Map.of())) {
                if (s.value() != null) {
                    values.put(s.entry().id(), s.value());
                }
                equal("GPU", s.entry().group(), "group of " + s.entry().id());
                check(s.entry().id().length() < 28 && s.entry().name().length() < 24 && s.entry().shortName().length() < 6,
                        "fits the Deck's catalog entry: " + s.entry().id());
            }
            equal(List.of("gpu0.render", "gpu0.video", "gpu0.clock_req", "gpu0.rc6", "gpu0.fan", "gpu0.vram_used",
                    "gpu0.irq"), new ArrayList<>(values.keySet()), "only what the driver gave");
            equal(1200.0, values.get("gpu0.irq"), "1200 interrupts in 1 s");
            equal(12.5, values.get("gpu0.render"), "render");
            equal(60.0, values.get("gpu0.rc6"), "0.6 s of 1 s in RC6");
            equal(650.0, values.get("gpu0.clock_req"), "requested clock");
            equal(0.0, values.get("gpu0.fan"), "a stopped fan is 0 RPM, a value");

            HostMetrics.Reading reset = reading(12.0, gpu(10.0, engines, 1800L));
            Double rc6 = null;
            for (Sample s : new GpuSource().collect(after, reset, Map.of())) {
                if (s.entry().id().equals("gpu0.rc6")) {
                    rc6 = s.value();
                }
            }
            equal(null, rc6, "a counter that went back (suspend) gives null, not a negative share");
            equal(0, new GpuSource().collect(null, reading(1.0, null), Map.of()).size(), "no GPU: nothing");
        });
    }

    // -- fixtures ------------------------------------------------------------------

    /** A card0 whose device is the PCI device 0000:03:00.0, bound to {@code driver}. */
    private static Path card(String prefix, String driver) throws IOException {
        Path root = Files.createTempDirectory(prefix);
        Path pci = root.resolve("sys/devices/pci0000:00/" + PCI);
        write(pci, "vendor", "0x8086\n");
        write(pci, "device", "0x56a0\n");
        Files.createSymbolicLink(pci.resolve("driver"), Path.of("../../../bus/pci/drivers/" + driver));
        Path card = root.resolve("sys/class/drm/card0");
        Files.createDirectories(card);
        Files.createSymbolicLink(card.resolve("device"), pci);
        Files.createDirectories(root.resolve("proc"));
        return root;
    }

    private static void client(Path root, int pid, int fd, String fdinfo) throws IOException {
        Path link = root.resolve("proc/" + pid + "/fd/" + fd);
        if (!Files.isSymbolicLink(link)) {
            Files.createDirectories(link.getParent());
            Files.createSymbolicLink(link, Path.of("/dev/dri/renderD128"));
        }
        write(root.resolve("proc/" + pid + "/fdinfo"), String.valueOf(fd), fdinfo);
    }

    /** An i915 client as kernel 6.12 writes it (times in ns, two video and two video-enhance engines). */
    private static String i915Fdinfo(long renderNs, long videoNs, long copyNs, long localKib) {
        return "pos:\t0\nflags:\t02100002\ndrm-driver:\ti915\ndrm-client-id:\t7\ndrm-pdev:\t" + PCI + "\n"
                + "drm-total-local0:\t" + localKib + " KiB\n"
                + "drm-resident-local0:\t" + localKib + " KiB\n"
                + "drm-engine-render:\t" + renderNs + " ns\n"
                + "drm-engine-copy:\t" + copyNs + " ns\n"
                + "drm-engine-video:\t" + videoNs + " ns\n"
                + "drm-engine-capacity-video:\t2\n"
                + "drm-engine-video-enhance:\t0 ns\n"
                + "drm-engine-capacity-video-enhance:\t2\n"
                + "drm-engine-compute:\t0 ns\n";
    }

    /** An xe client: busy cycles and total cycles per class, VRAM as vram0. */
    private static String xeFdinfo(long rcs, long vcs, long totalRcs, long totalVcs, long vramKib) {
        return "drm-driver:\txe\ndrm-client-id:\t3\ndrm-pdev:\t" + PCI + "\n"
                + "drm-total-vram0:\t" + vramKib + " KiB\n"
                + "drm-resident-vram0:\t" + vramKib + " KiB\n"
                + "drm-cycles-rcs:\t" + rcs + "\ndrm-total-cycles-rcs:\t" + totalRcs + "\n"
                + "drm-cycles-vcs:\t" + vcs + "\ndrm-total-cycles-vcs:\t" + totalVcs + "\n"
                + "drm-engine-capacity-vcs:\t2\n";
    }

    private static HostMetrics.Gpu gpu(Double idleMs, Map<String, Double> engines, Long interrupts) {
        return new HostMetrics.Gpu(12.5, 1.5, null, 600, null, null, null, "i915", 650, idleMs, 0, engines, interrupts);
    }

    private static HostMetrics.Reading reading(double time, HostMetrics.Gpu gpu) {
        return new HostMetrics.Reading(time, true, null, null, null, null, gpu, null, 0, "00:00");
    }

    private static void write(Path directory, String relative, String content) throws IOException {
        Path file = directory.resolve(relative);
        Files.createDirectories(file.getParent());
        Files.writeString(file, content, StandardCharsets.UTF_8);
    }
}
