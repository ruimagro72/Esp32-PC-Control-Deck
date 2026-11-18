package dev.controldeck.host.console;

import dev.controldeck.host.util.Log;
import dev.controldeck.host.util.Values;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The Deck's console on the server (docs/CONSOLE.md): the backoffice channel beside the normal
 * connection. A line typed by the user waits here until the link to its Deck takes it
 * ({@link #take}: the serial session's tick, or the answer to the Deck's next Wi-Fi request);
 * the Deck's answer and its live log lines come back through {@link #output}.
 *
 * The user never talks to the Deck directly: the command-line client and, later, the web
 * admin reach this hub through the server's own HTTP API (loopback only).
 *
 * Thread safe: the serial threads, the HTTP workers and the client share it.
 */
public final class ConsoleHub {

    /** A line longer than this is refused (the frame's {@code str} holds 255 bytes). */
    public static final int MAX_LINE_BYTES = 255;
    /** Lines waiting for one Deck; more are refused, so a gone Deck cannot fill the memory. */
    static final int MAX_PENDING = 8;
    /** console_input frames handed to a link at once. */
    static final int TAKE_AT_ONCE = 4;
    /** Log lines kept per Deck for the clients that poll them. */
    static final int MAX_LOG_LINES = 500;
    /** Answers kept for a client that has not asked for them yet. */
    static final int MAX_ANSWERS = 64;
    /** Text kept per answer; the rest is cut (a Deck's answer is a few hundred bytes). */
    static final int MAX_ANSWER_CHARS = 16384;
    /** A line not taken by a link this long is dropped: its Deck is not connected. */
    static final long PENDING_MAX_AGE_MS = 30_000;

    /** Any Deck: the first link that asks takes the line. */
    public static final String ANY = "";

    /** flags of console_output: bit 0, the last part of the answer. */
    public static final int FLAG_LAST = 1;

    /** A line typed by the user, not yet taken by a link. */
    private record Pending(int id, String line, long at) {
    }

    /** One of the Deck's log lines, numbered for the clients that poll. */
    public record LogLine(long seq, String deck, long time, String text) {
    }

    /** What a Deck answered to one line; {@code done} once its last part came. */
    public record Answer(int id, String deck, boolean done, String text) {
    }

    private final Map<String, Deque<Pending>> pending = new LinkedHashMap<>();
    private final Map<Integer, StringBuilder> answers = new LinkedHashMap<>();
    private final Map<Integer, String> answerDeck = new LinkedHashMap<>();
    private final Map<Integer, Boolean> answerDone = new LinkedHashMap<>();
    private final Deque<LogLine> logs = new ArrayDeque<>();
    private long logSeq;
    private int nextId = 1;
    private final boolean printLogs;

    /** A hub that also prints the Decks' log lines on the server's console. */
    public ConsoleHub() {
        this(true);
    }

    public ConsoleHub(boolean printLogs) {
        this.printLogs = printLogs;
    }

    /**
     * Queue a line for a Deck ({@link #ANY}: the first that asks).
     *
     * @return the request id its answer will carry
     * @throws IllegalArgumentException for an empty or too long line, a line with control
     *                                  characters, or too many lines waiting
     */
    public synchronized int submit(String deck, String line) {
        String target = deck == null ? ANY : deck.trim();
        if (!target.isEmpty() && !target.matches("^[0-9A-Za-z:_-]{1,32}$")) {
            throw new IllegalArgumentException("deck must be a Deck id");
        }
        String text = line == null ? "" : line.strip();
        if (text.isEmpty()) {
            throw new IllegalArgumentException("line is empty");
        }
        if (text.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > MAX_LINE_BYTES) {
            throw new IllegalArgumentException("line is longer than " + MAX_LINE_BYTES + " bytes");
        }
        if (text.chars().anyMatch(c -> c < 32 || c == 127)) {
            throw new IllegalArgumentException("line has control characters");
        }
        long now = System.currentTimeMillis();
        Deque<Pending> queue = pending.computeIfAbsent(target, key -> new ArrayDeque<>());
        queue.removeIf(p -> now - p.at() > PENDING_MAX_AGE_MS);
        if (queue.size() >= MAX_PENDING) {
            throw new IllegalArgumentException("too many lines wait for this Deck");
        }
        int id = nextId;
        nextId = nextId >= 0xFFFF ? 1 : nextId + 1;  // a u16 on the link; 0 means a log line
        queue.add(new Pending(id, text, now));
        answers.put(id, new StringBuilder());
        answerDeck.put(id, target);
        answerDone.put(id, false);
        while (answers.size() > MAX_ANSWERS) {
            Integer oldest = answers.keySet().iterator().next();
            forget(oldest);
        }
        return id;
    }

    /**
     * The console_input frames waiting for this Deck (and for any Deck), a few at a time. A link
     * whose Deck has not said its id yet passes {@link #ANY} and takes only the lines for any Deck.
     */
    public synchronized List<Map<String, Object>> take(String deck) {
        List<Map<String, Object>> frames = new ArrayList<>();
        String own = deck == null ? ANY : deck;
        long now = System.currentTimeMillis();
        for (String key : own.isEmpty() ? List.of(ANY) : List.of(own, ANY)) {
            Deque<Pending> queue = pending.get(key);
            while (queue != null && !queue.isEmpty() && frames.size() < TAKE_AT_ONCE) {
                Pending p = queue.poll();
                if (now - p.at() > PENDING_MAX_AGE_MS) {
                    continue;
                }
                answerDeck.put(p.id(), deck);
                frames.add(Map.of("type", "console_input", "id", p.id(), "line", p.line()));
            }
        }
        return frames;
    }

    /** True when a line waits for this Deck (a Wi-Fi answer then carries it). */
    public synchronized boolean hasPending(String deck) {
        Deque<Pending> own = pending.get(deck);
        Deque<Pending> any = pending.get(ANY);
        return (own != null && !own.isEmpty()) || (any != null && !any.isEmpty());
    }

    /**
     * A console_output frame from a Deck: a part of an answer, or (id 0) a live log line.
     * Answers to ids the hub never gave are dropped.
     */
    public synchronized void output(String deck, int id, int flags, String text) {
        String from = deck == null ? "?" : Values.truncateCodePoints(deck, 32);
        String body = text == null ? "" : text;
        if (id == 0) {
            for (String line : body.split("\n", -1)) {
                if (line.isEmpty()) {
                    continue;
                }
                logs.add(new LogLine(++logSeq, from, System.currentTimeMillis(), line));
                if (printLogs) {
                    Log.info("[deck-log " + from + "] " + line);
                }
            }
            while (logs.size() > MAX_LOG_LINES) {
                logs.poll();
            }
            notifyAll();
            return;
        }
        StringBuilder answer = answers.get(id);
        if (answer == null) {
            return;
        }
        if (answer.length() + body.length() <= MAX_ANSWER_CHARS) {
            answer.append(body);
        }
        if ((flags & FLAG_LAST) != 0) {
            answerDone.put(id, true);
        }
        notifyAll();
    }

    /** The answer so far; waits up to {@code waitMs} for its last part. Null for an unknown id. */
    public synchronized Answer await(int id, long waitMs) throws InterruptedException {
        long end = System.currentTimeMillis() + Math.max(0, waitMs);
        while (answers.containsKey(id) && !Boolean.TRUE.equals(answerDone.get(id))) {
            long left = end - System.currentTimeMillis();
            if (left <= 0) {
                break;
            }
            wait(left);
        }
        StringBuilder answer = answers.get(id);
        if (answer == null) {
            return null;
        }
        boolean done = Boolean.TRUE.equals(answerDone.get(id));
        Answer result = new Answer(id, answerDeck.get(id), done, answer.toString());
        if (done) {
            forget(id);
        }
        return result;
    }

    /** The log lines after {@code after} (0: all kept), of one Deck or ({@link #ANY}) of all. */
    public synchronized List<LogLine> logs(String deck, long after) {
        List<LogLine> lines = new ArrayList<>();
        for (LogLine line : logs) {
            if (line.seq() > after && (deck == null || deck.isEmpty() || deck.equals(line.deck()))) {
                lines.add(line);
            }
        }
        return lines;
    }

    /** The number of the newest log line (0: none yet). */
    public synchronized long lastLogSeq() {
        return logSeq;
    }

    private void forget(Integer id) {
        answers.remove(id);
        answerDeck.remove(id);
        answerDone.remove(id);
    }
}
