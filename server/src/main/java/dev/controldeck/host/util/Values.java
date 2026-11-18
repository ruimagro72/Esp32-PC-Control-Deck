package dev.controldeck.host.util;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.util.regex.Pattern;

/**
 * Lenient readers for values that arrive as JSON or as stored settings (a number may be
 * a string), and the rounding and text-length rules the API promises.
 */
public final class Values {

    private Values() {
    }

    private static final Pattern INT_PATTERN = Pattern.compile("^[+-]?(0|[1-9][0-9]*)$");

    /** Rounds half away from zero. NaN and infinities are returned unchanged. */
    public static double round(double value, int places) {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            return value;
        }
        return new BigDecimal(Double.toString(value)).setScale(places, RoundingMode.HALF_UP).doubleValue();
    }

    /**
     * The integer in {@code value}, or null. Accepts Integer/Long, and strings with an
     * optional sign and no leading zeros.
     */
    public static Long validateInt(Object value) {
        if (value instanceof Long l) {
            return l;
        }
        if (value instanceof Integer i) {
            return i.longValue();
        }
        if (value instanceof String s) {
            String trimmed = s.trim();
            if (!INT_PATTERN.matcher(trimmed).matches()) {
                return null;
            }
            try {
                return Long.parseLong(trimmed.startsWith("+") ? trimmed.substring(1) : trimmed);
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return null;
    }

    /** is_numeric() for values coming out of JSON. */
    public static boolean isNumeric(Object value) {
        if (value instanceof Number) {
            return true;
        }
        if (value instanceof String s) {
            try {
                Double.parseDouble(s.trim());
                return !s.isBlank();
            } catch (NumberFormatException e) {
                return false;
            }
        }
        return false;
    }

    /** mb_substr($text, 0, $max): truncates by code points, not UTF-16 units. */
    public static String truncateCodePoints(String text, int max) {
        if (text == null) {
            return "";
        }
        int count = text.codePointCount(0, text.length());
        if (count <= max) {
            return text;
        }
        return text.substring(0, text.offsetByCodePoints(0, max));
    }

    /** mb_strlen(). */
    public static int codePointLength(String text) {
        return text.codePointCount(0, text.length());
    }
}
