package dev.controldeck.host.transport;

import dev.controldeck.host.protocol.Codec4;
import dev.controldeck.host.protocol.Frame4;
import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;

/**
 * Answers a Deck looking for the host on its network: a protocol 4 discover frame as a UDP broadcast,
 * answered with the name and the API port. The Deck takes the address from the answer. No token
 * or setting leaves here: finding the host gives no access to it.
 */
public final class DiscoveryResponder {

    public static final int DEFAULT_PORT = 8080;
    private static final int MAX_DATAGRAM = 256;
    private static final long LOG_QUIET_MS = 60_000;

    private final DatagramSocket socket;
    private final int apiPort;
    private final BooleanSupplier enabled;
    private final String name;
    private final Map<String, Long> logged = new HashMap<>();

    public DiscoveryResponder(int udpPort, int apiPort, BooleanSupplier enabled) throws SocketException {
        this.socket = new DatagramSocket(udpPort);
        this.apiPort = apiPort;
        this.enabled = enabled;
        this.name = hostName();
    }

    public int port() {
        return socket.getLocalPort();
    }

    public void start() {
        Thread thread = new Thread(this::serve, "control-deck-discovery");
        thread.setDaemon(true);
        thread.start();
    }

    public void stop() {
        socket.close();
    }

    private void serve() {
        byte[] buffer = new byte[MAX_DATAGRAM];
        while (!socket.isClosed()) {
            try {
                DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                socket.receive(packet);
                answer(packet);
            } catch (IOException | RuntimeException error) {
                if (!socket.isClosed()) {
                    Log.error("[wifi] Discovery: " + error);
                }
            }
        }
    }

    private void answer(DatagramPacket packet) throws IOException {
        // One protocol 4 frame per datagram, not COBS-encoded: the datagram has its own end.
        byte[] raw = java.util.Arrays.copyOfRange(packet.getData(), packet.getOffset(),
                packet.getOffset() + packet.getLength());
        Frame4.Frame frame = Frame4.parse(raw);
        if (frame == null || frame.type() != Codec4.DISCOVER || !enabled.getAsBoolean()) {
            return;
        }
        Codec4.Out here = Codec4.encode(Map.of("type", "host_here", "name", name, "port", apiPort));
        byte[] reply = Frame4.build(here.type(), 0, 0, here.payload());
        socket.send(new DatagramPacket(reply, reply.length, packet.getSocketAddress()));
        String address = packet.getAddress().getHostAddress();
        long now = System.currentTimeMillis();
        Long last = logged.get(address);
        if (last == null || now - last > LOG_QUIET_MS) {
            logged.put(address, now);
            Log.info("[wifi] Found by " + address);
        }
    }

    private static String hostName() {
        try {
            return Values.truncateCodePoints(InetAddress.getLocalHost().getHostName(), 23);
        } catch (UnknownHostException e) {
            return "Control Deck";
        }
    }
}
