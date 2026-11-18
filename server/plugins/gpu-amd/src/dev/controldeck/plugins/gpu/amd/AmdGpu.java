package dev.controldeck.plugins.gpu.amd;

import dev.controldeck.host.telemetry.spi.GpuContext;
import dev.controldeck.host.telemetry.spi.GpuPlugin;
import dev.controldeck.host.telemetry.spi.GpuValues;

/**
 * AMD GPUs on Linux (amdgpu): busy share and video memory straight from the driver's sysfs files
 * (gpu_busy_percent, mem_info_vram_used, mem_info_vram_total). Moved out of the host on 13 Nov 2026.
 */
public final class AmdGpu implements GpuPlugin {

    @Override
    public String name() {
        return "gpu-amd";
    }

    @Override
    public boolean supports(String driver) {
        return "amdgpu".equals(driver);
    }

    // A card with its own memory files is a discrete AMD card: prefer it over an integrated one.
    @Override
    public int preference(GpuContext c) {
        return c.number(c.device() + "/mem_info_vram_total").isPresent() ? 4 : 0;
    }

    @Override
    public void read(GpuContext c, GpuValues v) {
        String device = c.device();
        v.busyPercent = c.number(device + "/gpu_busy_percent").orElse(null);
        v.vramUsedBytes = c.number(device + "/mem_info_vram_used").orElse(null);
        v.vramTotalBytes = c.number(device + "/mem_info_vram_total").orElse(null);
    }
}
