package dev.controldeck.host;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.config.HostPaths;
import dev.controldeck.host.config.SetupWizard;
import dev.controldeck.host.http.ApiServer;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.selftest.SelfTest;
import dev.controldeck.host.transport.DiscoveryResponder;
import dev.controldeck.host.transport.ble.BleBridges;
import dev.controldeck.host.transport.openaction.OpenActionClient;
import dev.controldeck.host.transport.serial.JSerialCommProvider;
import dev.controldeck.host.transport.serial.PortInfo;
import dev.controldeck.host.transport.serial.PortResolver;
import dev.controldeck.host.transport.serial.SerialBridge;
import dev.controldeck.host.transport.serial.SerialLog;
import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.SocketException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;

/**
 * Entry point of the Java host.
 *
 * <pre>
 *   java -jar control-deck-host-0.7.0-all.jar                 serve the HTTP API
 *   java -jar control-deck-host-0.7.0-all.jar --setup         ask for the settings, truncate the history, then serve
 *   java -jar control-deck-host-0.7.0-all.jar --bind 0.0.0.0 --port 8080
 *   java -jar control-deck-host-0.7.0-all.jar --list-ports    show the serial ports and which one "auto" picks
 *   java -jar control-deck-host-0.7.0-all.jar --selftest      run the self-test battery
 *   java -jar control-deck-host-0.7.0-all.jar --import-macros FILE
 *                                                             copy the macros of an old PHP host config.json, then exit
 * </pre>
 *
 * The settings live in the SQLite database (var/control-deck.db). The first run
 * creates the defaults there, so no configuration file is needed.
 * The USB serial bridge talks to the board by default (--no-serial turns it off).
 * Macros run when the board (or a peer) sends their id; they are defined with the
 * host-only /api/macros routes or imported from an old PHP host config with --import-macros.
 */
public final class Main {

    private static final String VERSION = "0.7.0";
    private static final long RECONNECT_DELAY_MS = 2000;

    private Main() {
    }

    public static void main(String[] args) throws Exception {
        System.setProperty("java.awt.headless", "true");
        List<String> argv = List.of(args);
        if (argv.contains("--help") || argv.contains("-h")) {
            printUsage();
            return;
        }
        if (argv.contains("--version")) {
            System.out.println("control-deck-host " + VERSION + " (protocol " + Protocol.VERSION + ")");
            return;
        }
        if (argv.contains("--list-ports")) {
            System.exit(listPorts());
            return;
        }
        if (argv.contains("--selftest")) {
            System.exit(SelfTest.run());
            return;
        }
        if (argv.contains("--console")) {
            System.exit(console(argv));
            return;
        }
        if (argv.contains("--import-macros")) {
            System.exit(importMacros(optionValue(argv, "--import-macros", "")));
            return;
        }
        serve(argv);
    }

