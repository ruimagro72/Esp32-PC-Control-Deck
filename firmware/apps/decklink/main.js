// Deck Link over BLE: the kernel runs the Deck Link service (DeckLink.bleStart), the same protocol
// as USB, so the link stays up after this app closes. This screen starts it with the passkey on,
// shows the passkey to type on the PC when pairing, and the link state. To start it at power on
// without this app, put DeckLink.bleStart(true) in /sd/autoexec.js.
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants
var BAR = rgb(30, 70, 160), WHITE = rgb(255, 255, 255), DIM = rgb(170, 180, 200), GREEN = rgb(60, 200, 90),
    AMBER = rgb(255, 190, 0), RED = rgb(220, 60, 60);

if (typeof DeckLink === "undefined" || typeof DeckLink.bleStart !== "function") {
    Display.fillScreen(0);
    Display.setTextColor(RED, 0);
    Display.drawString("This kernel has no DeckLink BLE: flash the new one.", 6, 40, 2);
    while (true) System.delay(200);
}
var started = DeckLink.bleStart(true);

function draw() {
    Display.fillScreen(0);
    Display.fillRect(0, 0, W, 26, BAR);
    Display.setTextColor(WHITE, BAR);
    Display.drawString("Deck Link (BLE)", 6, 4, 2);
    var y = 40;
    function line(label, value, col) {
        Display.setTextColor(DIM, 0); Display.drawString(label, 10, y, 2);
        Display.setTextColor(col, 0); Display.drawString(value, 110, y, 2);
        y += 26;
    }
    line("Service", started ? "on, advertising as Deck" : "failed to start", started ? GREEN : RED);
    line("PC", DeckLink.bleConnected() ? "connected" : "not connected", DeckLink.bleConnected() ? GREEN : AMBER);
    line("Server", DeckLink.greeted() ? "greeted" : "waiting for hello", DeckLink.greeted() ? GREEN : AMBER);
    var key = DeckLink.blePasskey();
    if (key) {
        Display.setTextColor(DIM, 0); Display.drawString("Passkey for the PC:", 10, y + 6, 2);
        var s = ("000000" + key).slice(-6);
        Display.setTextColor(AMBER, 0); Display.drawString(s, 40, y + 30, 4);
    }
    Display.setTextColor(DIM, 0);
    Display.drawString("Deck id " + DeckLink.id(), 10, H - 20, 1);
}

draw();
var last = "";
while (true) {
    System.delay(250);
    var now = [DeckLink.bleConnected(), DeckLink.greeted(), DeckLink.blePasskey()].join();
    if (now !== last) { last = now; draw(); }
}
