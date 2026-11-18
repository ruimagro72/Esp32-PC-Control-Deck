package dev.controldeck.host.telemetry.spi;

/**
 * A GPU vendor plugin (docs/03-SERVER.md): the vendor-specific readings of one kind of GPU driver,
 * kept out of the host. The host finds the card and reads what is common to every GPU on Linux
 * (hwmon sensors, the DRM clients' fdinfo); a plugin adds what only its driver offers.
 *
 * <p>Plugins are jars in the host's {@code plugins/} folder, found with {@link java.util.ServiceLoader}
 * through {@code META-INF/services/dev.controldeck.host.telemetry.spi.GpuPlugin}. A machine only
 * needs the plugin of its own GPU; with none, the host still reports the common values.
 */
public interface GpuPlugin {

    /** Short name for the log ("gpu-intel"). */
    String name();

    /** True for a kernel driver this plugin knows ("i915", "xe", "amdgpu"). */
    boolean supports(String driver);

    /**
     * Reads the card into {@code values}: set only what this driver tells; leave the rest null.
     * Called on every sample, so it must be quick and must not throw (return early instead).
     */
    void read(GpuContext card, GpuValues values);

    /**
     * How strongly this card should be the one shown when a machine has several (higher wins):
     * for example a discrete card over an integrated one. 0 when the plugin has no opinion.
     */
    default int preference(GpuContext card) {
        return 0;
    }
}
