package dev.controldeck.host.http;

/** An error that maps to an HTTP status. Messages are safe to show to the client. */
public final class HttpError extends RuntimeException {

    private static final long serialVersionUID = 1L;

    private final int status;

    public HttpError(int status, String message) {
        super(message);
        this.status = status;
    }

    public int status() {
        return status;
    }

    public static HttpError badRequest(String message) {
        return new HttpError(400, message);
    }

    public static HttpError unauthorized(String message) {
        return new HttpError(401, message);
    }

    public static HttpError forbidden(String message) {
        return new HttpError(403, message);
    }

    public static HttpError notFound(String message) {
        return new HttpError(404, message);
    }

    public static HttpError tooManyRequests(String message) {
        return new HttpError(429, message);
    }
}
