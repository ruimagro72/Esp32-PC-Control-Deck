package dev.controldeck.host.transport.serial;

import java.io.IOException;
import java.util.List;

/** Lists and opens serial ports. {@link JSerialCommProvider} is the real one. */
public interface SerialPortProvider {

    /** Serial ports currently present, in a stable order. */
    List<PortInfo> listPorts() throws IOException;

    /** Open {@code path} as 8N1 without flow control at {@code baud}. */
    SerialPortIO open(String path, int baud) throws IOException;
}
