package dev.controldeck.host.transport.serial;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Optional;
import com.fazecast.jSerialComm.SerialPort;
import com.fazecast.jSerialComm.SerialPortInvalidPortException;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * The real serial ports, through jSerialComm (Linux, macOS and Windows; the native
 * libraries are inside the library jar). This is the only class that knows the library.
 */
public final class JSerialCommProvider implements SerialPortProvider {

    /** A read returns after this long even when nothing arrived, so the bridge loop stays responsive. */
    private static final int READ_TIMEOUT_MS = 20;
    private static final int WRITE_TIMEOUT_MS = 2000;

    @Override
    public List<PortInfo> listPorts() throws IOException {
        try {
            List<PortInfo> ports = new ArrayList<>();
            for (SerialPort port : SerialPort.getCommPorts()) {
                String path = port.getSystemPortPath();
                if (path == null || path.isEmpty()) {
                    path = port.getSystemPortName();
                }
                ports.add(new PortInfo(normalize(path), port.getPortDescription(), port.getVendorID()));
            }
            pcDeckPort().ifPresent(ports::add);
            ports.sort(Comparator.comparing(PortInfo::path));
            return ports;
        } catch (LinkageError error) {
            throw new IOException("The serial library could not be loaded: " + error, error);
        }
    }

    /**
     * The PC build of the firmware has no USB port: it opens a pseudo terminal and says where
     * in a file, with its process id, since a dead Deck's terminal may belong to someone else.
     */
    private static Optional<PortInfo> pcDeckPort() {
        String dir = System.getenv("XDG_RUNTIME_DIR");
        Path file = Path.of(dir != null && !dir.isEmpty() ? dir : System.getProperty("java.io.tmpdir"),
                "control-deck-pc-deck");
        try {
            String[] fields = Files.readString(file).trim().split(" ", 2);
            if (fields.length == 2 && ProcessHandle.of(Long.parseLong(fields[0])).map(ProcessHandle::isAlive).orElse(false)) {
                return Optional.of(new PortInfo(fields[1], "Deck on this PC", PortInfo.VENDOR_PC_DECK));
            }
        } catch (IOException | NumberFormatException e) {
            // No Deck on this PC, or a file that is not ours.
        }
        return Optional.empty();
    }

    @Override
    public SerialPortIO open(String path, int baud) throws IOException {
        SerialPort port;
        try {
            port = SerialPort.getCommPort(path);
        } catch (SerialPortInvalidPortException error) {
            throw new IOException("Unable to open serial port " + path + ": " + error.getMessage(), error);
        } catch (LinkageError error) {
            throw new IOException("The serial library could not be loaded: " + error, error);
        }

        port.setComPortParameters(baud, 8, SerialPort.ONE_STOP_BIT, SerialPort.NO_PARITY);
        port.setFlowControl(SerialPort.FLOW_CONTROL_DISABLED);
        port.setComPortTimeouts(SerialPort.TIMEOUT_READ_SEMI_BLOCKING | SerialPort.TIMEOUT_WRITE_BLOCKING,
                READ_TIMEOUT_MS, WRITE_TIMEOUT_MS);
        if (!port.openPort()) {
            throw new IOException("Unable to open serial port " + path + " (error " + port.getLastErrorCode()
                    + "). Check that no other program has it open (a serial monitor, another host) "
                    + "and that your user may use it (Linux: group dialout or uucp).");
        }
        return new Io(port);
    }

    /** Windows reports \\.\COM3; the settings and the messages use COM3. */
    private static String normalize(String path) {
        return path.startsWith("\\\\.\\") ? path.substring(4) : path;
    }

    private static final class Io implements SerialPortIO {
        private final SerialPort port;

        Io(SerialPort port) {
            this.port = port;
        }

        @Override
        public int read(byte[] buffer) throws IOException {
            if (!port.isOpen()) {
                throw new IOException("Serial port disconnected");
            }
            int count = port.readBytes(buffer, buffer.length, 0);
            if (count < 0) {
                throw new IOException("Serial port disconnected");
            }
            return count;
        }

        @Override
        public void write(byte[] data, int offset, int length) throws IOException {
            int done = 0;
            while (done < length) {
                int count = port.writeBytes(data, length - done, offset + done);
                if (count <= 0) {
                    throw new IOException("Serial port disconnected while writing a protocol frame");
                }
                done += count;
            }
        }

        @Override
        public void close() {
            port.closePort();
        }
    }
}
