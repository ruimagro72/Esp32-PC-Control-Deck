// Deck client: stats and macros from the Control Deck server (protocol 4 over Wi-Fi,
// POST /api/deck, base64 text). Settings in deck.json (<paths.data>/deck.json, read from the
// per-app paths descriptor in app.json: /sd/UserData/deck): {"server","token","id"}.
// Offline: when the server cannot be reached the footer says so and the app tries again every 10 s.
eval(FS.readTextFile("decknet.js"));     // inside the app folder: outside it KryonOS asks for storage permission
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }
var BLUE = rgb(30, 70, 160), WHITE = rgb(255, 255, 255), GREY = rgb(70, 80, 100), DIM = rgb(160, 170, 190),
    RED = rgb(200, 40, 40), AMBER = rgb(255, 190, 0), GREEN = rgb(60, 200, 90), BLACK = 0;
var cfg = {};
function deckId() { return usb() ? DeckLink.id() : (cfg.id || "deck"); }
// Per-app paths descriptor (app.json -> paths): data dir of the mutable deck.json.
var PATHS = {};
try { PATHS = JSON.parse(FS.readTextFile("app.json")).paths || {}; } catch (e) {}
var DECK_JSON = PATHS.data ? PATHS.data + "/deck.json" : null;
if (DECK_JSON) { try { var deckRaw = FS.readTextFile(DECK_JSON); if (deckRaw) cfg = JSON.parse(deckRaw); } catch (e) {} }
// Boot environment variables (kernel ENV, set in autoexec.js): fallback when deck.json has no value.
if (typeof ENV !== "undefined" && ENV.get) {
    if (!cfg.server) cfg.server = ENV.get("deckServer") || "";
    if (!cfg.token) cfg.token = ENV.get("deckToken") || "";
}
// A kernel with the Wi-Fi link: hand it the server and token (it keeps them; the server also sends
// them over USB/BLE), then every link (USB, BLE, Wi-Fi) goes through DeckLink alone.
var KERNEL_LINK = typeof DeckLink !== "undefined" && typeof DeckLink.transport === "function";
if (KERNEL_LINK && cfg.server && cfg.token) DeckLink.wifiSetup(cfg.server, cfg.token);
var BAR = 24, macros = [], stats = null, status = "", offline = false, page = 0;   // page 0 stats, 1 macros, 2 messages
var PAGE_NEXT = [2, 0, 1], PAGE_LABEL = ["Msgs", "Stats", "Macros"];           // the footer button goes to the next page
var inbox = [], INBOX_MAX = 20, MSG_ITEM_H = 50, polls = 0;                   // inbox: newest first; a request every 5th poll
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
        offline = true;                              // the loop retries (every 10 s) while offline
        return null;
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
        else if (m.type === "messages") addMessages(m.messages);
    }
}
// Inbox: the server sends only messages newer than the newest id asked for; keep INBOX_MAX, newest first.
function addMessages(list) {
    for (var i = 0; i < list.length; i++) {
        var known = false;
        for (var j = 0; j < inbox.length; j++) if (inbox[j].id === list[i].id) known = true;
        if (!known) inbox.push(list[i]);
    }
    inbox.sort(function (a, b) { return b.id - a.id; });
    if (inbox.length > INBOX_MAX) inbox.length = INBOX_MAX;
}
function newestId() { return inbox.length ? inbox[0].id : 0; }
function unreadCount() { var n = 0; for (var i = 0; i < inbox.length; i++) if (!inbox[i].read) n++; return n; }
// Read ids not yet acknowledged by the server (0x11). Kept until a talk() succeeds, so an offline read is sent later.
var pendingReads = [];
function readMessage(idx) {
    var m = inbox[idx];
    if (m.read) return;
    m.read = true;                                   // shown read at once; the server gets 0x11 when it is reachable
    pendingReads.push(m.id);
    flushReads();
    draw();
}
function readFrames() {
    var frames = [];
    for (var i = 0; i < pendingReads.length; i++) frames.push(DeckNet.messageRead(deckId(), pendingReads[i]));
    return frames;
}
function flushReads() {
    if (!pendingReads.length) return;
    var got = talk(readFrames());
    take(got);
    if (got !== null) pendingReads = [];             // null: offline, the next successful poll resends them
}
function tempColour(t) { return t >= 55 ? RED : t >= 50 ? AMBER : GREEN; }
function bar() {
    Display.fillRect(0, 0, W, BAR, BLUE);
    Display.setTextColor(WHITE, BLUE);
    Display.drawString(page === 0 ? "PC stats" : page === 1 ? "Macros" : "Messages", 6, 4, 2);
    if (page === 2) { Display.drawString("Back", W - 56, 4, 2); return; }
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
// Messages screen: one row per message (from, then the text wrapped to two lines), unread in amber.
function wrapText(s, cols, maxLines) {
    var words = ("" + s).replace(/\s+/g, " ").replace(/^ | $/g, "").split(" "), lines = [], cur = "";
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        while (w.length > cols) { if (cur) { lines.push(cur); cur = ""; } lines.push(w.substring(0, cols)); w = w.substring(cols); }
        if (cur === "") cur = w;
        else if (cur.length + 1 + w.length <= cols) cur += " " + w;
        else { lines.push(cur); cur = w; }
    }
    if (cur) lines.push(cur);
    if (lines.length > maxLines) {
        lines = lines.slice(0, maxLines);
        lines[maxLines - 1] = lines[maxLines - 1].substring(0, cols - 3) + "...";
    }
    return lines;
}
function drawMessages() {
    Display.fillRect(0, BAR, W, H - BAR - 40, BLACK);
    if (!inbox.length) { Display.setTextColor(DIM, BLACK); Display.drawString("No messages", 10, BAR + 20, 2); return; }
    var rows = Math.floor((H - 38 - BAR) / MSG_ITEM_H);
    for (var i = 0; i < inbox.length && i < rows; i++) {
        var m = inbox[i], y = BAR + i * MSG_ITEM_H;
        Display.setTextColor(m.read ? DIM : AMBER, BLACK);
        Display.drawString(("" + m.from).substring(0, 19), 8, y + 4, 2);
        var lines = wrapText(m.voice ? "Voice message, " + m.text + " s" : m.text, 36, 2);
        Display.setTextColor(m.read ? DIM : WHITE, BLACK);
        for (var j = 0; j < lines.length; j++) Display.drawString(lines[j], 8, y + 22 + j * 12, 1);
        Display.fillRect(0, y + MSG_ITEM_H - 2, W, 1, GREY);
    }
}
function foot() {
    var y = H - 38;
    Display.fillRect(0, y, W, 38, BLACK);
    Display.fillRect(6, y + 2, 90, 32, rgb(90, 90, 90));
    Display.setTextColor(AMBER, rgb(90, 90, 90));
    Display.drawString(PAGE_LABEL[page], 18, y + 10, 2);
    var n = unreadCount();
    Display.fillRect(74, y - 8, 32, 8, BLACK);     // the badge's strip above the footer: clear, then draw if any
    if (n > 0) {                                    // unread badge on the footer button's top edge, every page
        Display.fillRect(80, y - 7, 22, 14, RED);
        Display.setTextColor(WHITE, RED);
        Display.drawString(n > 99 ? "99+" : "" + n, 84, y - 5, 1);
    }
    Display.setTextColor(offline ? AMBER : DIM, BLACK);
    if (page === 2) Display.drawString(inbox.length + " messages, " + n + " unread", 104, y + 12, 1);
    else Display.drawString(offline ? "offline: " + status : status, 104, y + 12, 1);
}
function draw() { bar(); if (page === 0) drawStats(); else if (page === 1) drawMacros(); else drawMessages(); foot(); }

