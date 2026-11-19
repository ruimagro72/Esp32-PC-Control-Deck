package dev.controldeck.host.http;

import static dev.controldeck.host.http.RouteSupport.success;

import dev.controldeck.host.Services;
import dev.controldeck.host.pack.PackStore;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Per-Deck settings: language and pack (host only). */
final class DeckConfigRoutes {

    private final Services services;

    DeckConfigRoutes(Services services) {
        this.services = services;
    }

    /** The language of one Deck's texts, or of all ({"language": "pt"}); the Deck gets it at its next connection. */
    Response setLanguage(String deckId, Map<String, Object> body) {
        Object language = body == null ? null : body.get("language");
        services.config().setDeckLanguage(deckId, language instanceof String text ? text : null);
        return Response.ok(success(Map.of("language", language)));
    }

    /** The Decks' packs (docs/04-PROTOCOL.md, "The pack"): the packs there are, with their versions per board, and who has which. */
    Response packs() {
        PackStore store = services.messages().packStore();
        if (store == null) {
            throw HttpError.notFound("No packs");
        }
        Map<String, Object> versions = new LinkedHashMap<>();
        for (String name : store.names()) {
            Map<String, Object> perBoard = new LinkedHashMap<>();
            for (String board : List.of("es3c28p", "e32r35t")) {
                store.packNamed(name, board).ifPresent(p -> perBoard.put(board, Map.of("version", p.version(), "files", p.files(),
                        "bytes", p.bytes().length)));
            }
            versions.put(name, perBoard);
        }
        return Response.ok(success(Map.of("packs", versions, "decks", store.assignments(), "default", PackStore.DEFAULT)));
    }

    /** Gives a Deck a pack ({"pack": "night"}; null or "default" for the default); a connected Deck gets it within a minute. */
    Response setPack(String deckId, Map<String, Object> body) {
        PackStore store = services.messages().packStore();
        Object pack = body == null ? null : body.get("pack");
        try {
            store.assign(deckId, pack instanceof String text ? text : null);
        } catch (IllegalArgumentException e) {
            throw HttpError.badRequest(e.getMessage());
        }
        return Response.ok(success(Map.of("deck", deckId.toUpperCase(), "pack", store.assigned(deckId.toUpperCase()))));
    }
}
