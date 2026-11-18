package dev.controldeck.host.console;

import dev.controldeck.host.util.Json;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Map;

/**
 * {@code ./start.sh server console [--deck ID] [--port N] [line]}: the Deck's console in a
 * terminal (docs/CONSOLE.md). It talks to the running server on this computer only (its
 * loopback API, {@code /api/console}); the server carries the lines to the Deck inside
 * protocol 4. With a line it prints the answer and ends; without one it reads lines until
 * end of input ("quit" or Ctrl-D) and prints the Deck's live log lines as they come.
 */
public final class ConsoleClient {

    private static final long ANSWER_WAIT_MS = 8_000;
    private static final long LOG_POLL_MS = 500;

    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(3)).build();
    private final String base;
    private final String deck;
    private final PrintStream out;

    public ConsoleClient(int port, String deck, PrintStream out) {
        this.base = "http://127.0.0.1:" + port + "/api/console";
        this.deck = deck == null ? "" : deck;
        this.out = out;
    }

    /** One line: its answer printed; the exit code (0 answered, 1 no answer, 2 refused). */
    public int once(String line) {
        try {
            return send(line) ? 0 : 1;
        } catch (IOException | IllegalStateException error) {
            out.println("console: " + error.getMessage());
            return 2;
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
            return 1;
        }
    }

    /** Lines from {@code in} until its end or "quit"; the live log printed meanwhile. */
    public int interactive(BufferedReader in) {
        long[] after = {0};
        try {
            after[0] = lastLogSeq();
        } catch (IOException | IllegalStateException error) {
            out.println("console: " + error.getMessage());
            return 2;
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
            return 1;
        }
        Thread logs = new Thread(() -> followLog(after), "console-log");
        logs.setDaemon(true);
        logs.start();
        out.println("Deck console" + (deck.isEmpty() ? "" : " (" + deck + ")")
                + ": type help, quit to leave. \"log on\" shows the Deck's log here.");
        try {
            String line;
            while ((line = in.readLine()) != null) {
                String text = line.strip();
                if (text.equals("quit") || text.equals("exit")) {
                    break;
                }
                if (!text.isEmpty()) {
                    send(text);
                }
            }
        } catch (IOException | IllegalStateException error) {
            out.println("console: " + error.getMessage());
            return 2;
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
        }
        logs.interrupt();
        return 0;
    }

    /** Sends a line and prints the answer as it comes; false when the Deck did not answer. */
    private boolean send(String line) throws IOException, InterruptedException {
        Map<String, Object> body = Map.of("deck", deck, "line", line, "wait_ms", (int) ANSWER_WAIT_MS);
        Map<String, Object> data = call(HttpRequest.newBuilder(URI.create(base))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(Json.encode(body))));
        String text = String.valueOf(data.getOrDefault("text", ""));
        if (!text.isEmpty()) {
            out.print(text.endsWith("\n") ? text : text + "\n");
        }
        if (!Boolean.TRUE.equals(data.get("done"))) {
            out.println("(no answer from the Deck in " + ANSWER_WAIT_MS / 1000 + " s: is it connected?)");
            return false;
        }
        return true;
    }

    private long lastLogSeq() throws IOException, InterruptedException {
        Map<String, Object> data = call(HttpRequest.newBuilder(URI.create(base + "/log?after=" + Integer.MAX_VALUE)).GET());
        return data.get("last") instanceof Number n ? n.longValue() : 0;
    }

    private void followLog(long[] after) {
        String deckQuery = deck.isEmpty() ? "" : "&deck=" + URLEncoder.encode(deck, StandardCharsets.UTF_8);
        while (!Thread.currentThread().isInterrupted()) {
            try {
                Thread.sleep(LOG_POLL_MS);
                Map<String, Object> data = call(HttpRequest.newBuilder(
                        URI.create(base + "/log?after=" + after[0] + deckQuery)).GET());
                if (data.get("lines") instanceof List<?> lines) {
                    for (Object item : lines) {
                        Map<?, ?> line = (Map<?, ?>) item;
                        out.println("[log " + line.get("deck") + "] " + line.get("text"));
                        if (line.get("seq") instanceof Number seq) {
                            after[0] = Math.max(after[0], seq.longValue());
                        }
                    }
                }
            } catch (InterruptedException stopped) {
                return;
            } catch (IOException | RuntimeException error) {
                // the server went away for a moment: the next poll tries again
            }
        }
    }

    /** The "data" of an API answer; IllegalStateException with the server's reason otherwise. */
    private Map<String, Object> call(HttpRequest.Builder request) throws IOException, InterruptedException {
        HttpResponse<String> answer;
        try {
            answer = http.send(request.timeout(Duration.ofMillis(ANSWER_WAIT_MS + 5_000)).build(),
                    HttpResponse.BodyHandlers.ofString());
        } catch (java.net.ConnectException refused) {
            throw new IllegalStateException("no server on " + base + " (start it with ./start.sh server)");
        }
        Map<String, Object> body = Json.parseObject(answer.body());
        if (answer.statusCode() != 200 || body == null || !(body.get("data") instanceof Map<?, ?> data)) {
            Object why = body == null ? answer.body() : body.getOrDefault("error", body.get("message"));
            throw new IllegalStateException("the server refused (" + answer.statusCode() + "): " + why);
        }
        @SuppressWarnings("unchecked")
        Map<String, Object> result = (Map<String, Object>) data;
        return result;
    }

    /** Entry point from Main: {@code --console [--deck ID] [--port N] [line...]}. */
    public static int run(List<String> args, int port) {
        String deck = "";
        StringBuilder line = new StringBuilder();
        for (int i = 0; i < args.size(); i++) {
            String arg = args.get(i);
            if (arg.equals("--deck") && i + 1 < args.size()) {
                deck = args.get(++i);
            } else if (arg.equals("--port") && i + 1 < args.size()) {
                i++;  // read by Main
            } else if (!arg.equals("--console")) {
                line.append(line.length() == 0 ? "" : " ").append(arg);
            }
        }
        ConsoleClient client = new ConsoleClient(port, deck, System.out);
        if (line.length() > 0) {
            return client.once(line.toString());
        }
        return client.interactive(new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8)));
    }
}
