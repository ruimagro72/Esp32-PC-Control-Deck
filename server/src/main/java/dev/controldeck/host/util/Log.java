package dev.controldeck.host.util;

import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

/** What the running host prints on its console: one line each, with the local date and time. */
public final class Log {

    private static final DateTimeFormatter STAMP = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    private Log() {
    }

    public static void info(String line) {
        System.out.println(stamped(line));
    }

    public static void error(String line) {
        System.err.println(stamped(line));
    }

    static String stamped(String line) {
        return STAMP.format(LocalDateTime.now()) + " " + line;
    }
}
