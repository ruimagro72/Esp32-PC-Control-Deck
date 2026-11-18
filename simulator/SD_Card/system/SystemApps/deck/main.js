// Deck client: stats and macros from the Control Deck server (protocol 4 over Wi-Fi,
// POST /api/deck, base64 text). Settings in deck.json (app folder): {"server","token","id"}.
// Offline mode: sample.b64 (a captured answer) when the server cannot be reached.
eval(FS.readTextFile("decknet.js"));     // inside the app folder: outside it KryonOS asks for storage permission
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }
var BLUE = rgb(30, 70, 160), WHITE = rgb(255, 255, 255), GREY = rgb(70, 80, 100), DIM = rgb(160, 170, 190),
    RED = rgb(200, 40, 40), AMBER = rgb(255, 190, 0), GREEN = rgb(60, 200, 90), BLACK = 0;
var cfg = {};
function deckId() { return usb() ? DeckLink.id() : (cfg.id || "deck"); }
try { cfg = JSON.parse(FS.readTextFile("deck.json")); } catch (e) {}
// A kernel with the Wi-Fi link: hand it the server and token (it keeps them; the server also sends
// them over USB/BLE), then every link (USB, BLE, Wi-Fi) goes through DeckLink alone.
var KERNEL_LINK = typeof DeckLink !== "undefined" && typeof DeckLink.transport === "function";
if (KERNEL_LINK && cfg.server && cfg.token) DeckLink.wifiSetup(cfg.server, cfg.token);
var BAR = 24, macros = [], stats = null, status = "", offline = false, page = 0;   // page 0 stats, 1 macros
var SHOWN = [[0, "CPU", "%"], [1, "CPU", "C"], [3, "RAM", "%"], [6, "GPU", "%"], [10, "GPU", "C"],
    [7, "VRAM", "%"], [13, "Disk", "%"], [17, "Down", "Mb/s"], [18, "Up", "Mb/s"]];

// USB link: when the server greeted this Deck on the serial port (kernel DeckLink), frames go
// there; answers and the server's stats (every few seconds) arrive by themselves (pumpUsb).
function usb() { return typeof DeckLink !== "undefined" && (KERNEL_LINK || DeckLink.greeted()); }   // name kept: "the kernel link"
function pumpUsb() {
    var got = false, s;
    while ((s = DeckLink.read()) !== null) {
        var m = DeckNet.decode(DeckNet.b64dec(s));
        if (m) { take([m]); got = true; }
    }
    return got;
}
function talk(frames) {
    if (usb()) {
        for (var i = 0; i < frames.length; i++)
            if (frames[i][2] !== 0x01) DeckLink.write(DeckNet.b64enc(frames[i]));   // the kernel sent hello
        offline = false; status = KERNEL_LINK ? (DeckLink.transport() || "connecting").toUpperCase() : "USB";
        return [];
    }
    if (!cfg.server || !cfg.token) { status = "no deck.json"; return null; }
    var r = DeckNet.exchange(function (u, b, t, h) { return Network.post(u, b, t, h, 3000); },
        cfg.server, cfg.token, frames);
    if (r.error) {
        status = r.error.substring(0, 30);
        var s = null;
        try { s = FS.readTextFile("sample.b64"); } catch (e) {}
        if (!s) return null;
        offline = true;
        return DeckNet.parse(DeckNet.b64dec(s));
    }
    offline = false; status = "";
    return r.messages;
}
function take(list) {
    if (!list) return;
    for (var i = 0; i < list.length; i++) {
        var m = list[i];
        if (m.type === "stats") stats = m;
        else if (m.type === "config") macros = m.macros;
        else if (m.type === "error") status = "server: " + m.detail;
    }
}
function tempColour(t) { return t >= 55 ? RED : t >= 50 ? AMBER : GREEN; }
function bar() {
    Display.fillRect(0, 0, W, BAR, BLUE);
    Display.setTextColor(WHITE, BLUE);
    Display.drawString(page === 0 ? "PC stats" : "Macros", 6, 4, 2);
    var t = System.getTemperature();
    Display.setTextColor(tempColour(t), BLUE);
    Display.drawString("Deck " + Math.round(t) + "C", W - 120, 4, 2);
}
function fmt(v) { return v === null || v !== v ? "-" : (v >= 100 ? Math.round(v) : Math.round(v * 10) / 10); }
function drawStats() {
    var rowH = Math.floor((H - BAR - 44) / SHOWN.length);
    Display.fillRect(0, BAR, W, H - BAR - 40, BLACK);
    for (var i = 0; i < SHOWN.length; i++) {
        var y = BAR + 4 + i * rowH, s = SHOWN[i], v = stats ? stats.values[s[0]] : null;
        Display.setTextColor(DIM, BLACK); Display.drawString(s[1] + " " + s[2], 10, y, 2);
        Display.setTextColor(WHITE, BLACK); Display.drawString("" + fmt(v), W / 2, y, 2);
        if (s[2] === "%" && v === v && v !== null) {
            var bw = Math.round((W / 2 - 60) * Math.min(v, 100) / 100);
            Display.fillRect(W / 2 + 50, y + 3, W / 2 - 60, rowH - 8, GREY);
            Display.fillRect(W / 2 + 50, y + 3, bw, rowH - 8, v > 85 ? RED : GREEN);
        }
    }
}
function macroBox(k) {
    var cols = 2, bw = (W - 18) / cols, bh = 40;
    return { x: 6 + (k % cols) * (bw + 6), y: BAR + 6 + Math.floor(k / cols) * (bh + 6), w: bw, h: bh };
}
function drawMacros() {
    Display.fillRect(0, BAR, W, H - BAR - 40, BLACK);
    for (var k = 0; k < macros.length; k++) {
        var b = macroBox(k);
        if (b.y + b.h > H - 44) break;
        Display.fillRect(b.x, b.y, b.w, b.h, GREY);
        Display.setTextColor(WHITE, GREY);
        Display.drawString(macros[k].label.substring(0, 14), b.x + 8, b.y + 12, 2);
    }
}
function foot() {
    var y = H - 38;
    Display.fillRect(0, y, W, 38, BLACK);
    Display.fillRect(6, y + 2, 90, 32, rgb(90, 90, 90));
    Display.setTextColor(AMBER, rgb(90, 90, 90));
    Display.drawString(page === 0 ? "Macros" : "Stats", 18, y + 10, 2);
    Display.setTextColor(offline ? AMBER : DIM, BLACK);
    Display.drawString(offline ? "offline: " + status : status, 104, y + 12, 1);
}
function draw() { bar(); if (page === 0) drawStats(); else drawMacros(); foot(); }

