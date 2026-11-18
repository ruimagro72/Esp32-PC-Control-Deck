package dev.controldeck.host.telemetry.spi;

import java.util.List;
import java.util.Optional;

/**
 * Read-only view of one GPU for a {@link GpuPlugin}. Paths are absolute Linux paths
 * ("/sys/class/drm/card0/device/..."); the host maps them onto a fixture tree in its tests,
 * so plugins never open files themselves.
 */
public interface GpuContext {

    /** "/sys/class/drm/card0". */
    String card();

    /** "/sys/class/drm/card0/device" (the PCI device). */
    String device();

    /** The card's own DRM folder under the device ("/sys/class/drm/card0/device/drm/card0"). */
    String drm();

    /** The kernel driver bound to the device ("i915", "xe", "amdgpu"), or null. */
    String driver();

    /** "0000:03:00.0", or null. */
    String pciAddress();

    /** A file holding one non-negative number. */
    Optional<Double> number(String path);

    /** A file's text, untrimmed. */
    Optional<String> text(String path);

    /** A file's lines (empty when unreadable). */
    List<String> lines(String path);

    /** Paths matching a glob in the last parts ("/sys/.../gt/gt*&#47;rc6_residency_ms"). */
    List<String> glob(String pattern);

    /** The names inside a directory (empty when it is missing or unreadable). */
    List<String> list(String directory);
}
