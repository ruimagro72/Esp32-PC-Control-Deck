// Terminal: a shell on the Deck's screen. Input: a tap on the prompt bar opens KryonOS's touch
// keyboard; a hardware keyboard works through Keyboard.getKey() as soon as the kernel feeds that
// queue (USB or BLE host: kernel work, see docs/firmware/KEYBOARD_TERMINAL.md). Commands run in JS on the
// FS / System / Device APIs; "help" lists them. The first time, KryonOS asks for storage permission
// (the shell reads outside the app folder): choose Always Allow.
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants
var BG = 0, FG = rgb(220, 230, 220), DIM = rgb(120, 140, 120), PROMPT = rgb(80, 220, 120), ERR = rgb(240, 100, 100), BAR = rgb(30, 70, 160);
var FONT = 1, CW = 6, LH = 10, TOP = 14, COLS = Math.floor(W / CW), ROWS = Math.floor((H - TOP - 16) / LH);
var lines = [], cwd = "/sd", typed = "", history = [], hpos = 0;

function put(s, col) {                       // append text, wrapped to the screen width
    s = String(s);
    var parts = s.split("\n");
    for (var i = 0; i < parts.length; i++) {
        var p = parts[i];
        do { lines.push({ t: p.substring(0, COLS), c: col || FG }); p = p.substring(COLS); } while (p.length);
    }
    if (lines.length > 300) lines = lines.slice(lines.length - 300);
}
function draw() {
    Display.fillRect(0, 0, W, TOP, BAR);
    Display.setTextColor(rgb(255, 255, 255), BAR);
    Display.drawString("Terminal  " + cwd, 4, 3, FONT);
    Display.fillRect(0, TOP, W, H - TOP, BG);
    var first = Math.max(0, lines.length - ROWS);
    for (var i = first; i < lines.length; i++) {
        Display.setTextColor(lines[i].c, BG);
        Display.drawString(lines[i].t, 0, TOP + (i - first) * LH + 2, FONT);
    }
    var y = H - 14;
    Display.fillRect(0, y, W, 14, rgb(20, 30, 20));
    Display.setTextColor(PROMPT, rgb(20, 30, 20));
    var pr = "$ " + typed + "_";
    Display.drawString(pr.length > COLS ? pr.substring(pr.length - COLS) : pr, 0, y + 3, FONT);
}

// ---- paths ----
function resolve(p) {
    if (!p) return cwd;
    var parts = (p.charAt(0) === "/" ? p : cwd + "/" + p).split("/"), out = [];
    for (var i = 0; i < parts.length; i++) {
        if (parts[i] === "" || parts[i] === ".") continue;
        if (parts[i] === "..") out.pop(); else out.push(parts[i]);
    }
    return "/" + out.join("/");
}
function base(p) { return p.substring(p.lastIndexOf("/") + 1); }
function kb(n) { return n >= 1048576 ? (n / 1048576).toFixed(1) + " MB" : n >= 1024 ? (n / 1024).toFixed(1) + " KB" : n + " B"; }

