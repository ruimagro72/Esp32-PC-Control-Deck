package dev.controldeck.host.selftest;

import dev.controldeck.host.config.ConfigService;
import dev.controldeck.host.config.SetupWizard;
import dev.controldeck.host.http.HttpError;
import dev.controldeck.host.http.Request;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.protocol.Protocol;
import dev.controldeck.host.selftest.Runner;
import dev.controldeck.host.telemetry.HostMetrics;
import dev.controldeck.host.telemetry.Inventory;
import dev.controldeck.host.telemetry.MetricRegistry;
import dev.controldeck.host.util.Json;
import dev.controldeck.host.util.Values;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static dev.controldeck.host.selftest.Runner.check;
import static dev.controldeck.host.selftest.Runner.close;
import static dev.controldeck.host.selftest.Runner.equal;

/** Pure and file-based checks: no HTTP server. The configuration tests use SQLite (the shaded jar carries the driver). */
final class UnitTests {

    private UnitTests() {
    }

    static void run(Runner r) {
        valueHelpers(r);
        protocolAndJson(r);
        requestParsing(r);
        registryAndCompute(r);
        fixtureTree(r);
        configFile(r);
        messageHandler(r);
    }

    private static void valueHelpers(Runner r) {
        r.test("round is half away from zero", () -> {
            equal(3.0, Values.round(2.5, 0), "round(2.5)");
            equal(-3.0, Values.round(-2.5, 0), "round(-2.5)");
            equal(2.35, Values.round(2.345, 2), "round(2.345, 2)");
            equal(1.01, Values.round(1.005, 2), "round(1.005, 2)");
        });
        r.test("int validation: sign, no leading zeros, no decimals", () -> {
            equal(12L, Values.validateInt("12"), "\"12\"");
            equal(3L, Values.validateInt("+3"), "\"+3\"");
            equal(5L, Values.validateInt(" 5 "), "\" 5 \"");
            equal(7L, Values.validateInt(7), "Integer 7");
            equal(null, Values.validateInt("012"), "leading zero");
            equal(null, Values.validateInt("1.5"), "decimal");
            equal(null, Values.validateInt(null), "null");
        });
        r.test("truncation counts code points", () -> {
            equal("ab😀", Values.truncateCodePoints("ab😀cd", 3), "emoji truncation");
            equal(3, Values.codePointLength("a😀b"), "code point length");
        });
    }

    private static void protocolAndJson(Runner r) {
        r.test("protocol 4, the only one", () -> {
            equal(4, Protocol.VERSION, "protocol version");
            equal(List.of("stats", "config", "macro", "history", "inventory"), Protocol.CAPABILITIES, "caps");
        });
        r.test("json keeps floats as floats (2.0 stays 2.0)", () -> {
            equal("{\"v\":2.0}", Json.encode(Map.of("v", 2.0)), "float encoding");
        });
    }

    private static void requestParsing(Runner r) {
        r.test("bearer token is read from the Authorization header", () -> {
            Request ok = new Request("POST", "/api/deck", Map.of(), Map.of("Authorization", "Bearer abc"), "", "10.0.0.5");
            equal("abc", ok.bearerToken(), "bearer token");
            Request basic = new Request("POST", "/api/deck", Map.of(), Map.of("authorization", "Basic x"), "", "10.0.0.5");
            equal(null, basic.bearerToken(), "non-bearer scheme");
        });
        r.test("loopback detection ignores proxy headers", () -> {
            check(new Request("GET", "/", Map.of(), Map.of(), "", "127.0.0.1").isLoopback(), "127.0.0.1");
            check(new Request("GET", "/", Map.of(), Map.of(), "", "0:0:0:0:0:0:0:1").isLoopback(), "::1");
            check(!new Request("GET", "/", Map.of(), Map.of("X-Forwarded-For", "127.0.0.1"), "", "10.0.0.5").isLoopback(),
                    "LAN peer with a spoofed header");
        });
        r.test("json body: object or empty only", () -> {
            equal(0, new Request("POST", "/", Map.of(), Map.of(), "   ", "127.0.0.1").json().size(), "blank body");
            try {
                new Request("POST", "/", Map.of(), Map.of(), "[1,2]", "127.0.0.1").json();
                throw new AssertionError("array body must be rejected");
            } catch (HttpError error) {
                equal(400, error.status(), "array body status");
            }
        });
        r.test("query strings are URL-decoded and paths normalised", () -> {
            equal("cpu_usage", Request.parseQuery("minutes=10&metric=cpu%5Fusage").get("metric"), "decoded metric");
            equal("/api/stats", new Request("GET", "api/stats/", Map.of(), Map.of(), "", "127.0.0.1").path(), "path");
        });
    }

