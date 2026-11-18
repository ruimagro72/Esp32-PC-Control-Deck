package dev.controldeck.host.config;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Where things live on disk. The host directory is the working directory unless
 * CONTROL_DECK_HOST_DIR says otherwise. Runtime data (the database, sampling state,
 * transport state) goes to var/ under it, or to CONTROL_DECK_VAR. The database file
 * itself can be moved with CONTROL_DECK_DB.
 *
 * There is no configuration file: the settings live in the database.
 */
public final class HostPaths {

    /** File name of the database inside the runtime directory. */
    public static final String DATABASE_NAME = "control-deck.db";

    private HostPaths() {
    }

    public static Path hostDir() {
        String override = System.getenv("CONTROL_DECK_HOST_DIR");
        return (override != null && !override.isBlank())
                ? Path.of(override).toAbsolutePath().normalize()
                : Path.of("").toAbsolutePath().normalize();
    }

    /** The SQLite database: configuration and telemetry history. Created on first use. */
    public static Path databaseFile() {
        String override = System.getenv("CONTROL_DECK_DB");
        return (override != null && !override.isBlank())
                ? Path.of(override).toAbsolutePath().normalize()
                : varDir().resolve(DATABASE_NAME);
    }

    /** Runtime data: database, sampling state, transport state, logs. Created on demand. */
    public static Path varDir() {
        String override = System.getenv("CONTROL_DECK_VAR");
        Path dir = (override != null && !override.isBlank())
                ? Path.of(override)
                : hostDir().resolve("var");
        try {
            Files.createDirectories(dir);
        } catch (IOException e) {
            throw new IllegalStateException("Cannot create runtime directory " + dir, e);
        }
        return dir;
    }

    /** Resolve a configured path: absolute stays, "var/..." follows CONTROL_DECK_VAR, anything else is relative to the host directory. */
    public static Path resolve(String configured) {
        Path path = Path.of(configured);
        if (path.isAbsolute()) {
            return path;
        }
        if (configured.startsWith("var/")) {
            return varDir().resolve(configured.substring(4));
        }
        return hostDir().resolve(configured);
    }
}
