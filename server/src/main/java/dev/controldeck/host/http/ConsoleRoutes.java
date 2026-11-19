package dev.controldeck.host.http;

import static dev.controldeck.host.http.RouteSupport.intParam;
import static dev.controldeck.host.http.RouteSupport.success;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** The Decks' consoles (docs/CONSOLE.md): host only, like the macro definitions. */
final class ConsoleRoutes {

    /** The longest a console request waits for the Deck's answer (a Wi-Fi Deck polls every 1-2 s). */
    static final int CONSOLE_WAIT_MAX_MS = 10_000;
    private static final int CONSOLE_WAIT_DEFAULT_MS = 3_000;

    private final Services services;

    ConsoleRoutes(Services services) {
        this.services = services;
    }

    /** POST /api/console {"line": "info", "deck": "<id>" (optional), "wait_ms": 3000}: one line for a Deck. */
    Response line(Map<String, Object> body) {
        String deck = body.get("deck") instanceof String d ? d : "";
        String line = body.get("line") instanceof String l ? l : "";
        Long wait = Values.validateInt(body.getOrDefault("wait_ms", CONSOLE_WAIT_DEFAULT_MS));
        if (wait == null || wait < 0 || wait > CONSOLE_WAIT_MAX_MS) {
            throw HttpError.badRequest("wait_ms must be an integer between 0 and " + CONSOLE_WAIT_MAX_MS);
        }
        int id = services.messages().console().submit(deck, line);
        Log.info("[console] " + (deck.isEmpty() ? "any Deck" : Values.truncateCodePoints(deck, 32)) + " > "
                + Values.truncateCodePoints(withoutSecret(line.strip()), 80));
        return answer(id, wait.intValue());
    }

    /** A line for the server's log: what follows login and passwd is a password, never written. */
    static String withoutSecret(String line) {
        String word = line.split("\\s+", 2)[0];
        return (word.equals("login") || word.equals("passwd")) && !line.equals(word) ? word + " ***" : line;
    }

    /** GET /api/console/<id>?wait_ms=N: the answer to a line so far, or all of it once done. */
    Response answer(int id, int waitMs) {
        var answer = awaitConsole(id, waitMs);
        if (answer == null) {
            throw HttpError.notFound("No console request " + id);
        }
        return Response.ok(success(MessageHandler.map("id", answer.id(), "deck", answer.deck(), "done", answer.done(),
                "text", answer.text())));
    }

    private dev.controldeck.host.console.ConsoleHub.Answer awaitConsole(int id, int waitMs) {
        try {
            return services.messages().console().await(id, waitMs);
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
            throw new HttpError(503, "The server is stopping");
        }
    }

    /** GET /api/console/log?deck=<id>&after=N: the Decks' live log lines after number N. */
    Response log(Map<String, String> query) {
        long after = intParam(query, "after", 0, 0, Integer.MAX_VALUE);
        String deck = query.getOrDefault("deck", "");
        var console = services.messages().console();
        List<Map<String, Object>> lines = new ArrayList<>();
        for (var line : console.logs(deck, after)) {
            lines.add(MessageHandler.map("seq", line.seq(), "deck", line.deck(), "time", line.time(), "text", line.text()));
        }
        return Response.ok(success(MessageHandler.map("last", console.lastLogSeq(), "lines", lines)));
    }
}
