package dev.controldeck.host.macro;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/**
 * Runs the action configured for a macro.
 *
 * Processes are started detached and never waited for, so a long-running command (for
 * example {@code ollama serve}) cannot block the HTTP request or the serial bridge.
 * Commands are always argument lists, never shell strings: on Unix the list is handed
 * to {@code sh} as separate arguments ({@code "$@"}), so nothing in it is interpreted.
 *
 * Supported: url, command, service. {@code hotkey} and {@code text} need keyboard
 * injection, which this host does not implement; they say so instead of pretending.
 */
public final class MacroExecutor {

    /**
     * @param executed    the action was started (or the service already ran)
     * @param unsupported the action type is not implemented by this host
     * @param status      service only: already_running, started or start_failed; else null
     */
    public record Result(boolean executed, boolean unsupported, String status, String message) {

        static Result ok(String message) {
            return new Result(true, false, null, message);
        }

        static Result failed(String message) {
            return new Result(false, false, null, message);
        }

        Result withStatus(String newStatus, String newMessage) {
            return new Result(executed, unsupported, newStatus, newMessage);
        }
    }

    /** Starts a process detached; returns false when it could not be started. */
    @FunctionalInterface
    public interface Launcher {
        boolean launch(List<String> command, Path workingDirectory) throws IOException;
    }

    /** True when the service's health address answers with a 2xx or 3xx status. */
    @FunctionalInterface
    public interface HealthProbe {
        boolean healthy(String url);
    }

    public enum Platform {
        LINUX("linux"), MACOS("macos"), WINDOWS("windows"), OTHER("other");

        private final String key;

        Platform(String key) {
            this.key = key;
        }

        /** The key used in per-platform values: linux, macos, windows. */
        public String key() {
            return key;
        }

        public static Platform detect(String osName) {
            String name = osName == null ? "" : osName.toLowerCase(Locale.ROOT);
            if (name.contains("linux")) {
                return LINUX;
            }
            if (name.contains("mac") || name.contains("darwin")) {
                return MACOS;
            }
            if (name.contains("windows")) {
                return WINDOWS;
            }
            return OTHER;
        }
    }

    // Detached start: setsid when it exists, all output to /dev/null; the shell exits at once.
    private static final String DETACH_SCRIPT = "if command -v setsid >/dev/null 2>&1; then "
            + "setsid \"$@\" >/dev/null 2>&1 </dev/null & else \"$@\" >/dev/null 2>&1 </dev/null & fi";

    private static final List<List<String>> TERMINALS = List.of(
            List.of("x-terminal-emulator", "-e"),
            List.of("gnome-terminal", "--"),
            List.of("konsole", "-e"),
            List.of("xfce4-terminal", "-x"),
            List.of("xterm", "-e"));

    private final Platform platform;
    private final Map<String, String> env;
    private final Launcher launcher;
    private final HealthProbe probe;

    /** The executor for this computer. */
    public MacroExecutor() {
        this(Platform.detect(System.getProperty("os.name")), System.getenv(), null, MacroExecutor::probe);
    }

    /**
     * @param env      environment used for PATH, DISPLAY and WAYLAND_DISPLAY
     * @param launcher null for the real detached start
     */
    public MacroExecutor(Platform platform, Map<String, String> env, Launcher launcher, HealthProbe probe) {
        this.platform = platform;
        this.env = env;
        this.launcher = launcher != null ? launcher : (command, dir) -> launchDetached(platform, command, dir);
        this.probe = probe;
    }

    public Result execute(Macro macro) {
        return switch (macro.action()) {
            case "url" -> openUrl(macro);
            case "service" -> ensureService(macro);
            case "command" -> runCommand(macro);
            default -> new Result(false, true, null, "Action '" + macro.action()
                    + "' is not supported by the Java host (it requires keyboard injection)");
        };
    }

    private Result openUrl(Macro macro) {
        if (!(macro.value() instanceof String url) || !isHttpUrl(url)) {
            return Result.failed("URL not configured or not an http(s) URL");
        }
        if (platform == Platform.LINUX && !hasGraphicalSession()) {
            return Result.failed("No graphical session (DISPLAY/WAYLAND_DISPLAY) for the host process");
        }
        List<String> command = switch (platform) {
            case MACOS -> List.of("open", url);
            case WINDOWS -> List.of("cmd", "/c", "start", "", url);
            default -> List.of("xdg-open", url);
        };
        return launch(command, null, "Opened URL: " + url);
    }

    private Result runCommand(Macro macro) {
        List<String> command = platformValue(macro.value(), false);
        if (command == null) {
            return Result.failed("No command configured for this platform");
        }

        Path workingDirectory = null;
        List<String> directory = platformValue(macro.workingDirectory(), true);
        if (directory != null) {
            workingDirectory = Path.of(String.join("", directory));
            if (!Files.isDirectory(workingDirectory)) {
                return Result.failed("Working directory does not exist");
            }
        }

        if (macro.newConsole() && platform == Platform.LINUX) {
            if (!hasGraphicalSession()) {
                return Result.failed("new_console needs a graphical session (DISPLAY/WAYLAND_DISPLAY)");
            }
            List<String> terminal = findTerminal();
            if (terminal == null) {
                return Result.failed("No terminal emulator found for new_console");
            }
            List<String> wrapped = new ArrayList<>(terminal);
            wrapped.addAll(command);
            command = wrapped;
        }

        return launch(command, workingDirectory, "Started: " + command.get(0));
    }

