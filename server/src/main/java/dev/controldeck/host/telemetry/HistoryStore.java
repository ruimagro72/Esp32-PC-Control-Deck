package dev.controldeck.host.telemetry;

import dev.controldeck.host.util.Values;

import java.nio.file.Files;
import java.nio.file.Path;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Telemetry history in SQLite, in the host's database: {@code samples(t REAL PRIMARY
 * KEY, <one nullable REAL column per stats field>)}. A NULL is an unavailable reading and becomes a gap, never a zero.
 *
 * Bucketing runs in SQL. Bounds are bound as doubles through JDBC, never written into
 * the SQL as text (epoch seconds printed with too few digits would shift the buckets).
 *
 * The driver is sqlite-jdbc, loaded through DriverManager at run time. Unavailable
 * history reads as an empty window, not as an error.
 */
public final class HistoryStore {

    private static final double MIN_SAMPLE_SPACING = 0.4;
    private static final double PRUNE_EVERY_SECONDS = 60;
    private static final int MAX_POINTS = 240;
    /**
     * A silence longer than this between two samples means the host was not running. The
     * graph then starts after it: samples from an earlier run would otherwise show up as a
     * separate island, far from the current data. Shorter pauses stay as gaps (null).
     */
    static final double SESSION_GAP_SECONDS = 180;

    /** A series answer. */
    public record Series(String metric, String label, String unit, String component, int minutes, int points,
                         double stepSeconds, long start, long end, int samplesInWindow, Double latest,
                         List<Double> values) {

        public Map<String, Object> toMap() {
            Map<String, Object> map = new LinkedHashMap<>();
            map.put("metric", metric);
            map.put("label", label);
            map.put("unit", unit);
            map.put("component", component);
            map.put("minutes", minutes);
            map.put("points", points);
            map.put("step_seconds", stepSeconds);
            map.put("start", start);
            map.put("end", end);
            map.put("samples_in_window", samplesInWindow);
            map.put("latest", latest);
            map.put("values", values);
            return map;
        }
    }

    private final Path file;
    private final int retentionSeconds;
    private Connection connection;
    private double lastPrune;

    public HistoryStore(Path file, int retentionMinutes) {
        this.file = file;
        this.retentionSeconds = Math.max(20, retentionMinutes) * 60;
    }

    public Path path() {
        return file;
    }

    public int retentionSeconds() {
        return retentionSeconds;
    }

    /** Store the registry fields of a stats frame. False when throttled or unavailable. */
    public synchronized boolean append(double time, Map<String, Object> frame) {
        try {
            Connection db = db();
            Double last = scalarDouble(db, "SELECT MAX(t) FROM samples");
            if (last != null && time >= last && time - last < MIN_SAMPLE_SPACING) {
                return false;
            }

            List<String> fields = fields();
            StringBuilder columns = new StringBuilder();
            StringBuilder marks = new StringBuilder();
            for (String field : fields) {
                columns.append(",\"").append(field).append('"');
                marks.append(",?");
            }
            String sql = "INSERT OR REPLACE INTO samples (t" + columns + ") VALUES (?" + marks + ")";
            try (PreparedStatement stmt = db.prepareStatement(sql)) {
                stmt.setDouble(1, Values.round(time, 2));
                for (int i = 0; i < fields.size(); i++) {
                    Object value = frame.get(fields.get(i));
                    if (value instanceof Number n) {
                        stmt.setDouble(i + 2, n.doubleValue());
                    } else {
                        stmt.setNull(i + 2, java.sql.Types.REAL);
                    }
                }
                stmt.executeUpdate();
            }

            if (time - lastPrune >= PRUNE_EVERY_SECONDS) {
                lastPrune = time;
                try (PreparedStatement del = db.prepareStatement("DELETE FROM samples WHERE t < ?")) {
                    del.setDouble(1, time - retentionSeconds);
                    del.executeUpdate();
                }
            }
            return true;
        } catch (SQLException | IllegalStateException e) {
            return false;
        }
    }

