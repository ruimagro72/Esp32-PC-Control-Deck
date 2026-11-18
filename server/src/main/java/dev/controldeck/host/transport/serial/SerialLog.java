package dev.controldeck.host.transport.serial;

import dev.controldeck.host.util.Log;

/** Where the serial bridge reports what it does. */
public interface SerialLog {

    void info(String message);

    void warn(String message);

    /** Only shown when CONTROL_DECK_DEBUG is set. */
    void debug(String message);

    /** Discards everything. */
    SerialLog NONE = new SerialLog() {
        @Override
        public void info(String message) {
        }

        @Override
        public void warn(String message) {
        }

        @Override
        public void debug(String message) {
        }
    };

    /** Standard output for information, standard error for warnings. */
    static SerialLog console() {
        return console("[serial] ");
    }

    /** The same, each line starting with {@code prefix}. */
    static SerialLog console(String prefix) {
        boolean debug = System.getenv("CONTROL_DECK_DEBUG") != null;
        return new SerialLog() {
            @Override
            public void info(String message) {
                Log.info(prefix + message);
            }

            @Override
            public void warn(String message) {
                Log.error(prefix + message);
            }

            @Override
            public void debug(String message) {
                if (debug) {
                    Log.info(prefix + message);
                }
            }
        };
    }
}
