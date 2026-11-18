package dev.controldeck.host.transport.serial;

/**
 * A serial port the operating system knows about.
 *
 * @param path        what to open: /dev/ttyACM0, /dev/cu.usbmodem1101 or COM4
 * @param description human readable name reported by the driver (may be empty)
 * @param vendorId    USB vendor id, or -1 when the port is not USB or it is unknown
 */
public record PortInfo(String path, String description, int vendorId) {

    /** Espressif: the native USB of the ESP32-S3 (and C3/C6), the Decks' own port. */
    public static final int VENDOR_ESPRESSIF = 0x303A;

    /** Not a USB vendor: the serial line of a Deck running on this PC (the PC build). */
    public static final int VENDOR_PC_DECK = -2;

    /** WCH (CH340) and Silicon Labs (CP210x): USB-UART chips, whose line runs at a baud rate. */
    public static final int VENDOR_WCH = 0x1A86;
    public static final int VENDOR_SILABS = 0x10C4;

    /** True for a board behind a USB-UART chip, where the baud rate matters. */
    public boolean isUartBridge() {
        return vendorId == VENDOR_WCH || vendorId == VENDOR_SILABS;
    }

    public PortInfo(String path, String description) {
        this(path, description, -1);
    }
}
