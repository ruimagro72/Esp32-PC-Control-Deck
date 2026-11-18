package dev.controldeck.host.http;

import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Keeps web pages away from the API.
 *
 * The host-only rules trust the loopback address, and a browser on this computer is on
 * the loopback address too. Without these checks any page open in the browser could
 * send {@code POST /api/macros} with a {@code text/plain} body (no preflight needed),
 * or rebind its own domain name to 127.0.0.1, and reach the host-only routes.
 *
 *  - Host: an IP address or "localhost" only. DNS rebinding needs a domain name, so
 *    it is refused; the board and curl use the address.
 *  - Origin / Sec-Fetch-Site: a request made by a page from another site is refused.
 *    Browsers send these headers; the board and command-line tools do not.
 *  - Content-Type: a request with a body must say application/json. A page can only
 *    send that after a CORS preflight, which this API never grants.
 */
public final class RequestGuard {

    private static final Pattern IPV4 = Pattern.compile("^\\d{1,3}(\\.\\d{1,3}){3}$");
    private static final Pattern IPV6 = Pattern.compile("^\\[[0-9a-fA-F:.%]+\\]$");

    private RequestGuard() {
    }

    /** Before anything else. @throws HttpError 403 for a foreign host or a web page */
    public static void checkSource(Request request) {
        String host = request.header("host");
        if (host != null && !allowedHost(host)) {
            throw HttpError.forbidden("Use the host's IP address or localhost to reach this API");
        }

        String site = request.header("sec-fetch-site");
        if (site != null && !site.equalsIgnoreCase("same-origin") && !site.equalsIgnoreCase("none")) {
            throw HttpError.forbidden("Requests from web pages are not accepted");
        }
        String origin = request.header("origin");
        if (origin != null && (host == null || !(origin.equalsIgnoreCase("http://" + host)
                || origin.equalsIgnoreCase("https://" + host)))) {
            throw HttpError.forbidden("Requests from web pages are not accepted");
        }
    }

    /**
     * After the access rules, so a peer that may not call the route learns nothing more.
     * @throws HttpError 415 for a request body that is not declared as JSON
     */
    public static void checkBody(Request request) {
        boolean writes = switch (request.method()) {
            case "POST", "PUT", "PATCH", "DELETE" -> true;
            default -> false;
        };
        if (writes && !request.body().isBlank()) {
            String type = request.header("content-type");
            // Both need a CORS preflight from a web page, which this server never answers.
            String lower = type == null ? "" : type.toLowerCase(Locale.ROOT);
            // text/plain only on /api/deck: base64 frames from KryonOS, whose HTTP is text only;
            // the Bearer token there still forces a preflight on a web page.
            boolean deckText = lower.startsWith("text/plain") && "/api/deck".equals(request.path());
            if (!deckText && !lower.startsWith("application/json") && !lower.startsWith("application/octet-stream")) {
                throw new HttpError(415, "Send the body with Content-Type: application/json "
                        + "(a Deck: application/octet-stream)");
            }
        }
    }

    /** "127.0.0.1:8080", "10.0.1.2", "[::1]:8080" and "localhost" pass; domain names do not. */
    static boolean allowedHost(String header) {
        String host = header.trim();
        if (host.startsWith("[")) {
            int end = host.indexOf(']');
            if (end < 0) {
                return false;
            }
            String rest = host.substring(end + 1);
            if (!rest.isEmpty() && !rest.matches("^:\\d{1,5}$")) {
                return false;
            }
            return IPV6.matcher(host.substring(0, end + 1)).matches();
        }
        int colon = host.indexOf(':');
        if (colon >= 0) {
            if (!host.substring(colon + 1).matches("^\\d{1,5}$")) {
                return false;
            }
            host = host.substring(0, colon);
        }
        return host.equalsIgnoreCase("localhost") || IPV4.matcher(host).matches();
    }
}
