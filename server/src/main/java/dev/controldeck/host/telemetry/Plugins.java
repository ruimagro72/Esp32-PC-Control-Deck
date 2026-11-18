package dev.controldeck.host.telemetry;

import dev.controldeck.host.telemetry.spi.GpuPlugin;

import java.io.IOException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.ServiceLoader;
import java.util.logging.Logger;
import java.util.stream.Stream;

/**
 * Loads the host's add-ons (docs/03-SERVER.md): jars in the {@code plugins/} folder next to the
 * host's jar, or the folder named by {@code -Dcontroldeck.plugins=...}, plus any on the class
 * path. Loaded once; a broken jar is skipped and logged, never fatal.
 */
public final class Plugins {
    private static final Logger LOG = Logger.getLogger(Plugins.class.getName());
    private static volatile List<GpuPlugin> gpu;

    private Plugins() {
    }

    /** The GPU vendor plugins found (empty when none is installed). */
    public static List<GpuPlugin> gpu() {
        List<GpuPlugin> found = gpu;
        if (found == null) {
            synchronized (Plugins.class) {
                if (gpu == null) gpu = load(GpuPlugin.class);
                found = gpu;
            }
        }
        return found;
    }

    private static <T> List<T> load(Class<T> type) {
        List<T> out = new ArrayList<>();
        List<String> names = new ArrayList<>();
        ClassLoader loader = jarLoader();
        try {
            for (T plugin : ServiceLoader.load(type, loader)) {
                String name = plugin.getClass().getName();
                if (names.contains(name)) continue;   // on the class path and in the folder
                names.add(name);
                out.add(plugin);
            }
        } catch (java.util.ServiceConfigurationError e) {
            LOG.warning("plugins: " + e.getMessage());
        }
        if (!out.isEmpty()) LOG.info("plugins: " + names);
        return List.copyOf(out);
    }

    /** A class loader over the folder's jars, parented on the host's own (which sees the SPI). */
    private static ClassLoader jarLoader() {
        ClassLoader parent = Plugins.class.getClassLoader();
        Path folder = folder();
        if (folder == null || !Files.isDirectory(folder)) return parent;
        List<URL> jars = new ArrayList<>();
        try (Stream<Path> entries = Files.list(folder)) {
            for (Path jar : entries.filter(p -> p.toString().endsWith(".jar")).sorted().toList()) {
                jars.add(jar.toUri().toURL());
            }
        } catch (IOException e) {
            LOG.warning("plugins: cannot read " + folder + ": " + e.getMessage());
        }
        return jars.isEmpty() ? parent : new URLClassLoader(jars.toArray(new URL[0]), parent);
    }

    private static Path folder() {
        String configured = System.getProperty("controldeck.plugins");
        if (configured != null && !configured.isBlank()) return Path.of(configured);
        try {
            Path self = Path.of(Plugins.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            return (Files.isDirectory(self) ? self : self.getParent()).resolve("plugins");
        } catch (Exception e) {
            return null;
        }
    }
}
