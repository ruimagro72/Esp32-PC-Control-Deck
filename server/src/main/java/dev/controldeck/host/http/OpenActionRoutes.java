package dev.controldeck.host.http;

import dev.controldeck.host.Services;
import dev.controldeck.host.protocol.MessageHandler;
import dev.controldeck.host.transport.openaction.OpenActionClient;

import java.util.Map;

/** OpenDeck plugin hand-over (host only). */
final class OpenActionRoutes {

    private final Services services;

    OpenActionRoutes(Services services) {
        this.services = services;
    }

    /** OpenDeck started the plugin, which found this host running: the host takes over. */
    Response attach(Map<String, Object> body) {
        if (!(body.get("port") instanceof Number port) || !(body.get("register_event") instanceof String event)
                || !(body.get("uuid") instanceof String uuid)) {
            throw HttpError.badRequest("Send port, register_event and uuid");
        }
        OpenActionClient.connect(services.messages().openAction(), port.intValue(), event, uuid);
        return status();
    }

    Response status() {
        return Response.ok(MessageHandler.map("success", true, "attached", services.messages().openAction().attached()));
    }
}
