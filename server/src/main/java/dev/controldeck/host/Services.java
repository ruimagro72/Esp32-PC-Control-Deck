package dev.controldeck.host;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.config.HostPaths;
import dev.controldeck.host.macro.MacroExecutor;
import dev.controldeck.host.macro.MacroService;
import dev.controldeck.host.firmware.FirmwareStore;
import dev.controldeck.host.pack.PackStore;
import dev.controldeck.host.inbox.MessageStore;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.security.PeerAuth;
import dev.controldeck.host.telemetry.HistoryStore;
import dev.controldeck.host.telemetry.HostMetrics;
import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.telemetry.Sampler;
import dev.controldeck.host.transport.TransportRegistry;
import dev.controldeck.host.util.Values;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Wires the services together once per process. The
 * HTTP API and every wrapper take their collaborators from here.
 *
 * @param systemRoot prefix for /proc and /sys; null or empty on a real host, a
 *                   fixture tree in the self-test
 * @param varDir     runtime directory (history, sampling state, transports, tokens)
 */
public final class Services {

    private final ConfigService config;
    private final String systemRoot;
    private final Path varDir;

    private Sampler sampler;
    private Inventory inventory;
    private MessageHandler messages;
    private MacroService macros;
    private TransportRegistry transports;
    private PeerAuth auth;

    public Services(ConfigService config, String systemRoot, Path varDir) {
        this.config = config;
        this.systemRoot = systemRoot == null ? "" : systemRoot;
        this.varDir = varDir;
    }

    /** Services for the real host: the database and var/ from the environment or the working directory. */
    public static Services forHost() {
        return new Services(new ConfigService(), "", HostPaths.varDir());
    }

    public ConfigService config() {
        return config;
    }

    public Path varDir() {
        return varDir;
    }

    public synchronized Sampler sampler() {
        if (sampler == null) {
            var telemetry = config.getTelemetrySettings();
            Object vram = telemetry.get("gpu_vram_total_gb");
            Double vramTotal = vram instanceof Number n ? n.doubleValue() : null;
            Long retention = Values.validateInt(telemetry.get("history_minutes"));
            String diskPath = String.valueOf(telemetry.get("disk_path"));

            HostMetrics metrics = new HostMetrics(systemRoot, diskPath, vramTotal);
            // The history shares the configuration's database (its own table).
            HistoryStore history = new HistoryStore(config.path(),
                    retention == null ? 30 : retention.intValue());
            sampler = new Sampler(metrics, history, varDir);
        }
        return sampler;
    }

    public synchronized Inventory inventory() {
        if (inventory == null) {
            var telemetry = config.getTelemetrySettings();
            Object vram = telemetry.get("gpu_vram_total_gb");
            Double vramTotal = vram instanceof Number n ? n.doubleValue() : null;
            inventory = new Inventory(systemRoot, String.valueOf(telemetry.get("disk_path")), vramTotal);
        }
        return inventory;
    }

    public synchronized MessageHandler messages() {
        if (messages == null) {
            messages = new MessageHandler(config, sampler(), inventory(), macros());
            messages.firmwareStore(new FirmwareStore(varDir.resolve("firmware")));
            // the packs (var/packs/<name>/); "default" falls back to the repository's firmware/sd-card/deck
            Path server = varDir.toAbsolutePath().normalize().getParent();
            Path repositoryPack = server == null ? null : server.resolveSibling("firmware").resolve("sd-card").resolve("deck");
            messages.packStore(new PackStore(varDir.resolve("packs"), repositoryPack));
            messages.inbox(new MessageStore(varDir.resolve("messages.json")));
        }
        return messages;
    }

    /** Macro definitions and events; each event is reported on the console. */
    public synchronized MacroService macros() {
        if (macros == null) {
            macros = new MacroService(config, new MacroExecutor(), System.out::println);
        }
        return macros;
    }

    public synchronized TransportRegistry transports() {
        if (transports == null) {
            transports = new TransportRegistry(config, varDir);
        }
        return transports;
    }

    public synchronized PeerAuth auth() {
        if (auth == null) {
            auth = new PeerAuth(config, varDir);
        }
        return auth;
    }

    /**
     * Start from a clean slate: remove every stored telemetry sample and the sampling
     * state (the previous counter reading and the cached frame), so the next sample is
     * a fresh baseline. The configuration is kept.
     *
     * @return how many history samples were removed
     */
    public synchronized int truncateHistory() {
        int removed = sampler().history().clear();
        for (String name : List.of("sample_state.json", "latest.json")) {
            try {
                Files.deleteIfExists(varDir.resolve(name));
            } catch (IOException e) {
                throw new IllegalStateException("Cannot remove " + varDir.resolve(name) + ": " + e.getMessage(), e);
            }
        }
        return removed;
    }

    /** Release resources held by the services (the database connections). */
    public synchronized void close() {
        if (sampler != null) {
            sampler.history().close();
        }
        config.close();
    }
}
