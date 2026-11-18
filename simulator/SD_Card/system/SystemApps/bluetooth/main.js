// Deck Bluetooth screen: what the board supports, make the Deck visible (BLE advertising),
// scan for devices. Colours are 24-bit RGB (KryonOS JS).
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants
var C_BAR = rgb(30, 70, 160), C_TXT = rgb(255, 255, 255), C_DIM = rgb(200, 200, 200), C_ON = rgb(40, 160, 40),
    C_BTN = rgb(70, 80, 100), C_STAT = rgb(255, 220, 0);
var PY = 50, BY = 96, BH = 40;   // power row, then visible/scan; big enough for this touch panel (about 5-10 px error)
var NAME = "DeckTest";
var caps = {}, power = true, visible = false, scanning = false, status = "", devices = {}, order = [];

function txt(s, x, y, col, bg, f) { Display.setTextColor(col, bg); Display.drawString(s, x, y, f || 2); }
function button(x, y, w, h, label, on) {
    var bg = on ? C_ON : C_BTN;
    Display.fillRect(x, y, w, h, bg);
    txt(label, x + 10, y + 12, C_TXT, bg);
}
// ---- Bluetooth keyboard screen: scan, pick, connect, forget. The kernel remembers the keyboard and
// reconnects to it by itself at start-up (Keyboard.host*). Typing works in every app that reads Keyboard.
function keyboardScreen() {
    var found = [], msg = "", lastPoll = 0, wasDown = false, ptx = 0, pty = 0, dn = false;
    function show() {
        var st = Keyboard.hostStatus();
        Display.fillScreen(0);
        Display.fillRect(0, 0, W, 26, C_BAR);
        txt("Keyboard", 6, 4, C_TXT, C_BAR, 4);
        txt(st.connected ? "Connected" : (st.saved ? "Saved: " + st.saved : "No keyboard saved"), 6, 32, st.connected ? C_STAT : C_DIM, 0);
        txt(st.state + (msg ? "  " + msg : ""), 6, 52, C_DIM, 0);
        button(6, 74, W / 2 - 12, 34, st.scanning ? "Scanning..." : "Scan", st.scanning);
        button(W / 2 + 6, 74, W / 2 - 12, 34, "Forget", false);
        button(6, H - 40, 70, 34, "Back", false);
        for (var i = 0; i < found.length && i < 5; i++) {
            var y = 116 + i * 20;
            Display.fillRect(6, y, W - 12, 18, C_BTN);
            txt((found[i].name || "(no name)") + "  " + found[i].rssi + " dBm", 10, y + 2, C_TXT, C_BTN, 2);
        }
    }
    Keyboard.hostScan();            // start with a scan: put the keyboard in pairing mode first
    show();
    while (true) {
        System.delay(40);
        var st = Keyboard.hostStatus();
        if (System.millis() - lastPoll > 700) {
            lastPoll = System.millis(); found = Keyboard.hostList(); show();
        }
        var t = System.getTouch();
        if (t.touched) { dn = true; ptx = t.x; pty = t.y; continue; }
        if (!dn) continue;
        dn = false;
        if (pty >= H - 44 && ptx < 80) return;                                   // Back
        if (pty >= 70 && pty <= 112 && ptx < W / 2) { Keyboard.hostScan(); msg = ""; show(); continue; }
        if (pty >= 70 && pty <= 112 && ptx >= W / 2) { Keyboard.hostForget(); msg = "forgotten"; found = []; show(); continue; }
        for (var i = 0; i < found.length && i < 5; i++) {
            var y = 116 + i * 20;
            if (pty >= y && pty < y + 18) { Keyboard.hostConnect(found[i].addr); msg = "connecting..."; show(); }
        }
    }
}

function draw() {
    Display.fillScreen(0);
    Display.fillRect(0, 0, W, 26, C_BAR);
    txt("Bluetooth", 6, 4, C_TXT, C_BAR, 4);
    txt("BLE: " + (caps.ble ? (caps.ble5 ? "yes (BLE 5)" : "yes") : "no") +
        "   Classic: " + (caps.classic ? "yes" : "not on this board"), 6, 32, C_DIM, 0);
    button(6, PY, W / 2 - 12, BH, power ? "Bluetooth: ON" : "Bluetooth: OFF", power);
    button(W / 2 + 6, PY, W / 2 - 12, BH, "Keyboard...", Keyboard.hostStatus && Keyboard.hostStatus().connected);
    button(6, BY, W / 2 - 12, BH, visible ? "Visible: ON" : "Visible: OFF", visible);
    button(W / 2 + 6, BY, W / 2 - 12, BH, scanning ? "Scanning..." : "Scan", scanning);
    drawStatus();
    var y = BY + BH + 26;
    for (var i = 0; i < order.length && y < H - 16; i++, y += 18) {
        var d = devices[order[i]];
        txt((d.name || "(no name)") + "  " + d.addr + "  " + d.rssi + " dBm", 6, y, C_TXT, 0);
    }
}
function drawStatus() { Display.fillRect(0, BY + BH + 4, W, 18, 0); txt(status, 6, BY + BH + 5, C_STAT, 0); }
function setStatus(s) { status = s; draw(); }

Bluetooth.enable();
caps = Bluetooth.getCapabilities() || {};
setStatus("On. MAC " + Bluetooth.getMacAddress() + ". Not visible.");
Bluetooth.BLE.Scanner.on("discover", function (d) {
    if (!devices[d.address]) order.push(d.address);
    devices[d.address] = { name: d.name, addr: d.address, rssi: d.rssi };
});

var down = false, tx = 0, ty = 0, scanEnd = 0, lastDraw = 0;
while (true) {
    System.delay(30);
    if (scanning && System.millis() > scanEnd) {
        Bluetooth.BLE.Scanner.stop(); scanning = false;
        setStatus("Scan done: " + order.length + " device(s).");
    }
    if (scanning && System.millis() - lastDraw > 1000) { lastDraw = System.millis(); status = "Scanning... " + order.length + " found"; drawStatus(); }
    var t = System.getTouch();
    if (t.touched) { down = true; tx = t.x; ty = t.y; continue; }
    if (!down) continue;
    down = false;
    if (ty >= PY - 4 && ty <= PY + BH + 2 && tx >= W / 2) { keyboardScreen(); draw(); continue; }
    if (ty >= PY - 4 && ty <= PY + BH + 2) {                       // power on/off
        if (power) {
            if (scanning) { Bluetooth.BLE.Scanner.stop(); scanning = false; }
            if (visible) { Bluetooth._raw.stopAdv(); visible = false; }
            Bluetooth.disable(); power = false; setStatus("Bluetooth off.");
        } else {
            Bluetooth.enable(); power = true; setStatus("On. MAC " + Bluetooth.getMacAddress() + ". Not visible.");
        }
        continue;
    }
    if (!power) { setStatus("Bluetooth is off: tap the top button."); continue; }
    if (ty >= BY - 6 && ty <= BY + BH + 6 && tx < W / 2) {
        if (!visible) visible = Bluetooth._raw.startAdv(NAME, [], 160, 160);
        else { Bluetooth._raw.stopAdv(); visible = false; }
        setStatus(visible ? "Visible as '" + NAME + "' (BLE). The PC must scan for BLE." : "Not visible.");
    } else if (ty >= BY - 6 && ty <= BY + BH + 6 && tx >= W / 2 && !scanning) {
        devices = {}; order = [];
        scanning = Bluetooth.BLE.Scanner.start({ timeout: 0, active: true });
        scanEnd = System.millis() + 8000;
        setStatus(scanning ? "Scanning 8 s..." : "Scan failed to start");
    }
}