take(talk([DeckNet.hello(deckId(), "0.8.0", "es3c28p"), DeckNet.statsRequest(deckId()), DeckNet.configRequest()]));
Display.fillScreen(BLACK);
draw();
var down = false, tx = 0, ty = 0, last = Date.now(), retry = Date.now();
while (true) {
try {                                               // one pass at a time: an error is shown and the loop goes on
    System.delay(20);
    if (usb() && pumpUsb()) draw();                  // USB: frames come by themselves
    var t = System.getTouch();
    if (t.touched) { down = true; tx = t.x; ty = t.y; continue; }
    if (down) {
        down = false;
        if (ty < BAR && page === 2 && tx >= W - 80) { page = 0; draw(); continue; }     // Back
        if (ty >= H - 38 && tx < 100) { page = PAGE_NEXT[page]; draw(); continue; }
        if (page === 2 && ty >= BAR && ty < H - 38) {
            var row = Math.floor((ty - BAR) / MSG_ITEM_H);
            if (row < inbox.length && row < Math.floor((H - 38 - BAR) / MSG_ITEM_H)) readMessage(row);
            continue;
        }
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
        } else if (!offline) {
            var frames = readFrames();                 // unacknowledged reads go first
            if (page === 0 && !usb()) frames.push(DeckNet.statsRequest(deckId()));
            if (page === 2 || ++polls % 5 === 0) frames.push(DeckNet.messagesRequest(deckId(), newestId()));   // messages: every poll on that screen, else every 5th
            if (frames.length) {
                var got = talk(frames);
                take(got);
                if (got !== null) pendingReads = [];
                draw();
            } else bar();
        } else bar();
    }
} catch (e) {                                       // show it; the exception screen would block the console
    if (("" + e).indexOf("OS_EXIT") >= 0) throw e;  // the X button: KryonOS leaves the app this way
    console.log("[deck] " + e);
    Display.setTextColor(RED, BLACK); Display.drawString(("" + e).substring(0, 40), 6, H - 52, 1);
    System.delay(5000);
}
}