take(talk([DeckNet.hello(deckId(), "0.8.0", "es3c28p"), DeckNet.statsRequest(deckId()), DeckNet.configRequest()]));
Display.fillScreen(BLACK);
draw();
var down = false, tx = 0, ty = 0, last = Date.now(), retry = Date.now();
try {
while (true) {
    System.delay(20);
    if (usb() && pumpUsb()) draw();                  // USB: frames come by themselves
    var t = System.getTouch();
    if (t.touched) { down = true; tx = t.x; ty = t.y; continue; }
    if (down) {
        down = false;
        if (ty >= H - 38 && tx < 100) { page = 1 - page; draw(); continue; }
        if (page === 1) {
            for (var k = 0; k < macros.length; k++) {
                var b = macroBox(k);
                if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h) {
                    Display.drawRect(b.x, b.y, b.w, b.h, AMBER);
                    take(talk([DeckNet.macro(macros[k].key, deckId())]));
                    status = offline ? status : "sent " + macros[k].label;
                    draw();
                }
            }
        }
    }
    if (Date.now() - last > 2000) {                    // the server's stats interval
        last = Date.now();
        if (offline && Date.now() - retry > 10000) {   // try the server again
            retry = Date.now();
            take(talk([DeckNet.hello(deckId(), "0.8.0", "es3c28p"), DeckNet.statsRequest(deckId()), DeckNet.configRequest()]));
            draw();
        } else if (page === 0 && !offline && !usb()) { take(talk([DeckNet.statsRequest(deckId())])); draw(); }
        else bar();
    }
}
} catch (e) {                                       // show it; the exception screen would block the console
    if (("" + e).indexOf("OS_EXIT") >= 0) throw e;  // the X button: KryonOS leaves the app this way
    console.log("[deck] " + e);
    Display.setTextColor(RED, BLACK); Display.drawString(("" + e).substring(0, 40), 6, H - 52, 1);
    System.delay(5000);
}
