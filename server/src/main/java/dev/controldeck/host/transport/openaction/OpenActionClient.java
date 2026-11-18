package dev.controldeck.host.transport.openaction;

import dev.controldeck.host.util.Log;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

/** The WebSocket to OpenDeck, on this computer, for the {@link OpenActionBridge}. */
public final class OpenActionClient {

    private OpenActionClient() {
    }

    /** Connects and registers; the bridge detaches when the socket closes. */
    public static void connect(OpenActionBridge bridge, int port, String registerEvent, String uuid) {
        WebSocket socket = HttpClient.newHttpClient().newWebSocketBuilder()
                .connectTimeout(Duration.ofSeconds(5))
                .buildAsync(URI.create("ws://127.0.0.1:" + port), new WebSocket.Listener() {
                    private final StringBuilder text = new StringBuilder();

                    @Override
                    public CompletionStage<?> onText(WebSocket webSocket, CharSequence data, boolean last) {
                        text.append(data);
                        if (last) {
                            String message = text.toString();
                            text.setLength(0);
                            bridge.onMessage(message);
                        }
                        webSocket.request(1);
                        return null;
                    }

                    @Override
                    public CompletionStage<?> onClose(WebSocket webSocket, int status, String reason) {
                        lost(bridge);
                        return null;
                    }

                    @Override
                    public void onError(WebSocket webSocket, Throwable error) {
                        lost(bridge);
                    }
                }).join();
        // Sends go one at a time: a WebSocket refuses a send while another is in progress.
        final CompletableFuture<?>[] tail = {CompletableFuture.completedFuture(null)};
        bridge.attach(text -> {
            synchronized (tail) {
                tail[0] = tail[0].handle((ok, failed) -> null).thenCompose(ok -> socket.sendText(text, true));
            }
        }, registerEvent, uuid);
        Log.info("[openaction] Attached to OpenDeck on port " + port);
    }

    private static void lost(OpenActionBridge bridge) {
        bridge.detach();
        Log.info("[openaction] OpenDeck went away: the keys are the host's macros again");
    }
}
