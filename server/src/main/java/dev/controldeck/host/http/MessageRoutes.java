package dev.controldeck.host.http;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.MessageHandler;

import java.util.Map;

/** The inbox: leave, read audio, delete (host only). */
final class MessageRoutes {

    private final Services services;

    MessageRoutes(Services services) {
        this.services = services;
    }

    /**
     * POST /api/messages {"text": "...", "to": "<Deck id>" or "all", "from": "..."}; a voice
     * message: {"kind": "voice", "file": "<a WAV on this PC: 16 kHz, 16-bit, mono>", "to", "from"}.
     */
    Response leave(Map<String, Object> body) {
        try {
            if ("voice".equals(body.get("kind"))) {
                if (!(body.get("file") instanceof String file) || file.isBlank()) {
                    throw new IllegalArgumentException("a voice message needs \"file\": a WAV on this PC");
                }
                byte[] wav;
                try {
                    wav = java.nio.file.Files.readAllBytes(java.nio.file.Path.of(file));
                } catch (java.io.IOException e) {
                    throw new IllegalArgumentException("cannot read " + file + ": " + e.getMessage());
                }
                var voice = services.messages().inbox().addVoice(
                        body.get("to") instanceof String to ? to : null,
                        body.get("from") instanceof String from ? from : null,
                        dev.controldeck.host.inbox.Voice.fromWav(wav));
                return Response.ok(MessageHandler.map("success", true, "id", voice.id(), "length", voice.text()));
            }
            var message = services.messages().inbox().add(
                    body.get("to") instanceof String to ? to : null,
                    body.get("from") instanceof String from ? from : null,
                    body.get("kind") instanceof String kind ? kind : "text",
                    body.get("text") instanceof String text ? text : null);
            return Response.ok(MessageHandler.map("success", true, "id", message.id()));
        } catch (IllegalArgumentException error) {
            throw HttpError.badRequest(error.getMessage());
        }
    }

    /** GET /api/messages/{id}/audio: a voice message as WAV. */
    Response audio(String id) {
        byte[] pcm = services.messages().inbox().audio(Long.parseLong(id));
        if (pcm == null) {
            throw HttpError.notFound("No audio for message " + id);
        }
        return new Response(200, dev.controldeck.host.inbox.Voice.toWav(pcm), Map.of("Content-Type", "audio/wav"));
    }

    Response delete(String id) {
        if (!services.messages().inbox().delete(Long.parseLong(id))) {
            throw HttpError.notFound("No message " + id);
        }
        return Response.ok(MessageHandler.map("success", true));
    }
}