    private static void registryAndCompute(Runner r) {
        r.test("metric registry lookups", () -> {
            check(MetricRegistry.has("cpu_usage"), "cpu_usage exists");
            check(!MetricRegistry.has("bogus"), "bogus does not");
            equal("net_down_mbps", MetricRegistry.fieldOf("net_down"), "field of net_down");
            equal(List.of("net_down", "net_up"), MetricRegistry.forComponent("network"), "network metrics");
            equal("disk_read", MetricRegistry.defaultFor("disk"), "default disk metric");
            check(!MetricRegistry.isValidWindow(7), "7 minutes is not a window");
        });
        r.test("rates come from two readings and never from one", () -> {
            HostMetrics metrics = new HostMetrics("", "auto", null);
            HostMetrics.Reading before = reading(1000.0, 1000, 800, 1_000_000L, 500_000L, 1000L, 1000L, null);
            HostMetrics.Reading after = reading(1002.0, 2000, 1600, 3_000_000L, 1_000_000L, 3000L, 3000L, null);

            Map<String, Object> frame = metrics.compute(before, after);
            close(20.0, asDouble(frame.get("cpu")), 1e-9, "cpu");
            close(8.0, asDouble(frame.get("net_down_mbps")), 1e-9, "download");
            close(2.0, asDouble(frame.get("net_up_mbps")), 1e-9, "upload");
            // 2000 sectors of 512 B in 2 s, kept to the byte (6 decimals): 0.48828125 MB/s
            close(0.488281, asDouble(frame.get("disk_read_mbs")), 1e-9, "disk read");
            equal(16.0, asDouble(frame.get("ram_total_gb")), "ram total from memory reading");

            Map<String, Object> first = metrics.compute(null, after);
            equal(null, first.get("cpu"), "cpu without a previous reading");
            equal(null, first.get("net_down_mbps"), "net without a previous reading");
            equal(16.0, asDouble(first.get("ram_total_gb")), "ram is instantaneous");
        });
        r.test("gpu power from the energy counter of the GPU device", () -> {
            HostMetrics metrics = new HostMetrics("", "auto", null);
            HostMetrics.Reading before = reading(1000.0, 1000, 800, 0L, 0L, 0L, 0L, 1_000_000_000.0);
            HostMetrics.Reading after = reading(1002.0, 1000, 800, 0L, 0L, 0L, 0L, 1_060_000_000.0);
            close(30.0, asDouble(metrics.compute(before, after).get("gpu_power_watts")), 1e-9, "gpu power");
        });
        r.test("a reading survives the JSON state file (rates work across requests)", () -> {
            HostMetrics.Reading original = reading(1000.0, 1000, 800, 1_000_000L, 500_000L, 1000L, 1000L, 1_000_000_000.0);
            HostMetrics.Reading back = Json.decode(Json.encode(original), HostMetrics.Reading.class);
            check(back != null, "state file must decode back into a reading");
            equal(original, back, "round-tripped reading");
        });
        r.test("unsupported platform yields a frame of nulls", () -> {
            HostMetrics.Reading off = new HostMetrics.Reading(1.0, false, null, null, null, null, null, null, 0, "");
            Map<String, Object> frame = new HostMetrics("", "auto", null).compute(null, off);
            equal("stats", frame.get("type"), "frame type");
            equal(null, frame.get("cpu"), "cpu");
            equal(null, frame.get("ram"), "ram");
        });
    }

