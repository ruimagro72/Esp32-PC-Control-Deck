package dev.controldeck.host.config;

import dev.controldeck.host.util.Json;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermission;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * Persists the configuration in the SQLite database: table {@code config(section
 * TEXT PRIMARY KEY, json TEXT NOT NULL)}, one row per top-level section
 * ("api", "telemetry", "transports", ...), the value stored as JSON.
 *
 * The connection is opened on first use, so building a store never touches the
 * disk. The file is shared with the telemetry history (another table); only the
 * {@code config} table is read or written here.
 */
final class ConfigStore {

    private final Path file;
    private Connection connection;

    ConfigStore(Path file) {
        this.file = file;
    }

    Path file() {
        return file;
    }

    /** True when no configuration has been stored yet. */
    synchronized boolean isEmpty() {
        try (Statement stmt = db().createStatement(); ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM config")) {
            return !rs.next() || rs.getLong(1) == 0;
        } catch (SQLException e) {
            throw failure("read", e);
        }
    }

    /** Every stored section in insertion order; empty when nothing is stored. */
    synchronized Map<String, Object> read() {
        Map<String, Object> sections = new LinkedHashMap<>();
        try (Statement stmt = db().createStatement();
             ResultSet rs = stmt.executeQuery("SELECT section, json FROM config ORDER BY rowid")) {
            while (rs.next()) {
                String section = rs.getString(1);
                try {
                    sections.put(section, Json.parseValue(rs.getString(2)));
                } catch (IllegalArgumentException e) {
                    throw new IllegalStateException("The stored configuration section '" + section
                            + "' is not valid JSON; run with --setup to rebuild it from the defaults.", e);
                }
            }
        } catch (SQLException e) {
            throw failure("read", e);
        }
        return sections;
    }

    /** Replace the whole configuration in one transaction. */
    synchronized void write(Map<String, Object> config) {
        Connection db;
        try {
            db = db();
        } catch (SQLException e) {
            throw failure("write", e);
        }
        try (PreparedStatement delete = db.prepareStatement("DELETE FROM config");
             PreparedStatement insert = db.prepareStatement("INSERT INTO config(section, json) VALUES (?, ?)")) {
            db.setAutoCommit(false);
            try {
                delete.executeUpdate();
                for (Map.Entry<String, Object> entry : config.entrySet()) {
                    insert.setString(1, entry.getKey());
                    insert.setString(2, Json.encode(entry.getValue()));
                    insert.executeUpdate();
                }
                db.commit();
            } catch (SQLException | RuntimeException e) {
                db.rollback();
                throw e;
            } finally {
                db.setAutoCommit(true);
            }
        } catch (SQLException e) {
            throw failure("write", e);
        }
    }

    synchronized void close() {
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException ignored) {
                // Nothing left to report for a connection that is already gone.
            }
            connection = null;
        }
    }

    private Connection db() throws SQLException {
        if (connection != null) {
            return connection;
        }
        Path dir = file.toAbsolutePath().getParent();
        try {
            Files.createDirectories(dir);
        } catch (IOException e) {
            throw new IllegalStateException("Cannot create the database folder " + dir, e);
        }
        Connection db = DriverManager.getConnection("jdbc:sqlite:" + file.toAbsolutePath());
        try (Statement stmt = db.createStatement()) {
            stmt.execute("PRAGMA busy_timeout = 5000");
            stmt.execute("PRAGMA journal_mode = WAL");
            stmt.execute("CREATE TABLE IF NOT EXISTS config (section TEXT PRIMARY KEY, json TEXT NOT NULL)");
        } catch (SQLException e) {
            db.close();
            throw e;
        }
        restrictPermissions(file);
        connection = db;
        return db;
    }

    private static void restrictPermissions(Path path) {
        try {
            // The database holds the Wi-Fi token hash and the macro definitions: owner only.
            Files.setPosixFilePermissions(path, Set.of(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE));
        } catch (IOException | UnsupportedOperationException ignored) {
            // Non-POSIX file systems keep their defaults.
        }
    }

    private IllegalStateException failure(String action, SQLException cause) {
        return new IllegalStateException("Cannot " + action + " the configuration database " + file + ": "
                + cause.getMessage(), cause);
    }
}