    /**
     * History of one metric over the latest {@code minutes}, resampled into
     * {@code points} equal buckets (mean of the non-null samples in a bucket; a bucket
     * without a measurement is null).
     *
     * @throws IllegalArgumentException for an unknown metric or out-of-range arguments
     */
    public synchronized Series series(String metricId, int minutes, int points, Double nowOverride) {
        MetricRegistry.Metric metric = MetricRegistry.get(metricId)
                .orElseThrow(() -> new IllegalArgumentException("Unknown metric '" + metricId + "'"));
        if (minutes < 1 || minutes > retentionSeconds / 60) {
            throw new IllegalArgumentException("minutes must be between 1 and " + (retentionSeconds / 60));
        }
        if (points < 2 || points > MAX_POINTS) {
            throw new IllegalArgumentException("points must be between 2 and " + MAX_POINTS);
        }

        double now = nowOverride != null ? nowOverride : HostMetrics.nowSeconds();
        double span = minutes * 60.0;
        double start = now - span;
        double step = span / points;

        Aggregate aggregate = aggregate(metric.field(), start, now, points, step);
        return new Series(
                metric.id(),
                metric.label(),
                metric.unit(),
                metric.component(),
                minutes,
                points,
                Values.round(step, 2),
                (long) Math.floor(start),
                (long) Math.floor(now),
                aggregate.received(),
                aggregate.latest(),
                aggregate.values());
    }

    private record Aggregate(List<Double> values, int received, Double latest) {
    }

    /** One row per bucket, reading only the metric's column. */
    private Aggregate aggregate(String field, double start, double now, int points, double step) {
        List<Double> values = new ArrayList<>(points);
        for (int i = 0; i < points; i++) {
            values.add(null);
        }
        int received = 0;
        Double latest = null;
        String column = "\"" + field + "\"";

        try {
            Connection db = db();
            double from = currentRunStart(db, start, now);
            String bucketSql = "SELECT MIN(" + (points - 1) + ", CAST((t - ?) / ? AS INTEGER)) AS bucket, "
                    + "AVG(" + column + ") AS mean, COUNT(*) AS rows_in "
                    + "FROM samples WHERE t >= ? AND t <= ? GROUP BY bucket";
            try (PreparedStatement stmt = db.prepareStatement(bucketSql)) {
                stmt.setDouble(1, start);
                stmt.setDouble(2, step);
                stmt.setDouble(3, from);
                stmt.setDouble(4, now);
                try (ResultSet rs = stmt.executeQuery()) {
                    while (rs.next()) {
                        received += rs.getInt("rows_in");
                        double mean = rs.getDouble("mean");
                        if (!rs.wasNull()) {
                            // Small rates (a few bytes per second) keep their digits; the rest 2 decimals.
                            values.set(rs.getInt("bucket"), Values.round(mean, Math.abs(mean) < 1.0 ? 6 : 2));
                        }
                    }
                }
            }

            String newestSql = "SELECT " + column + " FROM samples WHERE t >= ? AND t <= ? AND "
                    + column + " IS NOT NULL ORDER BY t DESC LIMIT 1";
            try (PreparedStatement stmt = db.prepareStatement(newestSql)) {
                stmt.setDouble(1, from);
                stmt.setDouble(2, now);
                try (ResultSet rs = stmt.executeQuery()) {
                    if (rs.next()) {
                        latest = rs.getDouble(1);
                    }
                }
            }
        } catch (SQLException | IllegalStateException e) {
            // Unavailable history reads as an empty window.
        }
        return new Aggregate(values, received, latest);
    }