    private static void fixtureTree(Runner r) {
        r.test("reads /proc and /sys from a fixture tree", () -> {
            Path root = Files.createTempDirectory("cd-fixture");
            write(root, "proc/stat", "cpu  100 0 50 800 10 0 0 0 0 0\ncpu0 1 2 3 4\n");
            write(root, "proc/meminfo", "MemTotal:       16777216 kB\nMemAvailable:    8388608 kB\n");
            write(root, "proc/net/route",
                    "Iface\tDestination\tGateway\tFlags\n"
                            + "eth0\t00000000\t0102A8C0\t0003\t0\t0\t0\t00000000\t0\t0\t0\n");
            write(root, "proc/net/dev",
                    "Inter-|   Receive\n face |bytes\n"
                            + "    lo: 5 0 0 0 0 0 0 0 5 0 0 0 0 0 0 0\n"
                            + "  eth0: 1000 0 0 0 0 0 0 0 2000 0 0 0 0 0 0 0\n");
            write(root, "sys/class/hwmon/hwmon0/name", "coretemp\n");
            write(root, "sys/class/hwmon/hwmon0/temp1_label", "Package id 0\n");
            write(root, "sys/class/hwmon/hwmon0/temp1_input", "52000\n");

            HostMetrics.Reading reading = new HostMetrics(root.toString(), "auto", null).read();
            check(reading.supported(), "fixture root counts as supported");
            equal(960L, reading.cpuTicks().total(), "cpu total ticks");
            equal(810L, reading.cpuTicks().idle(), "cpu idle ticks (idle + iowait)");
            close(16.0, reading.memory().totalGb(), 1e-9, "memory total");
            close(50.0, reading.memory().usagePercent(), 1e-9, "memory usage");
            equal("eth0", reading.net().interfaceName(), "default interface");
            equal(1000L, reading.net().counters().get("eth0").rx(), "rx counter");
            close(52.0, reading.temps().cpu(), 1e-9, "cpu temperature from coretemp package sensor");
        });
        r.test("a GPU whose driver reports no memory size does not break the sample or the inventory", () -> {
            Path root = Files.createTempDirectory("cd-gpu-novram");
            write(root, "proc/stat", "cpu  100 0 50 800 10 0 0 0 0 0\n");
            write(root, "proc/meminfo", "MemTotal:       16777216 kB\nMemAvailable:    8388608 kB\n");
            // An AMD card (amdgpu): its size file is read by the gpu-amd plugin (docs/03-SERVER.md).
            write(root, "sys/class/drm/card0/device/vendor", "0x1002\n");
            write(root, "sys/class/drm/card0/device/device", "0x73bf\n");
            write(root, "sys/class/drm/card0/device/gpu_busy_percent", "12\n");
            Files.createDirectories(root.resolve("sys/bus/pci/drivers/amdgpu"));
            Files.createSymbolicLink(root.resolve("sys/class/drm/card0/device/driver"), root.resolve("sys/bus/pci/drivers/amdgpu"));

            // No size from the driver and none configured: the size is simply unknown (null).
            HostMetrics.Reading reading = new HostMetrics(root.toString(), "auto", null).read();
            check(reading.gpu() != null, "the card is found");
            equal(null, reading.gpu().vramTotalGb(), "unknown VRAM size is null, not an exception");
            Map<String, Object> gpu = new Inventory(root.toString(), "auto", null).component("gpu");
            equal(null, gpu.get("vram_total_gb"), "inventory: unknown VRAM size");
            equal(null, gpu.get("vram_total_source"), "inventory: no source");

            // A configured size is used when the driver reports none.
            equal(16.0, new HostMetrics(root.toString(), "auto", 16.0).read().gpu().vramTotalGb(), "configured size");
            Map<String, Object> configured = new Inventory(root.toString(), "auto", 16.0).component("gpu");
            equal(16.0, configured.get("vram_total_gb"), "inventory: configured size");
            equal("config", configured.get("vram_total_source"), "inventory: source is the configuration");

            // The driver's size wins over the configured one.
            write(root, "sys/class/drm/card0/device/mem_info_vram_total", "8589934592\n");
            equal(8.0, new HostMetrics(root.toString(), "auto", 16.0).read().gpu().vramTotalGb(), "driver size");
            equal("driver", new Inventory(root.toString(), "auto", 16.0).component("gpu").get("vram_total_source"),
                    "inventory: source is the driver");
        });
        r.test("inventory reads the CPU model from a fixture tree", () -> {
            Path root = Files.createTempDirectory("cd-inventory");
            write(root, "proc/cpuinfo",
                    "processor\t: 0\nmodel name\t: Test CPU 9000\nphysical id\t: 0\ncore id\t\t: 0\n"
                            + "processor\t: 1\nmodel name\t: Test CPU 9000\nphysical id\t: 0\ncore id\t\t: 0\n");
            Map<String, Object> cpu = new Inventory(root.toString(), "auto", null).component("cpu");
            equal("Test CPU 9000", cpu.get("model"), "model");
            equal(2, cpu.get("logical_threads"), "threads");
            equal(1, cpu.get("physical_cores"), "cores de-duplicated by physical:core");
        });
    }

