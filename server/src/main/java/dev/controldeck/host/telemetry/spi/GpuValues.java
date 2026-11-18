package dev.controldeck.host.telemetry.spi;

/**
 * The values a {@link GpuPlugin} fills for one sample. Null means "this driver does not tell";
 * the host then uses its common sources (the DRM clients' fdinfo) or a configured value.
 */
public final class GpuValues {
    /** Busy share of the GPU, 0-100. */
    public Double busyPercent;
    /** Video memory in use and in total, in bytes. */
    public Double vramUsedBytes;
    public Double vramTotalBytes;
    /** Clock now and the clock the driver asks for, in MHz. */
    public Double clockMhz;
    public Double requestedMhz;
    /** Idle time counter (RC6 or equivalent), in milliseconds since boot. */
    public Double idleResidencyMs;
}
