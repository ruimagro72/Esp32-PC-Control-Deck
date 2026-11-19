package dev.controldeck.host.http;

import dev.controldeck.host.display.VirtualMonitor;

import java.util.Map;

/** Virtual monitor for the Deck's VNC app (krfb-virtualmonitor + the Java WsTcpBridge), token required. */
final class VirtualMonitorRoutes {

    private final VirtualMonitor monitor;

    VirtualMonitorRoutes(VirtualMonitor monitor) {
        this.monitor = monitor;
    }

    Response start(Map<String, Object> body) {
        try {
            return Response.ok(monitor.start(String.valueOf(body.getOrDefault("resolution", "320x240"))));
        } catch (IllegalStateException e) {
            return Response.error(503, e.getMessage());
        }
    }

    Response info() {
        return Response.ok(monitor.info());
    }

    Response stop() {
        return Response.ok(monitor.stop());
    }
}
