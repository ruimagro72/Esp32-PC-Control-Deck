package dev.controldeck.host.transport.ble;

import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.transport.TransportRegistry;
import dev.controldeck.host.transport.serial.JSerialCommProvider;
import dev.controldeck.host.transport.serial.PortResolver;
import dev.controldeck.host.transport.serial.SerialBridge;
import dev.controldeck.host.transport.serial.SerialLog;
import dev.controldeck.host.transport.serial.SerialSession;
import dev.controldeck.host.util.Log;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.Supplier;

/**
 * Decks on Bluetooth LE (docs/04-PROTOCOL.md). The radio is handled by a separate program,
 * {@code server/ble-bridge} (Linux, BlueZ), which gives each paired Deck a pseudo terminal and
 * names it in {@code $XDG_RUNTIME_DIR/control-deck-ble/}. This class starts that program when
 * it is built, watches the folder and runs a {@link SerialBridge} on each terminal, so a BLE
 * Deck gets the same session as a USB one. Each is reported as {@code ble:<name>} in the
 * transport registry.
 */
public final class BleBridges {

    private static final long SCAN_MS = 2000;
    private static final long RECONNECT_DELAY_MS = 2000;

    private record Link(String path, SerialBridge bridge) {
    }

    private final Supplier<SerialBridge.Settings> settings;
    private final MessageHandler messages;
    private final SerialSession.Stats stats;
    private final TransportRegistry registry;
    private final Path folder;
    private final Path helper;
    private final Map<String, Link> links = new HashMap<>();
    private volatile boolean running;
    private Thread thread;
    private Process helperProcess;

    public BleBridges(Supplier<SerialBridge.Settings> settings, MessageHandler messages, SerialSession.Stats stats,
                      TransportRegistry registry, Path helper) {
        this.settings = settings;
        this.messages = messages;
        this.stats = stats;
        this.registry = registry;
        this.helper = helper;
        String runtime = System.getenv("XDG_RUNTIME_DIR");
        this.folder = Path.of(runtime != null && !runtime.isEmpty() ? runtime : System.getProperty("java.io.tmpdir"),
                "control-deck-ble");
    }

    public synchronized void start() {
        if (running) {
            return;
        }
        running = true;
        startHelper();
        thread = new Thread(this::run, "control-deck-ble");
        thread.setDaemon(true);
        thread.start();
    }

    public void stop() {
        synchronized (this) {
            running = false;
        }
        if (thread != null) {
            thread.interrupt();
        }
        synchronized (links) {
            links.values().forEach(link -> link.bridge().stop());
            links.clear();
        }
        if (helperProcess != null) {
            helperProcess.destroy();
        }
    }

    /** The radio side, when it has been built (make in server/ble-bridge); its lines go to the log. */
    private void startHelper() {
        if (!Files.isExecutable(helper)) {
            Log.info("[ble] " + helper + " is not built: BLE Decks are not reached (make in server/ble-bridge). "
                    + "Simulated BLE Decks on this PC still are.");
            return;
        }
        try {
            helperProcess = new ProcessBuilder(helper.toString()).redirectErrorStream(true).start();
        } catch (IOException error) {
            Log.error("[ble] Cannot start " + helper + ": " + error.getMessage());
            return;
        }
        Thread reader = new Thread(() -> {
            try (BufferedReader lines = new BufferedReader(
                    new InputStreamReader(helperProcess.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = lines.readLine()) != null) {
                    Log.info("[ble] " + line);
                }
            } catch (IOException e) {
                // The helper ended.
            }
            if (running) {
                Log.error("[ble] ble-bridge stopped (exit " + helperProcess.exitValue() + ")");
            }
        }, "control-deck-ble-helper");
        reader.setDaemon(true);
        reader.start();
    }

    private void run() {
        while (running) {
            scan();
            try {
                Thread.sleep(SCAN_MS);
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    /** One bridge per live file; a file that went away, or whose process died, ends its bridge. */
    private void scan() {
        Map<String, String> found = new HashMap<>();
        if (Files.isDirectory(folder)) {
            try (DirectoryStream<Path> files = Files.newDirectoryStream(folder)) {
                for (Path file : files) {
                    String path = livePath(file);
                    if (path != null) {
                        found.put(file.getFileName().toString(), path);
                    }
                }
            } catch (IOException e) {
                // Read again at the next scan.
            }
        }
        synchronized (links) {
            Set<String> gone = new HashSet<>(links.keySet());
            gone.removeAll(found.keySet());
            for (String name : gone) {
                end(name);
            }
            for (Map.Entry<String, String> entry : found.entrySet()) {
                Link link = links.get(entry.getKey());
                if (link != null && link.path().equals(entry.getValue())) {
                    continue;
                }
                if (link != null) {
                    end(entry.getKey());
                }
                begin(entry.getKey(), entry.getValue());
            }
        }
    }

    private void begin(String name, String path) {
        String id = "ble:" + name;
        SerialBridge bridge = new SerialBridge(() -> {
            SerialBridge.Settings base = settings.get();
            return new SerialBridge.Settings(true, path, base.baud(), base.intervalSeconds());
        }, messages, stats, registry, new JSerialCommProvider(), SerialLog.console("[ble] " + name + ": "),
                RECONNECT_DELAY_MS, PortResolver.Os.detect(System.getProperty("os.name")), id);
        links.put(name, new Link(path, bridge));
        bridge.start();
    }

    private void end(String name) {
        Link link = links.remove(name);
        if (link != null) {
            link.bridge().stop();
            registry.update("ble:" + name, SerialSession.fields("state", "stopped"));
            Log.info("[ble] " + name + ": gone");
        }
    }

    /** "<pid> <pty> <description>": the terminal, while the process that wrote it lives. */
    private static String livePath(Path file) {
        try {
            String[] fields = Files.readString(file).trim().split(" ", 3);
            if (fields.length >= 2 && ProcessHandle.of(Long.parseLong(fields[0]))
                    .map(ProcessHandle::isAlive).orElse(false)) {
                return fields[1];
            }
        } catch (IOException | NumberFormatException e) {
            // Not ours, or being written.
        }
        return null;
    }
}
