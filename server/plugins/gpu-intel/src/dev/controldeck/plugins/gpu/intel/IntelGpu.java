package dev.controldeck.plugins.gpu.intel;

import dev.controldeck.host.telemetry.spi.GpuContext;
import dev.controldeck.host.telemetry.spi.GpuPlugin;
import dev.controldeck.host.telemetry.spi.GpuValues;

import java.util.List;
import java.util.Optional;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Intel GPUs on Linux (i915 and xe): what only these drivers tell, from files any user can read
 * (docs/03-SERVER.md). Moved out of the host on 13 Nov 2026 so a machine without an Intel GPU
 * carries none of it.
 */
public final class IntelGpu implements GpuPlugin {
    private static final Pattern LMEM_USED = Pattern.compile("drm-total-lmem:\\s+(\\d+)\\s+KiB", Pattern.CASE_INSENSITIVE);

    @Override
    public String name() {
        return "gpu-intel";
    }

    @Override
    public boolean supports(String driver) {
        return "i915".equals(driver) || "xe".equals(driver);
    }

    @Override
    public void read(GpuContext c, GpuValues v) {
        String card = c.card(), device = c.device(), drm = c.drm();
        v.vramUsedBytes = lmemUsedBytes(c, card, device, drm);
        // i915's module parameter lmem_size is a limit readable only by root, not the size; neither
        // driver puts the VRAM size in sysfs (kernel 6.12). lmem_size_bytes where it exists, else
        // the PCI window: with Resizable BAR an Intel dGPU maps all its memory there (A770: 16 GiB).
        v.vramTotalBytes = c.number(drm + "/gt/gt0/lmem_size_bytes").orElseGet(() -> barVram(c, device));
        // Frequencies: i915 in the card's folder (gt_act / gt_cur) or per GT (rps_*); xe per GT of
        // tile 0 (freq0/act_freq, cur_freq). "cur" is the frequency requested.
        String xeGt = device + "/tile0/gt0";
        v.clockMhz = first(c, card + "/gt_act_freq_mhz", card + "/gt/gt0/rps_act_freq_mhz", xeGt + "/freq0/act_freq");
        v.requestedMhz = first(c, card + "/gt_cur_freq_mhz", card + "/gt/gt0/rps_cur_freq_mhz", xeGt + "/freq0/cur_freq");
        // RC6 / idle: the average over every GT, as intel_gpu_top averages its per-GT PMU counters
        // (a GPU with a render and a media GT, such as Meteor Lake, has two).
        Double idle = average(c, c.glob(card + "/gt/gt*/rc6_residency_ms"));
        if (idle == null) idle = average(c, c.glob(device + "/tile*/gt*/gtidle/idle_residency_ms"));
        if (idle == null) idle = c.number(card + "/power/rc6_residency_ms").orElse(null);
        v.idleResidencyMs = idle;
    }

    private static Double first(GpuContext c, String... paths) {
        for (String p : paths) {
            Optional<Double> value = c.number(p);
            if (value.isPresent()) return value.get();
        }
        return null;
    }

    private static Double average(GpuContext c, List<String> files) {
        double sum = 0;
        int count = 0;
        for (String file : files) {
            Optional<Double> value = c.number(file);
            if (value.isPresent()) { sum += value.get(); count++; }
        }
        return count == 0 ? null : sum / count;
    }

    /** Intel discrete GPUs report local-memory use per client; sum it. */
    private static Double lmemUsedBytes(GpuContext c, String card, String device, String drm) {
        long usedKib = 0;
        for (String clients : List.of(card + "/clients", drm + "/clients", device + "/clients")) {
            for (String entry : c.list(clients)) {
                Matcher m = LMEM_USED.matcher(c.text(clients + "/" + entry + "/status").orElse(""));
                while (m.find()) usedKib += Long.parseLong(m.group(1));
            }
        }
        return usedKib > 0 ? (double) usedKib * 1024.0 : null;
    }

    /** The largest prefetchable memory BAR when it is 2 GiB or more (Resizable BAR on), else null. */
    private static Double barVram(GpuContext c, String device) {
        double best = 0;
        for (String line : c.lines(device + "/resource")) {
            String[] f = line.trim().split("\\s+");
            if (f.length < 3) continue;
            try {
                long start = Long.decode(f[0]), end = Long.decode(f[1]), flags = Long.decode(f[2]);
                if ((flags & 0x200) != 0 && (flags & 0x2000) != 0 && end > start) best = Math.max(best, (double) (end - start + 1));
            } catch (NumberFormatException e) {
                // a malformed line: skipped
            }
        }
        return best >= 2.0 * 1073741824.0 ? best : null;
    }
}
