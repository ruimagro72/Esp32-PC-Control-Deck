package dev.controldeck.host.display;

import java.io.File;
import java.security.SecureRandom;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;
import java.util.function.Supplier;

/**
 * A virtual monitor for a Deck's VNC client: KDE's krfb-virtualmonitor makes an extra screen and
 * serves it over VNC; WsTcpBridge (plain Java, no Python) turns it into the WebSocket the
 * Deck's VNC app speaks. One monitor at a time; a new request with another size replaces it.
 * The WebSocket listens on the API's Deck-facing address (loopback when none is set) and needs
 * the Deck's Bearer token.
 */
public final class VirtualMonitor {
    public static final int VNC_PORT = 5910, WS_PORT = 5911;
    private static final String CHARS = "abcdefghijkmnpqrstuvwxyz23456789";
    /** krfb writes here: var/log/vmonitor.log next to the server's other logs. */
    private static final File LOG = new File("var/log/vmonitor.log");

    private final Supplier<String> bindAddress;
    private final Predicate<String> tokenAccepted;
    private Process monitor;
    private WsTcpBridge bridge;
    private String resolution = "", password = "";

    /**
     * @param bindAddress   the Deck-facing address of the API; the WebSocket listens only there
     * @param tokenAccepted true for a Bearer token the Deck may use
     */
    public VirtualMonitor(Supplier<String> bindAddress, Predicate<String> tokenAccepted) {
        this.bindAddress = bindAddress;
        this.tokenAccepted = tokenAccepted;
        Runtime.getRuntime().addShutdownHook(new Thread(this::stop, "virtual-monitor-stop"));   // never leave it behind
    }

    /** Starts (or keeps) the monitor; the answer has what the VNC app needs to connect. */
    public synchronized Map<String, Object> start(String wanted) {
        String res = wanted == null || !wanted.matches("\\d{3,4}x\\d{3,4}") ? "320x240" : wanted;
        if (running() && res.equals(resolution)) return info();
        stop();
        if (!onPath("krfb-virtualmonitor")) throw new IllegalStateException("krfb-virtualmonitor is not installed");
        password = randomPassword();
        resolution = res;
        String bind = bridgeBind();
        Map<String, Integer> before = screenPriorities();   // KDE reorders them when a screen appears
        try {
            monitor = new ProcessBuilder("krfb-virtualmonitor", "--resolution", res, "--name", "Deck",
                    "--password", password, "--port", String.valueOf(VNC_PORT))
                    .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.appendTo(LOG)).start();
            // WebSocket -> VNC in Java, frames of 8 KB at most (the ESP32 WebSocket library drops
            // frames over about 15 KB; with 64 KB frames the Deck disconnected on the first picture).
            bridge = new WsTcpBridge(WS_PORT, bind, "127.0.0.1", VNC_PORT, tokenAccepted);
        } catch (Exception e) {
            stop();
            throw new IllegalStateException("cannot start the virtual monitor: " + e.getMessage());
        }
        // Answer only when both ports accept connections, or the Deck connects too early.
        String probe = "0.0.0.0".equals(bind) ? "127.0.0.1" : bind;
        if (!listening("127.0.0.1", VNC_PORT, 5000) || !listening(probe, WS_PORT, 3000)) {
            stop();
            throw new IllegalStateException("the virtual monitor did not start listening");
        }
        restorePriorities(before);
        return info();
    }

    // ---- KDE screens: the new virtual screen made Plasma move the main screen's panel (13 Nov) ----

    /** Screen name -> priority (1 = main) from kscreen-doctor; empty when it is not there. */
    private static Map<String, Integer> screenPriorities() {
        Map<String, Integer> out = new LinkedHashMap<>();
        if (!onPath("kscreen-doctor")) return out;
        try {
            Process p = new ProcessBuilder("kscreen-doctor", "-o").redirectErrorStream(true).start();
            String text = new String(p.getInputStream().readAllBytes()).replaceAll("\u001B\\[[0-9;]*m", "");
            p.waitFor(3, TimeUnit.SECONDS);
            String name = null;
            for (String line : text.split("\n")) {
                String t = line.trim();
                if (t.startsWith("Output:")) {
                    String[] w = t.split("\\s+");
                    name = w.length >= 3 ? w[2] : null;
                } else if (name != null && t.startsWith("priority ")) {
                    out.put(name, Integer.parseInt(t.substring(9).trim()));
                }
            }
        } catch (Exception e) {
            return new LinkedHashMap<>();
        }
        return out;
    }

    /** Once the virtual screen is listed, give the real screens back their priorities (it goes last). */
    private static void restorePriorities(Map<String, Integer> before) {
        if (before.isEmpty()) return;
        for (int i = 0; i < 30; i++) {   // up to 3 s for KDE to list the new screen
            Map<String, Integer> now = screenPriorities();
            if (now.size() > before.size()) {
                if (now.entrySet().stream().allMatch(e -> !before.containsKey(e.getKey())
                        || before.get(e.getKey()).equals(e.getValue()))) return;
                java.util.List<String> cmd = new java.util.ArrayList<>(java.util.List.of("kscreen-doctor"));
                before.forEach((n, prio) -> cmd.add("output." + n + ".priority." + prio));
                try {
                    new ProcessBuilder(cmd).redirectErrorStream(true)
                            .redirectOutput(ProcessBuilder.Redirect.DISCARD).start().waitFor(5, TimeUnit.SECONDS);
                } catch (Exception e) {
                    Thread.currentThread().interrupt();
                }
                return;
            }
            try { Thread.sleep(100); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
        }
    }

    /** The WebSocket's address: the API's Deck-facing address, loopback when none is set. */
    private String bridgeBind() {
        String bind = bindAddress.get();
        return bind == null || bind.isBlank() ? "127.0.0.1" : bind;
    }

    private static boolean listening(String host, int port, long ms) {
        long end = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < end) {
            try (java.net.Socket s = new java.net.Socket()) {
                s.connect(new java.net.InetSocketAddress(host, port), 200);
                return true;
            } catch (java.io.IOException e) {
                try { Thread.sleep(100); } catch (InterruptedException ie) { Thread.currentThread().interrupt(); return false; }
            }
        }
        return false;
    }

    public synchronized Map<String, Object> stop() {
        kill(monitor);
        if (bridge != null) bridge.close();
        monitor = null;
        bridge = null;
        resolution = "";
        password = "";
        return Map.of("running", false);
    }

    public synchronized Map<String, Object> info() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("running", running());
        if (running()) {
            m.put("resolution", resolution);
            m.put("ws_port", WS_PORT);
            m.put("password", password);
        }
        return m;
    }

    private boolean running() {
        return monitor != null && monitor.isAlive() && bridge != null && bridge.isOpen();
    }

    private static void kill(Process p) {
        if (p == null) return;
        p.destroy();
        try {
            if (!p.waitFor(2, TimeUnit.SECONDS)) p.destroyForcibly();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            p.destroyForcibly();
        }
    }

    private static boolean onPath(String tool) {
        String path = System.getenv("PATH");
        if (path == null) return false;
        for (String dir : path.split(File.pathSeparator)) {
            if (new File(dir, tool).canExecute()) return true;
        }
        return false;
    }

    /** VNC passwords use 8 characters at most. */
    private static String randomPassword() {
        SecureRandom r = new SecureRandom();
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < 8; i++) b.append(CHARS.charAt(r.nextInt(CHARS.length())));
        return b.toString();
    }
}