// ---- commands ----
var C = {
    help: function () {
        put("ls cd pwd cat head rm mkdir rmdir cp mv df free temp bat led js run clear exit reboot\n" +
            "ls [dir]  cd dir  cat file  head file [n]  cp a b  mv a b  rm file  mkdir d  rmdir d\n" +
            "df  free  temp  bat  led r g b | led blink|fade|breathe r g b | led off\n" +
            "js <code>  run <app>  clear  exit  reboot", DIM);
    },
    pwd: function () { put(cwd); },
    cd: function (a) {
        var d = resolve(a[0] || "/sd");
        if (d === "/") { cwd = "/"; return; }
        if (FS.isDirectory(d)) cwd = d; else put("cd: not a folder: " + d, ERR);
    },
    ls: function (a) {
        var d = resolve(a[0]), items = FS.listDir(d) || [];
        if (!items.length) { put("(empty or not found)", DIM); return; }
        for (var i = 0; i < items.length; i++) {
            var n = base(items[i]);
            put(FS.isDirectory(items[i]) ? n + "/" : n + "  " + kb(FS.getFileSize(items[i])), FS.isDirectory(items[i]) ? PROMPT : FG);
        }
    },
    cat: function (a) {
        var t = FS.readTextFile(resolve(a[0]));
        if (t === null) { put("cat: cannot read " + a[0], ERR); return; }
        put(t.length > 4000 ? t.substring(0, 4000) + "\n...(cut at 4000 chars)" : t);
    },
    head: function (a) {
        var t = FS.readTextFile(resolve(a[0]));
        if (t === null) { put("head: cannot read " + a[0], ERR); return; }
        put(t.split("\n").slice(0, parseInt(a[1] || "10", 10)).join("\n"));
    },
    rm: function (a) { put(FS.deleteFile(resolve(a[0])) ? "removed" : "rm: failed", FS.exists(resolve(a[0])) ? ERR : DIM); },
    mkdir: function (a) { put(FS.mkdir(resolve(a[0])) ? "created" : "mkdir: failed", DIM); },
    rmdir: function (a) { put(FS.rmdir(resolve(a[0])) ? "removed" : "rmdir: failed (not empty?)", DIM); },
    cp: function (a) { put(FS.copyFile(resolve(a[0]), resolve(a[1])) ? "copied" : "cp: failed", DIM); },
    mv: function (a) { put(FS.renameFile(resolve(a[0]), resolve(a[1])) ? "moved" : "mv: failed", DIM); },
    df: function () {
        var d = ["/sd", "/local"];
        for (var i = 0; i < d.length; i++) {
            var t = FS.getTotalSpace(d[i]), f = FS.getFreeSpace(d[i]);
            put(d[i] + "  " + kb(t - f) + " used of " + kb(t) + "  (" + kb(f) + " free)");
        }
    },
    free: function () { var i = System.getInfo(); put("RAM free " + kb(i.freeRAM) + " of " + kb(i.totalRAM)); },
    temp: function () { put(System.getTemperature().toFixed(1) + " C"); },
    bat: function () {
        put(Device.getBatteryVoltage().toFixed(2) + " V  " + Device.getBatteryPercent() + " %  " + (Device.isCharging() ? "charging" : "on battery"));
    },
    led: function (a) {
        var fx = ["blink", "fade", "breathe"];
        if (a[0] === "off") { System.setLED(0, 0, 0); return; }
        if (fx.indexOf(a[0]) >= 0 && typeof System.ledEffect === "function") { System.ledEffect(a[0], +a[1] || 0, +a[2] || 0, +a[3] || 0, 1200); return; }
        System.setLED(+a[0] || 0, +a[1] || 0, +a[2] || 0);
    },
    js: function (a, raw) {
        try { var r = eval(raw.substring(3)); put(r === undefined ? "undefined" : String(r)); } catch (e) { put("js: " + e, ERR); }
    },
    run: function (a) { put("launching " + a[0] + " ...", DIM); draw(); System.launch(a[0]); },
    clear: function () { lines = []; },
    exit: function () { System.launch("deckshell"); },
    reboot: function () { System.restart(); }
};

function exec(line) {
    line = line.replace(/^\s+|\s+$/g, "");
    if (!line) return;
    put("$ " + line, PROMPT);
    history.push(line); hpos = history.length;
    var a = line.split(/\s+/), cmd = a.shift();
    if (!C.hasOwnProperty(cmd)) { put(cmd + ": unknown command (help)", ERR); return; }
    try { C[cmd](a, line); } catch (e) { if (("" + e).indexOf("OS_EXIT") >= 0) throw e; put(cmd + ": " + e, ERR); }
}

// ---- main loop ----
put("Deck terminal. Tap the bottom bar to type; 'help' lists commands.", DIM);
draw();
var down = false, ty = 0, hasKeys = typeof Keyboard !== "undefined" && typeof Keyboard.getKey === "function";
while (true) {
    System.delay(20);
    if (hasKeys) {                                  // a hardware keyboard, when the kernel has one
        var k = Keyboard.getKey(), changed = false;
        while (k !== null && k !== undefined) {
            if (k === "\n" || k === "\r") { var l = typed; typed = ""; exec(l); }
            else if (k === "\b") typed = typed.substring(0, typed.length - 1);
            else if (k.length === 1 && k.charCodeAt(0) >= 32) typed += k;
            changed = true; k = Keyboard.getKey();
        }
        if (changed) draw();
    }
    var t = System.getTouch();
    if (t.touched) { down = true; ty = t.y; continue; }
    if (!down) continue;
    down = false;
    if (ty >= H - 16) {                             // the prompt bar: the touch keyboard
        var s = System.prompt("$ " + cwd, typed);
        if (s !== null && s !== undefined && s !== "") { typed = ""; exec(s); } else typed = s || "";
        draw();
    }
}
