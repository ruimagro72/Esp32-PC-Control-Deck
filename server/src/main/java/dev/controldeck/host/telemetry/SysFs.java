package dev.controldeck.host.telemetry;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.regex.Pattern;

import dev.controldeck.host.util.Values;

/**
 * Read-only access to /proc and /sys. Every path is absolute ("/proc/stat") and
 * is prefixed with a root, which is empty on a real host and points at a fixture
 * tree in the self-test. Unreadable files are simply absent: the callers turn
 * that into null, never into a guess.
 */
final class SysFs {

    private final String root;

    SysFs(String root) {
        this.root = root == null ? "" : (root.endsWith("/") ? root.substring(0, root.length() - 1) : root);
    }

    String root() {
        return root;
    }

    /** Absolute location of a path under the root. */
    String absolute(String path) {
        return root + path;
    }

    Optional<String> read(String path) {
        try {
            return Optional.of(Files.readString(Paths.get(absolute(path)), StandardCharsets.UTF_8));
        } catch (IOException | InvalidPathException | SecurityException e) {
            return Optional.empty();
        }
    }

    /** Trimmed content, or an empty string when the file is missing. */
    boolean exists(String path) {
        try {
            return Files.exists(Paths.get(absolute(path)));
        } catch (InvalidPathException | SecurityException e) {
            return false;
        }
    }

    String trimmed(String path) {
        return read(path).map(String::trim).orElse("");
    }

    List<String> lines(String path) {
        return read(path).map(text -> List.of(text.split("\n", -1))).map(list -> {
            List<String> copy = new ArrayList<>(list);
            if (!copy.isEmpty() && copy.get(copy.size() - 1).isEmpty()) {
                copy.remove(copy.size() - 1);
            }
            return copy;
        }).orElse(List.of());
    }

    /** A non-negative number written alone in a sysfs file, or empty. */
    Optional<Double> numeric(String path) {
        return parseNumber(path, "^\\d+(\\.\\d+)?$");
    }

    /** Like numeric() but also accepts a leading minus sign. */
    Optional<Double> signedNumeric(String path) {
        return parseNumber(path, "^-?\\d+(\\.\\d+)?$");
    }

    private Optional<Double> parseNumber(String path, String accepted) {
        return read(path).flatMap(text -> {
            String value = text.trim();
            if (!value.matches(accepted)) {
                return Optional.empty();
            }
            return Optional.of(Double.parseDouble(value));
        });
    }

    /** Millidegrees to degrees with one decimal. Values outside 0..150 °C are rejected. */
    Optional<Double> milliCelsius(String path) {
        return numeric(path).filter(v -> v > 0 && v <= 150000)
                .map(v -> Values.round(v / 1000.0, 1));
    }

    /**
     * Expand a glob whose wildcards may appear in any segment, returning sorted
     * absolute paths that include the root prefix. Only "*" and "?" are supported.
     */
    List<String> glob(String pattern) {
        String[] segments = pattern.split("/");
        List<Path> current = new ArrayList<>();
        current.add(Paths.get("/"));
        for (String segment : segments) {
            if (segment.isEmpty()) {
                continue;
            }
            List<Path> next = new ArrayList<>();
            if (segment.indexOf('*') < 0 && segment.indexOf('?') < 0) {
                for (Path base : current) {
                    next.add(base.resolve(segment));
                }
            } else {
                Pattern matcher = globToRegex(segment);
                for (Path base : current) {
                    try (DirectoryStream<Path> children = Files.newDirectoryStream(base)) {
                        for (Path child : children) {
                            if (matcher.matcher(child.getFileName().toString()).matches()) {
                                next.add(child);
                            }
                        }
                    } catch (IOException | SecurityException e) {
                        // Missing directory: no matches from this branch.
                    }
                }
            }
            current = next;
        }
        List<String> result = new ArrayList<>();
        for (Path path : current) {
            if (Files.exists(path)) {
                result.add(path.toString());
            }
        }
        Collections.sort(result);
        return result;
    }

    /** Glob with the root prefix removed again: "/sys/..." for use with read(). */
    List<String> globRelative(String pattern) {
        List<String> absolute = glob(absolute(pattern));
        List<String> relative = new ArrayList<>(absolute.size());
        for (String path : absolute) {
            relative.add(path.substring(root.length()));
        }
        return relative;
    }

    static Pattern globToRegex(String segment) {
        StringBuilder regex = new StringBuilder();
        for (char c : segment.toCharArray()) {
            switch (c) {
                case '*' -> regex.append(".*");
                case '?' -> regex.append('.');
                default -> regex.append(Pattern.quote(String.valueOf(c)));
            }
        }
        return Pattern.compile(regex.toString());
    }
}