    private static void configFile(Runner r) {
        r.test("legacy flat keys are migrated and defaults filled", () -> {
            Map<String, Object> config = ConfigService.normalize(Json.parseObject(
                    "{\"serial_port\":\"COM4\",\"baud\":9600,\"interval_seconds\":1.5,\"disk_path\":\"auto\"}"));
            equal("COM4", ((Map<?, ?>) ((Map<?, ?>) config.get("transports")).get("serial")).get("port"), "serial port");
            equal(9600, ((Map<?, ?>) ((Map<?, ?>) config.get("transports")).get("serial")).get("baud"), "baud");
            equal(1.5, ((Map<?, ?>) config.get("telemetry")).get("interval_seconds"), "interval");
            equal(30, ((Map<?, ?>) config.get("telemetry")).get("history_minutes"), "default retention");
            equal(2, config.get("config_version"), "version");
        });
        r.test("peer token is stored as SHA-256 and cleared on request", () -> {
            Path dir = Files.createTempDirectory("cd-config");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            String token = config.rotateWifiToken();
            equal(48, token.length(), "token length (24 random bytes, hex)");
            equal(ConfigService.sha256Hex(token), config.getWifiSettings().tokenHash(), "stored hash");
            check(!databaseText(dir.resolve("control-deck.db")).contains(token), "plain token must not be stored");
            config.clearWifiToken();
            equal(null, config.getWifiSettings().tokenHash(), "cleared hash");
            config.close();
        });
        r.test("wifi settings reject bad bind and port", () -> {
            Path dir = Files.createTempDirectory("cd-config-wifi");
            ConfigService config = new ConfigService(dir.resolve("control-deck.db"));
            try {
                config.setWifiSettings(Map.of("bind", "not an address"));
                throw new AssertionError("bad bind must be rejected");
            } catch (IllegalArgumentException expected) {
                // expected
            }
            try {
                config.setWifiSettings(Map.of("port", 70000));
                throw new AssertionError("port 70000 must be rejected");
            } catch (IllegalArgumentException expected) {
                // expected
            }
            config.setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0", "port", 8080));
            equal(true, config.getWifiSettings().enabled(), "enabled saved");
            equal("0.0.0.0", config.getWifiSettings().bind(), "bind saved");
            config.close();
        });
        r.test("first run creates the default settings in the database, with no config file", () -> {
            Path dir = Files.createTempDirectory("cd-first-run");
            Path db = dir.resolve("not-yet").resolve("control-deck.db");
            ConfigService config = new ConfigService(db);
            check(!Files.exists(db), "building the service must not touch the disk");
            equal(true, config.initialize(), "defaults created on the first run");
            equal(false, config.initialize(), "a second call creates nothing");
            Map<?, ?> api = (Map<?, ?>) config.load().get("api");
            equal("127.0.0.1", api.get("bind"), "default bind");
            equal(8080, api.get("port"), "default port");
            equal(2, config.load().get("config_version"), "config version");
            check(Files.isRegularFile(db), "the database file exists");
            check(!Files.exists(dir.resolve("config.json")) && !Files.exists(db.resolveSibling("config.json")),
                    "no config.json is written");
            config.close();
        });
        r.test("settings survive a restart", () -> {
            Path db = Files.createTempDirectory("cd-restart").resolve("control-deck.db");
            ConfigService first = new ConfigService(db);
            first.setWifiSettings(Map.of("enabled", true, "bind", "0.0.0.0", "port", 9191));
            first.setSerialSettings(Map.of("port", "/dev/ttyACM1", "baud", 9600));
            first.setHistoryMinutes(45);
            first.close();

            ConfigService second = new ConfigService(db);
            equal(false, second.initialize(), "a database with settings is not reset");
            Map<?, ?> config = second.load();
            equal(9191, ((Map<?, ?>) config.get("api")).get("port"), "port");
            equal(45, ((Map<?, ?>) config.get("telemetry")).get("history_minutes"), "history minutes");
            equal("/dev/ttyACM1", second.getSerialSettings().get("port"), "serial port");
            equal(9600, second.getSerialSettings().get("baud"), "baud");
            equal(true, second.getWifiSettings().enabled(), "wifi enabled");
            second.close();
        });
        r.test("serial and history settings reject bad values", () -> {
            ConfigService config = new ConfigService(Files.createTempDirectory("cd-bad").resolve("control-deck.db"));
            rejects(() -> config.setSerialSettings(Map.of("baud", 0)), "baud 0");
            rejects(() -> config.setSerialSettings(Map.of("port", "  ")), "blank port");
            rejects(() -> config.setHistoryMinutes(10), "10 minutes of history");
            rejects(() -> config.setHistoryMinutes(500), "500 minutes of history");
            equal("auto", config.getSerialSettings().get("port"), "port unchanged");
            config.close();
        });
        r.test("setup asks again after a bad answer, keeps Enter defaults and prints the token once", () -> {
            Path db = Files.createTempDirectory("cd-setup").resolve("control-deck.db");
            ConfigService config = new ConfigService(db);
            // bind (bad, then good), port, wifi yes, serial port (Enter), baud, history (bad, then good), token (Enter = yes)
            String input = "999.1.1.1\n0.0.0.0\n9090\ny\n\n9600\nabc\n60\n\n";
            ByteArrayOutputStream captured = new ByteArrayOutputStream();
            new SetupWizard(config, new BufferedReader(new StringReader(input)),
                    new PrintStream(captured, true, StandardCharsets.UTF_8)).run();
            String output = captured.toString(StandardCharsets.UTF_8);

            check(output.contains("Enter an IP address"), "a bad bind is explained");
            check(output.contains("Enter a whole number from 20 to 240"), "a bad history value is explained");
            equal("0.0.0.0", config.getWifiSettings().bind(), "bind");
            equal(9090, config.getWifiSettings().port(), "port");
            equal(true, config.getWifiSettings().enabled(), "wifi");
            equal("auto", config.getSerialSettings().get("port"), "Enter keeps the serial port");
            equal(9600, config.getSerialSettings().get("baud"), "baud");
            equal(60, ((Map<?, ?>) config.load().get("telemetry")).get("history_minutes"), "history minutes");

            String marker = "Wi-Fi token: ";
            int start = output.indexOf(marker);
            check(start >= 0, "the token is shown");
            String token = output.substring(start + marker.length()).lines().findFirst().orElse("").trim();
            equal(48, token.length(), "token length");
            equal(ConfigService.sha256Hex(token), config.getWifiSettings().tokenHash(), "only the hash is stored");
            config.close();
        });
        r.test("setup with the input closed keeps every current value", () -> {
            Path db = Files.createTempDirectory("cd-setup-eof").resolve("control-deck.db");
            ConfigService config = new ConfigService(db);
            ByteArrayOutputStream captured = new ByteArrayOutputStream();
            new SetupWizard(config, new BufferedReader(new StringReader("")),
                    new PrintStream(captured, true, StandardCharsets.UTF_8)).run();
            equal("127.0.0.1", config.getWifiSettings().bind(), "bind");
            equal(8080, config.getWifiSettings().port(), "port");
            equal(false, config.getWifiSettings().enabled(), "wifi stays off");
            equal(null, config.getWifiSettings().tokenHash(), "no token without wifi");
            check(!captured.toString(StandardCharsets.UTF_8).contains("Wi-Fi token"), "no token is printed");
            config.close();
        });
    }

