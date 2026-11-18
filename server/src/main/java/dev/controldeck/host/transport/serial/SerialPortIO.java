package dev.controldeck.host.transport.serial;

import java.io.Closeable;
import java.io.IOException;

/**
 * One open serial port. The bridge talks to this interface only, so the protocol
 * code can be tested with a fake board and the jSerialComm library stays in one class.
 */
public interface SerialPortIO extends Closeable {

    /**
     * Read what has arrived, waiting a short time (tens of milliseconds) for it.
     *
     * @return the number of bytes stored in {@code buffer}; 0 when nothing arrived
     * @throws IOException when the port is gone (unplugged) or cannot be read
     */
    int read(byte[] buffer) throws IOException;

    /** Write all {@code length} bytes or fail. */
    void write(byte[] data, int offset, int length) throws IOException;

    @Override
    void close();
}