    /**
     * Start of the newest stretch of samples without a silence longer than
     * {@link #SESSION_GAP_SECONDS} (the current run of the host), or {@code start} when the
     * whole window is one stretch.
     */
    private static double currentRunStart(Connection db, double start, double now) throws SQLException {
        try (PreparedStatement stmt = db.prepareStatement(
                "SELECT t FROM samples WHERE t >= ? AND t <= ? ORDER BY t DESC")) {
            stmt.setDouble(1, start);
            stmt.setDouble(2, now);
            try (ResultSet rs = stmt.executeQuery()) {
                Double newer = null;
                while (rs.next()) {
                    double t = rs.getDouble(1);
                    if (newer != null && newer - t > SESSION_GAP_SECONDS) {
                        return newer;
                    }
                    newer = t;
                }
            }
        }
        return start;
    }

    /** True when at least one non-null sample of the metric exists in the retained window. */
    public synchronized boolean hasData(String metricId) {
        String field = MetricRegistry.fieldOf(metricId);
        if (field == null) {
            return false;
        }
        try (PreparedStatement stmt = db().prepareStatement(
                "SELECT 1 FROM samples WHERE t >= ? AND \"" + field + "\" IS NOT NULL LIMIT 1")) {
            stmt.setDouble(1, HostMetrics.nowSeconds() - retentionSeconds);
            try (ResultSet rs = stmt.executeQuery()) {
                return rs.next();
            }
        } catch (SQLException | IllegalStateException e) {
            return false;
        }
    }

    /**
     * Truncate the history: every stored sample is removed and the write-ahead log
     * is folded back so the file shrinks. Only the samples table is touched, so the
     * rest of the database (the configuration) is kept.
     *
     * @return how many samples were removed, 0 when the store is unavailable
     */
    public synchronized int clear() {
        try (Statement stmt = db().createStatement()) {
            int removed = stmt.executeUpdate("DELETE FROM samples");
            stmt.execute("PRAGMA wal_checkpoint(TRUNCATE)");
            return removed;
        } catch (SQLException | IllegalStateException e) {
            // Nothing to clear when the store is unavailable.
            return 0;
        }
    }

    /** Close the connection (service shutdown). */
    public synchronized void close() {
        if (connection != null) {
            try {
                connection.close();
            } catch (SQLException ignored) {
                // Closing an already failed connection has nothing left to report.
            }
            connection = null;
        }
    }

    private static Double scalarDouble(Connection db, String sql) throws SQLException {
        try (Statement stmt = db.createStatement(); ResultSet rs = stmt.executeQuery(sql)) {
            if (!rs.next()) {
                return null;
            }
            double value = rs.getDouble(1);
            return rs.wasNull() ? null : value;
        }
    }

    private static List<String> fields() {
        List<String> fields = new ArrayList<>();
        for (MetricRegistry.Metric metric : MetricRegistry.all()) {
            if (!fields.contains(metric.field())) {
                fields.add(metric.field());
            }
        }
        return fields;
    }

    private Connection db() throws SQLException {
        if (connection != null) {
            return connection;
        }
        Path dir = file.toAbsolutePath().getParent();
        try {
            Files.createDirectories(dir);
        } catch (java.io.IOException e) {
            throw new IllegalStateException("Cannot create " + dir, e);
        }

        Connection db = DriverManager.getConnection("jdbc:sqlite:" + file.toAbsolutePath());
        try (Statement stmt = db.createStatement()) {
            stmt.execute("PRAGMA busy_timeout = 3000");
            stmt.execute("PRAGMA journal_mode = WAL");
            stmt.execute("PRAGMA synchronous = NORMAL");
            stmt.execute("CREATE TABLE IF NOT EXISTS samples (t REAL PRIMARY KEY)");
        }

        // One nullable column per metric; a newer registry only adds columns.
        Set<String> have = new HashSet<>();
        try (Statement stmt = db.createStatement(); ResultSet rs = stmt.executeQuery("PRAGMA table_info(samples)")) {
            while (rs.next()) {
                have.add(rs.getString("name"));
            }
        }
        try (Statement stmt = db.createStatement()) {
            for (String field : fields()) {
                if (!have.contains(field)) {
                    stmt.execute("ALTER TABLE samples ADD COLUMN \"" + field + "\" REAL");
                }
            }
        }
        connection = db;
        return db;
    }
}