    /** Text of the database file and its write-ahead log, to prove a secret is not stored in clear. */
    private static String databaseText(Path db) throws Exception {
        StringBuilder text = new StringBuilder(new String(Files.readAllBytes(db), StandardCharsets.ISO_8859_1));
        Path wal = db.resolveSibling(db.getFileName() + "-wal");
        if (Files.exists(wal)) {
            text.append(new String(Files.readAllBytes(wal), StandardCharsets.ISO_8859_1));
        }
        return text.toString();
    }

    private static void rejects(Runnable action, String what) {
        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            return;
        }
        throw new AssertionError(what + " must be rejected");
    }

    private static void messageHandler(Runner r) {
        MessageHandler handler = new MessageHandler(new ConfigService(Path.of("/nonexistent/control-deck.db")), null, null);
        r.test("protocol replies for the requests that need no telemetry", () -> {
            equal("pong", handler.handle(Map.of("type", "ping"), 65536).get(0).get("type"), "ping");
            Map<String, Object> missing = handler.handle(Map.of(), 65536).get(0);
            equal("missing_type", missing.get("code"), "missing type code");
            equal("unknown_type", handler.handle(Map.of("type", "nope"), 65536).get(0).get("code"), "unknown type code");
            equal("invalid_request", handler.handle(Map.of("type", "macro", "id", 9), 65536).get(0).get("code"),
                    "macro id out of range");
            Map<String, Object> macro = handler.handle(Map.of("type", "macro", "id", 1), 65536).get(0);
            equal("macro_result", macro.get("type"), "a macro event is answered with macro_result");
            equal(false, macro.get("ok"), "nothing ran: the configuration cannot be read");
            check(macro.get("detail") instanceof String, "the answer says why");
            equal("invalid_request", handler.handle(Map.of("type", "history_request", "metric", "bogus"), 65536).get(0).get("code"),
                    "unknown metric");
        });
        r.test("error details are truncated to 120 characters", () -> {
            String detail = "x".repeat(300);
            equal(120, MessageHandler.error("internal_error", detail).get("detail").toString().length(), "detail length");
        });
    }

    // -- helpers -------------------------------------------------------------------

    private static HostMetrics.Reading reading(double time, long total, long idle, long rx, long tx,
                                               long sectorsRead, long sectorsWritten, Double energyUj) {
        HostMetrics.CpuTicks cpu = new HostMetrics.CpuTicks(total, idle);
        HostMetrics.Memory memory = new HostMetrics.Memory(16.0, 8.0, 50.0);
        HostMetrics.Net net = new HostMetrics.Net("eth0", Map.of("eth0", new HostMetrics.Rx(rx, tx)));
        HostMetrics.Disk disk = new HostMetrics.Disk("sda", "sda", "/", 40.0, 1.0, 2.0, sectorsRead, sectorsWritten);
        HostMetrics.Gpu gpu = energyUj == null ? null
                : new HostMetrics.Gpu(10.0, 1.0, 8.0, 1500, 60.0, null, energyUj, "i915", 1600, 5000.0, 0,
                        Map.of("render", 12.5, "video", 0.0), 42L);
        HostMetrics.Temps temps = new HostMetrics.Temps(50.0, 40.0, 35.0, 60.0);
        return new HostMetrics.Reading(time, true, cpu, memory, net, disk, gpu, temps, 0, "00:00");
    }

    private static Double asDouble(Object value) {
        return value == null ? null : ((Number) value).doubleValue();
    }

    private static void write(Path root, String relative, String content) throws java.io.IOException {
        Path file = root.resolve(relative);
        Files.createDirectories(file.getParent());
        Files.writeString(file, content, StandardCharsets.UTF_8);
    }
}