    private static void serve(List<String> argv) throws Exception {
        Services services;
        try {
            services = Services.forHost();
        } catch (IllegalStateException error) {
            System.err.println("Cannot start: " + error.getMessage());
            System.err.println("The runtime folder must be writable (CONTROL_DECK_VAR changes where it is).");
            System.exit(2);
            return;
        }
        ConfigService config = services.config();
        boolean setup = argv.contains("--setup") || argv.contains("--config");
        Map<String, Object> api;
        try {
            boolean created = config.initialize();
            if (setup) {
                new SetupWizard(config, new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8)),
                        System.out).run();
                int removed = services.truncateHistory();
                System.out.println("History truncated (" + removed + " old samples removed): the new data starts now.");
            } else if (created) {
                System.out.println("No configuration found: the default settings were saved in " + config.path()
                        + ". Run with --setup to change them.");
            }
            api = config.load().get("api") instanceof Map<?, ?> m ? castMap(m) : Map.of();
        } catch (IllegalStateException | java.io.IOException error) {
            System.err.println("Cannot load the configuration: " + error.getMessage());
            System.err.println("The settings are stored in the SQLite database " + config.path()
                    + ". Its folder must be writable (CONTROL_DECK_VAR or CONTROL_DECK_DB change where it is).");
            System.exit(2);
            return;
        }

        String bind = optionValue(argv, "--bind", String.valueOf(api.getOrDefault("bind", "127.0.0.1")));
        Long port = Values.validateInt(optionValue(argv, "--port", String.valueOf(api.getOrDefault("port", 8080))));
        if (port == null || port < 1 || port > 65535) {
            System.err.println("--port must be an integer between 1 and 65535");
            System.exit(2);
            return;
        }

        Integer baudOverride = null;
        if (argv.contains("--baud")) {
            Long baud = Values.validateInt(optionValue(argv, "--baud", ""));
            if (baud == null || baud < 1 || baud > 4_000_000) {
                System.err.println("--baud must be a positive integer");
                System.exit(2);
                return;
            }
            baudOverride = baud.intValue();
        }
        String serialPort = argv.contains("--serial-port") ? optionValue(argv, "--serial-port", "") : null;
        if (serialPort != null && serialPort.isBlank()) {
            System.err.println("--serial-port needs a device name (or auto)");
            System.exit(2);
            return;
        }

        // Started by OpenDeck as its plugin. With a host already running, it takes over; this
        // process stays while OpenDeck is connected, as a plugin is expected to.
        int openActionPort = 0;
        if (argv.contains("-pluginUUID")) {
            openActionPort = Values.validateInt(optionValue(argv, "-port", "0")).intValue();
            if (attachToRunningHost(port.intValue(), openActionPort, optionValue(argv, "-registerEvent", "registerPlugin"),
                    optionValue(argv, "-pluginUUID", ""))) {
                return;
            }
        }
        ApiServer server = new ApiServer(services, bind, port.intValue());
        services.messages().setListenAddress(bind, port.intValue());
        SerialBridge serial = argv.contains("--no-serial") ? null : new SerialBridge(
                SerialBridge.settingsFrom(config, serialPort, baudOverride), services.messages(),
                age -> services.sampler().sample(age).frame(), services.transports(), new JSerialCommProvider(),
                SerialLog.console(), RECONNECT_DELAY_MS, PortResolver.Os.detect(System.getProperty("os.name")));
        BleBridges ble = argv.contains("--no-ble") ? null : new BleBridges(
                SerialBridge.settingsFrom(config, serialPort, baudOverride), services.messages(),
                age -> services.sampler().sample(age).frame(), services.transports(), bleHelper());
        // A host that listens on this computer only has nothing to tell the network.
        DiscoveryResponder discovery = null;
        if (!List.of("127.0.0.1", "localhost", "::1").contains(bind)) {
            try {
                discovery = new DiscoveryResponder(DiscoveryResponder.DEFAULT_PORT, port.intValue(),
                        () -> config.getWifiSettings().enabled());
            } catch (SocketException error) {
                Log.error("[wifi] Decks cannot find this host by themselves: " + error.getMessage());
            }
        }
        final DiscoveryResponder responder = discovery;
        CountDownLatch stopped = new CountDownLatch(1);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            if (serial != null) {
                serial.stop();
            }
            if (ble != null) {
                ble.stop();
            }
            if (responder != null) {
                responder.stop();
            }
            server.stop();
            services.close();
            stopped.countDown();
        }, "control-deck-shutdown"));

        server.start();
        if (ble != null) {
            ble.start();
        }
        if (openActionPort > 0) {
            OpenActionClient.connect(services.messages().openAction(), openActionPort,
                    optionValue(argv, "-registerEvent", "registerPlugin"), optionValue(argv, "-pluginUUID", ""));
        }
        if (responder != null) {
            responder.start();
        }
        Log.info("Control Deck Java host " + VERSION + " listening on http://" + bind + ":" + server.port()
                + " (protocol " + Protocol.VERSION + ")");
        try {
            int macros = config.getMacros().size();
            if (macros == 0) {
                Log.info("No macros configured: define them with POST /api/macros (from this computer) "
                        + "or copy a PHP host config.json with --import-macros FILE.");
            } else {
                Log.info(macros + " macro(s) configured.");
            }
        } catch (IllegalStateException error) {
            Log.error("Cannot read the macros: " + error.getMessage());
        }
        if (serial != null) {
            serial.start();
            Log.info("USB serial bridge started: it looks for the board and reconnects by itself "
                    + "(--list-ports shows what it sees, --no-serial turns it off).");
        } else {
            Log.info("USB serial bridge is off (--no-serial).");
        }
        stopped.await();
    }

    /**
     * --import-macros FILE: copy the "macros" list of a PHP host config.json into this
     * host's database (replacing what is there). Every macro is checked first, so a bad
     * file changes nothing.
     */
    private static boolean attachToRunningHost(int apiPort, int port, String event, String uuid) throws InterruptedException {
        HttpClient client = HttpClient.newHttpClient();
        URI base = URI.create("http://127.0.0.1:" + apiPort + "/api/openaction");
        try {
            String body = Json.encode(Map.of("port", port, "register_event", event, "uuid", uuid));
            HttpResponse<String> answer = client.send(HttpRequest.newBuilder(URI.create(base + "/attach"))
                    .header("Content-Type", "application/json").POST(HttpRequest.BodyPublishers.ofString(body)).build(),
                    HttpResponse.BodyHandlers.ofString());
            if (answer.statusCode() != 200) {
                return false;
            }
        } catch (IOException error) {
            return false;  // no host here: this process becomes the host
        }
        // Attached: this process only stays while the host is attached. If the host goes away it
        // ends, and does not become a host in its place (a stopped host must stay stopped).
        try {
            while (client.send(HttpRequest.newBuilder(base).GET().build(), HttpResponse.BodyHandlers.ofString())
                    .body().contains("\"attached\":true")) {
                Thread.sleep(2000);
            }
        } catch (IOException error) {
            // the host stopped
        }
        return true;
    }

    /**
     * --console [--deck ID] [--port N] [line]: the Deck's console through the running server
     * (docs/CONSOLE.md). The port is --port, else the saved api.port.
     */
    private static int console(List<String> argv) {
        String fallback = "8080";
        if (!argv.contains("--port")) {
            try {
                ConfigService config = Services.forHost().config();
                Map<String, Object> api = config.load().get("api") instanceof Map<?, ?> m ? castMap(m) : Map.of();
                fallback = String.valueOf(api.getOrDefault("port", 8080));
                config.close();
            } catch (RuntimeException error) {
                // no settings to read: the default port
            }
        }
        Long port = Values.validateInt(optionValue(argv, "--port", fallback));
        if (port == null || port < 1 || port > 65535) {
            System.err.println("--port must be an integer between 1 and 65535");
            return 2;
        }
        return dev.controldeck.host.console.ConsoleClient.run(argv, port.intValue());
    }

    private static int importMacros(String file) {
        if (file.isBlank() || file.startsWith("--")) {
            System.err.println("--import-macros needs the path of the PHP host's config.json");
            return 2;
        }
        Map<String, Object> source;
        try {
            source = dev.controldeck.host.util.Json.parseObject(
                    java.nio.file.Files.readString(java.nio.file.Path.of(file), StandardCharsets.UTF_8));
        } catch (java.io.IOException | RuntimeException error) {
            System.err.println("Cannot read " + file + ": " + error.getMessage());
            return 1;
        }
        if (source == null || !(source.get("macros") instanceof List<?> list)) {
            System.err.println(file + " has no \"macros\" list");
            return 1;
        }
        List<Map<String, Object>> macros = new java.util.ArrayList<>();
        try {
            for (Object item : list) {
                if (!(item instanceof Map<?, ?> map)) {
                    throw new IllegalArgumentException("every macro must be a JSON object");
                }
                Map<String, Object> macro = castMap(map);
                dev.controldeck.host.macro.Macro.fromMap(macro); // action and label length
                macros.add(macro);
            }
            ConfigService config = Services.forHost().config();
            config.initialize();
            config.setMacros(macros);
            System.out.println("Imported " + macros.size() + " macro(s) from " + file + " into " + config.path());
            for (Map<String, Object> macro : macros) {
                System.out.println("  " + macro.get("id") + "  " + macro.get("label") + "  (" + macro.get("action") + ")");
            }
            config.close();
            return 0;
        } catch (IllegalArgumentException | IllegalStateException error) {
            System.err.println("Nothing imported: " + error.getMessage());
            return 1;
        }
    }

    /** --list-ports: the serial ports the system reports and what "auto" would choose. */
    private static int listPorts() {
        PortResolver.Os os = PortResolver.Os.detect(System.getProperty("os.name"));
        List<PortInfo> ports;
        try {
            ports = new JSerialCommProvider().listPorts();
        } catch (java.io.IOException error) {
            System.err.println("Cannot list the serial ports: " + error.getMessage());
            return 1;
        }
        if (ports.isEmpty()) {
            System.out.println("No serial ports found.");
        }
        for (PortInfo port : ports) {
            String description = port.description() == null || port.description().isBlank() ? "" : "  " + port.description();
            if (port.vendorId() >= 0) {
                description += String.format("  (USB %04x)", port.vendorId());
            }
            System.out.println("  " + port.path() + description + (PortResolver.isUsbSerial(port, os) ? "   <- looks like a board" : ""));
        }
        try {
            System.out.println("auto would use: " + PortResolver.resolve("auto", () -> ports, os));
        } catch (IllegalArgumentException | java.io.IOException error) {
            System.out.println("auto: " + error.getMessage());
        }
        return 0;
    }

    private static String optionValue(List<String> argv, String name, String fallback) {
        int index = argv.indexOf(name);
        if (index >= 0 && index + 1 < argv.size()) {
            return argv.get(index + 1);
        }
        return fallback;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> castMap(Map<?, ?> value) {
        return (Map<String, Object>) value;
    }

    private static void printUsage() {
        System.out.println("""
                ESP32 PC Control Deck - Java host %s

                Usage:
                  java -jar control-deck-host-%s-all.jar [options]

                Options:
                  --setup, --config  ask for the settings (saved in the database) and truncate the
                                     telemetry history, so only new data is stored afterwards
                  --bind ADDRESS     listen address for this run (default: the saved api.bind, 127.0.0.1)
                  --port N           listen port for this run (default: the saved api.port, 8080)
                  --no-serial        do not talk to the board over USB (the API still runs)
                  --no-ble           do not reach Decks over Bluetooth LE (server/ble-bridge)
                  --serial-port DEV  serial port for this run: auto, /dev/ttyACM0, COM4 ... (default: the
                                     saved transports.serial.port, auto)
                  --baud N           baud rate for this run (default: the saved one, 115200;
                                     behind a CH340/CP210x 921600 and 115200 are tried too)
                  --list-ports       list the serial ports, mark the ones that look like a board, and exit
                  --import-macros FILE  copy the macros of a PHP host config.json into the database,
                                     replacing the current ones, and exit
                  --console [--deck ID] [LINE]
                                     the Deck's console through the running server (--port: its
                                     port): LINE is sent and its answer printed; without LINE,
                                     lines are read until quit, with the Deck's live log
                  --selftest         run the self-test battery and exit
                  --version          print the version and exit
                  -h, --help         this help

                The settings are kept in the SQLite database var/control-deck.db. The first run
                creates the default settings there, so no configuration file is needed.

                Environment:
                  ESP32_SERIAL, ESP32_BAUD  serial port and baud rate (--serial-port and
                                     --baud win over them, and they win over the saved settings)
                  CONTROL_DECK_DB       path of the database (default: <var>/control-deck.db)
                  CONTROL_DECK_VAR      runtime directory (default: ./var)
                  CONTROL_DECK_HOST_DIR host directory used for relative paths (default: working directory)
                """.formatted(VERSION, VERSION));
    }

    /**
     * The BLE radio program: CONTROL_DECK_BLE_BRIDGE, or server/ble-bridge/build/ble-bridge
     * (the jar is in server/target).
     */
    private static Path bleHelper() {
        String override = System.getenv("CONTROL_DECK_BLE_BRIDGE");
        if (override != null && !override.isBlank()) {
            return Path.of(override);
        }
        try {
            Path jar = Path.of(Main.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            return jar.getParent().getParent().resolve("ble-bridge").resolve("build").resolve("ble-bridge");
        } catch (Exception error) {
            return HostPaths.hostDir().resolve("ble-bridge/build/ble-bridge").normalize();
        }
    }
}
