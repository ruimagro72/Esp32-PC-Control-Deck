package dev.controldeck.host.transport.serial;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.TreeSet;
import java.util.function.Supplier;
import java.util.regex.Pattern;

/**
 * Chooses the serial port to open. A configured port is only checked for its shape;
 * {@code auto} picks the only USB serial device the system lists and refuses to
 * guess when there are none or several.
 */
public final class PortResolver {

    /** The operating system families the rules differ for. */
    public enum Os {
        LINUX, MAC, WINDOWS, OTHER;

        public static Os detect(String osName) {
            String name = osName == null ? "" : osName.toLowerCase(Locale.ROOT);
            // "darwin" contains "win", so macOS is tested first.
            if (name.contains("mac") || name.contains("darwin")) {
                return MAC;
            }
            if (name.contains("win")) {
                return WINDOWS;
            }
            if (name.contains("linux") || name.contains("nux")) {
                return LINUX;
            }
            return OTHER;
        }
    }

    private static final Pattern LINUX_USB = Pattern.compile("^/dev/tty(ACM|USB)\\d+$");
    private static final Pattern MAC_USB = Pattern.compile("^/dev/cu\\.(usbmodem|usbserial).*");
    private static final Pattern WINDOWS_PORT = Pattern.compile("^(\\\\\\\\\\.\\\\)?COM\\d+$", Pattern.CASE_INSENSITIVE);

    private PortResolver() {
    }

    /**
     * @param configured "auto" or a device name
     * @param ports      the ports the system lists; only asked for when {@code configured} is "auto"
     * @throws IllegalArgumentException when the port cannot be chosen; the message says what to do
     */
    public static String resolve(String configured, Supplier<List<PortInfo>> ports, Os os) throws IOException {
        if (!"auto".equalsIgnoreCase(configured)) {
            validateExplicit(configured, os);
            return configured;
        }

        TreeSet<String> candidates = new TreeSet<>();
        TreeSet<String> espressif = new TreeSet<>();
        for (PortInfo port : ports.get()) {
            // A Deck started on this PC is there because someone wants to work with it.
            if (port.vendorId() == PortInfo.VENDOR_PC_DECK) {
                return port.path();
            }
            if (isUsbSerial(port, os)) {
                candidates.add(port.path());
                if (port.vendorId() == PortInfo.VENDOR_ESPRESSIF) {
                    espressif.add(port.path());
                }
            }
        }
        if (candidates.isEmpty()) {
            throw new IllegalArgumentException("No serial device found; plug the board in, choose a port with "
                    + "--serial-port or in the settings (transports.serial.port), or start the host with --no-serial");
        }
        // Several devices, one of them an ESP32 on its native USB (the ES3C28P Deck): that
        // one. The others (a USB-UART chip such as a CH340, another board) are left alone;
        // several Decks at once come with the multi-Deck bridge.
        if (candidates.size() > 1 && espressif.size() == 1) {
            return espressif.first();
        }
        if (candidates.size() > 1) {
            throw new IllegalArgumentException("Multiple serial devices found; choose one with --serial-port or in the settings: "
                    + String.join(", ", new ArrayList<>(candidates)));
        }
        return candidates.first();
    }

    /** True when {@code auto} would consider this port a board. */
    public static boolean isUsbSerial(PortInfo port, Os os) {
        String path = port.path() == null ? "" : port.path();
        return switch (os) {
            case LINUX -> LINUX_USB.matcher(path).matches();
            case MAC -> MAC_USB.matcher(path).matches();
            case WINDOWS -> {
                String description = port.description() == null ? "" : port.description().toLowerCase(Locale.ROOT);
                yield WINDOWS_PORT.matcher(path).matches() && description.contains("usb")
                        && !description.contains("bluetooth");
            }
            case OTHER -> false;
        };
    }

    static void validateExplicit(String port, Os os) {
        if (port == null || port.isBlank()) {
            throw new IllegalArgumentException("Serial port is empty; use \"auto\" or a device name");
        }
        if (os == Os.WINDOWS) {
            if (!WINDOWS_PORT.matcher(port).matches()) {
                throw new IllegalArgumentException("Windows serial port must use the COM<number> format");
            }
        } else if (!port.startsWith("/dev/")) {
            throw new IllegalArgumentException("Serial device path must start with /dev/");
        }
    }
}
