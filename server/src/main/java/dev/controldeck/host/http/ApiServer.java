package dev.controldeck.host.http;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import dev.controldeck.host.Services;
import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The HTTP API on the JDK's built-in server (com.sun.net.httpserver): no extra
 * dependency.
 */
public final class ApiServer {

    private static final int WORKER_THREADS = 4;

    private final HttpServer server;
    private final ExecutorService executor;
    private final Router router;

    /**
     * @param bind address to listen on ("127.0.0.1" for loopback only)
     * @param port TCP port; 0 picks a free port (used by the self-test)
     */
    public ApiServer(Services services, String bind, int port) throws IOException {
        this.router = new Router(services);
        this.server = HttpServer.create(new InetSocketAddress(bind, port), 0);
        this.executor = Executors.newFixedThreadPool(WORKER_THREADS, runnable -> {
            Thread thread = new Thread(runnable, "control-deck-api");
            thread.setDaemon(true);
            return thread;
        });
        this.server.createContext("/", this::serve);
        this.server.setExecutor(executor);
    }

    public void start() {
        server.start();
    }

    public void stop() {
        server.stop(0);
        executor.shutdownNow();
    }

    /** The port actually bound (useful when port 0 was requested). */
    public int port() {
        return server.getAddress().getPort();
    }

    private void serve(HttpExchange exchange) throws IOException {
        try {
            Request request = readRequest(exchange);
            Response response = router.handle(request);
            send(exchange, response);
        } finally {
            exchange.close();
        }
    }

    private static Request readRequest(HttpExchange exchange) throws IOException {
        byte[] bytes = exchange.getRequestBody().readNBytes(Request.MAX_BODY_BYTES + 1);
        String rawQuery = exchange.getRequestURI().getRawQuery();
        Map<String, String> headers = new java.util.LinkedHashMap<>();
        exchange.getRequestHeaders().forEach((name, values) -> {
            if (!values.isEmpty()) {
                headers.put(name, values.get(0));
            }
        });
        String address = exchange.getRemoteAddress().getAddress().getHostAddress();
        return new Request(
                exchange.getRequestMethod(),
                exchange.getRequestURI().getPath(),
                Request.parseQuery(rawQuery),
                headers,
                new String(bytes, StandardCharsets.UTF_8),
                address,
                bytes);
    }

    private static void send(HttpExchange exchange, Response response) throws IOException {
        byte[] body = response.body() instanceof byte[] raw ? raw
                : Json.encode(response.body()).getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "application/json; charset=utf-8");
        exchange.getResponseHeaders().set("Cache-Control", "no-store");
        exchange.getResponseHeaders().set("X-Content-Type-Options", "nosniff");
        response.headers().forEach((name, value) -> exchange.getResponseHeaders().set(name, value));
        exchange.sendResponseHeaders(response.status(), body.length);
        try (OutputStream out = exchange.getResponseBody()) {
            out.write(body);
        }
    }
}