    private Result ensureService(Macro macro) {
        String healthUrl = macro.healthUrl();
        if (healthUrl == null || !isHttpUrl(healthUrl)) {
            return Result.failed("Health URL not configured or not an http(s) URL");
        }
        if (probe.healthy(healthUrl)) {
            return new Result(true, false, "already_running", "Service already running");
        }
        Result result = runCommand(macro);
        return result.executed()
                ? result.withStatus("started", "Service was not reachable; start command launched")
                : result.withStatus("start_failed", result.message());
    }

    private Result launch(List<String> command, Path workingDirectory, String successMessage) {
        if (platform != Platform.WINDOWS && !commandExists(command.get(0))) {
            return Result.failed("Command not found: " + command.get(0));
        }
        try {
            if (!launcher.launch(command, workingDirectory)) {
                return Result.failed("Unable to start the process");
            }
        } catch (IOException | RuntimeException error) {
            return Result.failed("Unable to start the process");
        }
        return Result.ok(successMessage);
    }

    /**
     * The argument list for this platform: a list as is, or the entry of a map keyed by
     * platform (falling back to "default"). With {@code scalarAllowed} a non-empty string
     * is a one-element list (used for the working directory). Null when nothing usable.
     */
    List<String> platformValue(Object value, boolean scalarAllowed) {
        Object candidate;
        if (value instanceof List<?>) {
            candidate = value;
        } else if (value instanceof Map<?, ?> map) {
            candidate = map.get(platform.key()) != null ? map.get(platform.key()) : map.get("default");
        } else if (scalarAllowed && value instanceof String text && !text.isEmpty()) {
            return List.of(text);
        } else {
            return null;
        }

        if (scalarAllowed && candidate instanceof String text && !text.isEmpty()) {
            return List.of(text);
        }
        if (!(candidate instanceof List<?> items) || items.isEmpty()) {
            return null;
        }
        List<String> arguments = new ArrayList<>();
        for (Object item : items) {
            if (item instanceof String text) {
                arguments.add(text);
            } else if (item instanceof Number number) {
                double d = number.doubleValue();
                arguments.add(d == Math.rint(d) && !Double.isInfinite(d) ? Long.toString((long) d) : String.valueOf(d));
            } else {
                return null;
            }
        }
        return arguments;
    }

    static boolean isHttpUrl(String url) {
        try {
            URI uri = new URI(url);
            String scheme = uri.getScheme() == null ? "" : uri.getScheme().toLowerCase(Locale.ROOT);
            return (scheme.equals("http") || scheme.equals("https")) && uri.getHost() != null;
        } catch (Exception e) {
            return false;
        }
    }

    private boolean hasGraphicalSession() {
        return !env.getOrDefault("DISPLAY", "").isEmpty() || !env.getOrDefault("WAYLAND_DISPLAY", "").isEmpty();
    }

    private List<String> findTerminal() {
        for (List<String> terminal : TERMINALS) {
            if (commandExists(terminal.get(0))) {
                return terminal;
            }
        }
        return null;
    }

    boolean commandExists(String command) {
        if (command.isEmpty()) {
            return false;
        }
        if (command.contains("/")) {
            Path path = Path.of(command);
            return Files.isRegularFile(path) && Files.isExecutable(path);
        }
        for (String directory : env.getOrDefault("PATH", "").split(File.pathSeparator)) {
            if (!directory.isEmpty()) {
                Path path = Path.of(directory, command);
                if (Files.isRegularFile(path) && Files.isExecutable(path)) {
                    return true;
                }
            }
        }
        return false;
    }

    private static boolean launchDetached(Platform platform, List<String> command, Path workingDirectory)
            throws IOException {
        ProcessBuilder builder;
        if (platform == Platform.WINDOWS) {
            // Best effort, not validated on Windows.
            builder = new ProcessBuilder(command);
        } else {
            List<String> shell = new ArrayList<>(List.of("sh", "-c", DETACH_SCRIPT, "sh"));
            shell.addAll(command);
            builder = new ProcessBuilder(shell);
            builder.redirectInput(ProcessBuilder.Redirect.from(new File("/dev/null")));
        }
        if (workingDirectory != null) {
            builder.directory(workingDirectory.toFile());
        }
        builder.redirectOutput(ProcessBuilder.Redirect.DISCARD);
        builder.redirectError(ProcessBuilder.Redirect.DISCARD);
        Process process = builder.start();
        if (platform != Platform.WINDOWS) {
            // Only waits for the short-lived launcher shell, not for the command.
            try {
                if (!process.waitFor(5, TimeUnit.SECONDS)) {
                    process.destroy();
                    return false;
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
            return process.exitValue() == 0;
        }
        return true;
    }

    private static boolean probe(String url) {
        try {
            HttpClient client = HttpClient.newBuilder()
                    .connectTimeout(Duration.ofSeconds(2))
                    .followRedirects(HttpClient.Redirect.NEVER)
                    .build();
            HttpRequest request = HttpRequest.newBuilder(URI.create(url)).timeout(Duration.ofSeconds(2)).GET().build();
            int status = client.send(request, HttpResponse.BodyHandlers.discarding()).statusCode();
            return status >= 200 && status < 400;
        } catch (IOException | IllegalArgumentException e) {
            return false;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }
}
