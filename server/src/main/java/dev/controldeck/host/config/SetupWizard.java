package dev.controldeck.host.config;

import dev.controldeck.host.util.Values;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.PrintStream;
import java.util.Map;
import java.util.function.Function;

/**
 * Interactive setup run by {@code --setup} (alias {@code --config}). It asks for the
 * few settings a host needs and stores the answers in the database; the Enter key
 * keeps the value shown in brackets, so a first run can accept every default.
 *
 * Invalid answers are asked again (three tries, then the current value is kept). When
 * the input ends (no terminal, a closed pipe) the remaining questions keep their
 * current value, so a scripted run never hangs.
 */
public final class SetupWizard {

    private static final int ATTEMPTS = 3;

    private final ConfigService config;
    private final BufferedReader in;
    private final PrintStream out;

    public SetupWizard(ConfigService config, BufferedReader in, PrintStream out) {
        this.config = config;
        this.in = in;
        this.out = out;
    }

    /** Ask every question and store the answers. */
    public void run() throws IOException {
        Map<String, Object> current = config.loadRaw();
        Map<String, Object> api = ConfigService.section(current, "api");
        Map<String, Object> telemetry = ConfigService.section(current, "telemetry");
        Map<String, Object> transports = ConfigService.section(current, "transports");
        Map<String, Object> serial = ConfigService.section(transports, "serial");
        Map<String, Object> wifi = ConfigService.section(transports, "wifi");

        out.println();
        out.println("Control Deck host setup");
        out.println("Press Enter to keep the value in brackets. Settings are saved in " + config.path());
        out.println("Running the setup also clears the stored telemetry history, so only new data is kept afterwards.");
        out.println();

        String bind = ask("API address (127.0.0.1 = this computer only, 0.0.0.0 = whole network)",
                String.valueOf(api.get("bind")),
                text -> ConfigService.isIpAddress(text) || "localhost".equals(text) ? null
                        : "Enter an IP address such as 127.0.0.1, 0.0.0.0 or a LAN address.");
        int port = askInt("API port", intOr(api.get("port"), 8080), 1, 65535);
        boolean wifiEnabled = askYesNo("Allow ESP32 boards on the Wi-Fi network to connect (needs a token)",
                ConfigService.truthy(wifi.get("enabled")));
        String serialPort = ask("USB serial port (auto, or a device such as /dev/ttyACM0 or COM4)",
                String.valueOf(serial.get("port")),
                text -> text.length() <= 255 ? null : "That is too long for a device name.");
        int baud = askInt("USB serial baud rate", intOr(serial.get("baud"), 115200), 300, 4_000_000);
        int history = askInt("Minutes of telemetry history to keep (20 to 240)",
                intOr(telemetry.get("history_minutes"), 30), 20, 240);

        config.setWifiSettings(Map.of("enabled", wifiEnabled, "bind", bind, "port", port));
        config.setSerialSettings(Map.of("port", serialPort, "baud", baud));
        config.setHistoryMinutes(history);

        if (wifiEnabled) {
            boolean hasToken = config.getWifiSettings().tokenHash() != null;
            boolean createToken = hasToken
                    ? askYesNo("Replace the existing Wi-Fi token with a new one", false)
                    : askYesNo("Create the Wi-Fi token now (boards need it to connect)", true);
            if (createToken) {
                String token = config.rotateWifiToken();
                out.println();
                out.println("Wi-Fi token: " + token);
                out.println("Copy it now: only its hash is stored, so it cannot be shown again.");
            }
        }
        out.println();
        out.println("Settings saved.");
    }

    private int intOr(Object value, int fallback) {
        Long parsed = Values.validateInt(value);
        return parsed == null ? fallback : parsed.intValue();
    }

    private int askInt(String prompt, int fallback, int min, int max) throws IOException {
        String answer = ask(prompt, Integer.toString(fallback), text -> {
            Long value = Values.validateInt(text);
            return value != null && value >= min && value <= max ? null
                    : "Enter a whole number from " + min + " to " + max + ".";
        });
        return Integer.parseInt(answer);
    }

    private boolean askYesNo(String prompt, boolean fallback) throws IOException {
        String answer = ask(prompt, fallback ? "Y/n" : "y/N", text -> {
            String lower = text.toLowerCase();
            return isYes(lower) || isNo(lower) ? null : "Answer y or n.";
        });
        String lower = answer.toLowerCase();
        if (isYes(lower)) {
            return true;
        }
        if (isNo(lower)) {
            return false;
        }
        return fallback;
    }

    private static boolean isYes(String text) {
        return text.equals("y") || text.equals("yes") || text.equals("s") || text.equals("sim");
    }

    private static boolean isNo(String text) {
        return text.equals("n") || text.equals("no") || text.equals("nao") || text.equals("não");
    }

    /**
     * Ask one question. {@code check} returns null for a valid answer, otherwise the
     * message to show before asking again. An empty answer, or a closed input, returns
     * {@code fallback} (for yes/no questions the fallback is the "Y/n" hint and is
     * resolved by the caller).
     */
    private String ask(String prompt, String fallback, Function<String, String> check) throws IOException {
        for (int attempt = 1; attempt <= ATTEMPTS; attempt++) {
            out.print(prompt + " [" + fallback + "]: ");
            out.flush();
            String line = in.readLine();
            if (line == null) {
                out.println();
                return fallback;
            }
            String answer = line.trim();
            if (answer.isEmpty()) {
                return fallback;
            }
            String problem = check.apply(answer);
            if (problem == null) {
                return answer;
            }
            out.println("  " + problem);
        }
        out.println("  Keeping " + fallback);
        return fallback;
    }
}
