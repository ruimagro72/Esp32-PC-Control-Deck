package dev.controldeck.host.display;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Base64;
import java.util.Locale;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * WebSocket to TCP bridge in plain Java (replaces websockify, which is Python): a WebSocket client
 * (the Deck's VNC app) on {@code listenPort} is joined to a TCP server (VNC) on {@code target}.
 * Binary frames only, each at most {@link #MAX_FRAME} bytes, because the ESP32 WebSocket library
 * drops frames over about 15 KB.
 *
 * <p>The bridge listens on one address only (the Deck-facing address of the API), and every
 * upgrade needs {@code Authorization: Bearer <token>} for which {@code tokenAccepted} answers true.
 * No peer is exempt, loopback included: the VNC password must not be the only lock.
 */
public final class WsTcpBridge implements AutoCloseable {
    static final int MAX_FRAME = 8192;
    private static final String GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    private static final Pattern BEARER = Pattern.compile("^Bearer\\s+(\\S+)$", Pattern.CASE_INSENSITIVE);

    private final ServerSocket server;
    private final String targetHost;
    private final int targetPort;
    private final Predicate<String> tokenAccepted;
    private volatile boolean open = true;

    public WsTcpBridge(int listenPort, String bindAddress, String targetHost, int targetPort,
                       Predicate<String> tokenAccepted) throws IOException {
        this.targetHost = targetHost;
        this.targetPort = targetPort;
        this.tokenAccepted = tokenAccepted;
        server = new ServerSocket();
        server.setReuseAddress(true);
        server.bind(new InetSocketAddress(bindAddress, listenPort));
        Thread t = new Thread(this::acceptLoop, "ws-bridge-" + listenPort);
        t.setDaemon(true);
        t.start();
    }

    /**
     * Standalone development bridge (firmware/tools/vnc-bridge.sh): loopback only. Its upgrades
     * need the token in the environment variable CONTROL_DECK_BRIDGE_TOKEN; without it every
     * upgrade is refused.
     * java -cp control-deck-host-*-all.jar dev.controldeck.host.display.WsTcpBridge [LISTEN] [HOST:PORT]
     */
    public static void main(String[] a) throws Exception {
        int listen = a.length > 0 ? Integer.parseInt(a[0]) : 5901;
        String target = a.length > 1 ? a[1] : "127.0.0.1:5900";
        int c = target.lastIndexOf(':');
        String token = System.getenv("CONTROL_DECK_BRIDGE_TOKEN");
        new WsTcpBridge(listen, "127.0.0.1", target.substring(0, c), Integer.parseInt(target.substring(c + 1)),
                presented -> token != null && !token.isEmpty() && token.equals(presented));
        System.out.println("Bridge: ws://127.0.0.1:" + listen + " -> " + target + " (loopback, Ctrl+C to stop)");
        Thread.currentThread().join();
    }

    public boolean isOpen() { return open && !server.isClosed(); }

    @Override
    public void close() {
        open = false;
        try { server.close(); } catch (IOException ignored) { }
    }

    private void acceptLoop() {
        while (open) {
            try {
                Socket ws = server.accept();
                Thread t = new Thread(() -> serve(ws), "ws-bridge-client");
                t.setDaemon(true);
                t.start();
            } catch (IOException e) {
                if (!open) return;
            }
        }
    }

    private void serve(Socket ws) {
        try (ws) {
            InputStream wsIn = ws.getInputStream();
            OutputStream wsOut = ws.getOutputStream();
            if (!handshake(wsIn, wsOut)) return;   // refused peers never reach the VNC server
            try (Socket tcp = new Socket(targetHost, targetPort)) {
                tcp.setTcpNoDelay(true);
                ws.setTcpNoDelay(true);
                Thread up = new Thread(() -> wsToTcp(wsIn, tcp, ws), "ws-bridge-up");
                up.setDaemon(true);
                up.start();
                // TCP -> WebSocket: binary frames of at most MAX_FRAME bytes, unmasked (server side).
                InputStream tcpIn = tcp.getInputStream();
                byte[] buf = new byte[MAX_FRAME];
                int n;
                while ((n = tcpIn.read(buf)) > 0) {
                    synchronized (wsOut) { writeFrame(wsOut, 0x2, buf, n); }
                }
                synchronized (wsOut) { writeFrame(wsOut, 0x8, new byte[]{0x03, (byte) 0xE8}, 2); }   // close 1000
            }
        } catch (IOException ignored) {
            // either side went away: the sockets close here
        }
    }

    /**
     * HTTP upgrade; answers the "binary" subprotocol when the client asks for it. A peer without
     * an accepted Bearer token gets 401 and the connection closes.
     */
    private boolean handshake(InputStream in, OutputStream out) throws IOException {
        String key = null, protocols = null, authorization = null;
        for (String line : readHeaders(in).split("\r\n")) {
            int c = line.indexOf(':');
            if (c < 0) continue;
            String name = line.substring(0, c).trim().toLowerCase(Locale.ROOT), value = line.substring(c + 1).trim();
            if (name.equals("sec-websocket-key")) key = value;
            else if (name.equals("sec-websocket-protocol")) protocols = value;
            else if (name.equals("authorization")) authorization = value;
        }
        Matcher bearer = authorization == null ? null : BEARER.matcher(authorization);
        String token = bearer != null && bearer.matches() ? bearer.group(1) : null;
        if (token == null || !tokenAccepted.test(token)) {
            out.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"
                    .getBytes(StandardCharsets.US_ASCII));
            out.flush();
            return false;
        }
        if (key == null) return false;
        String accept;
        try {
            accept = Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-1")
                    .digest((key + GUID).getBytes(StandardCharsets.US_ASCII)));
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
        StringBuilder r = new StringBuilder("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                + "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n");
        if (protocols != null) {
            for (String p : protocols.split(",")) {
                if (p.trim().equals("binary")) { r.append("Sec-WebSocket-Protocol: binary\r\n"); break; }
            }
        }
        out.write(r.append("\r\n").toString().getBytes(StandardCharsets.US_ASCII));
        out.flush();
        return true;
    }

    private static String readHeaders(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int state = 0, c;
        while ((c = in.read()) >= 0 && b.size() < 8192) {
            b.write(c);
            state = (c == '\r' && (state == 0 || state == 2)) || (c == '\n' && (state == 1 || state == 3)) ? state + 1 : 0;
            if (state == 4) break;
        }
        return b.toString(StandardCharsets.US_ASCII);
    }

    /** WebSocket -> TCP: client frames are masked; data frames are passed on, close ends both. */
    private static void wsToTcp(InputStream raw, Socket tcp, Socket ws) {
        try {
            DataInputStream in = new DataInputStream(raw);
            OutputStream tcpOut = tcp.getOutputStream();
            OutputStream wsOut = ws.getOutputStream();
            while (true) {
                int b0 = in.readUnsignedByte(), b1 = in.readUnsignedByte();
                int op = b0 & 0x0F;
                long len = b1 & 0x7F;
                if (len == 126) len = in.readUnsignedShort();
                else if (len == 127) len = in.readLong();
                byte[] mask = new byte[4];
                if ((b1 & 0x80) != 0) in.readFully(mask);
                if (len > 1 << 20) break;   // nothing the Deck sends is that big
                byte[] data = new byte[(int) len];
                in.readFully(data);
                for (int i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
                if (op == 0x8) break;                                   // close
                if (op == 0x9) { synchronized (wsOut) { writeFrame(wsOut, 0xA, data, data.length); } continue; }   // ping -> pong
                if (op == 0x0 || op == 0x1 || op == 0x2) { tcpOut.write(data); tcpOut.flush(); }
            }
        } catch (IOException ignored) {
            // the client went away
        }
        try { tcp.close(); } catch (IOException ignored) { }
        try { ws.close(); } catch (IOException ignored) { }
    }

    private static void writeFrame(OutputStream out, int op, byte[] data, int n) throws IOException {
        out.write(0x80 | op);
        if (n < 126) out.write(n);
        else { out.write(126); out.write(n >> 8); out.write(n & 0xFF); }   // n <= MAX_FRAME < 65536
        out.write(data, 0, n);
        out.flush();
    }
}
