// Tracker for the Deck (KryonOS). Layout and idea: Win-95-Tracker-Cyd (github.com/ivans805/Win-95-Tracker-Cyd).
// The kernel plays the module in real time (Audio.playMod) and gives this screen its state:
// Audio.modState(), Audio.modRow(n), Audio.modWave(), Audio.modPause(b). All drawing is JavaScript.
// Buttons: previous / play-pause / next module; touch the top-right corner for 1 s to leave.
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
var GREY = rgb(190, 190, 190), DARK = rgb(70, 70, 70), BG = 0, GREEN = rgb(0, 255, 0), WHITE = rgb(255, 255, 255);
var DIRS = ["/sd/media/music/modules", "/sd/music", "/sd/audio"];
var ROWH = 10, ROWS = 14, MID = 6, TX = 6, TY = 24;   // tracker view: 14 rows, current row in the middle

function ext(p) { var i = p.lastIndexOf("."); return i < 0 ? "" : p.substring(i + 1).toLowerCase(); }
var files = [], cur = 0, paused = false;
for (var d = 0; d < DIRS.length; d++) {
    var items = FS.listDir(DIRS[d]) || [];
    for (var i = 0; i < items.length; i++) if (ext(items[i]) === "mod") files.push(items[i]);
}

function box(x, y, w, h, fill) { Display.fillRect(x, y, w, h, fill); }
function frame() {
    Display.fillScreen(GREY);
    box(TX - 2, TY - 2, W - 2 * TX + 4, ROWS * ROWH + 4, BG);                    // tracker window
    box(6, 4, W - 12, 14, BG);                                                    // top bar (pos / row / bpm / speed)
    box(6, 168, W - 12, 18, BG);                                                  // file name
    box(6, 192, 70, 18, BG);                                                      // time
    box(W - 190, 192, 184, 40, BG);                                               // oscilloscope
    button(6, 212, "<"); button(50, 212, paused ? ">" : "||"); button(94, 212, ">>");
}
function button(x, y, label) {
    box(x, y, 40, 24, GREY); Display.drawRect(x, y, 40, 24, DARK);
    Display.setTextColor(rgb(60, 60, 60), GREY); Display.drawString(label, x + 12, y + 5, 2);
}
function name() { var p = files[cur] || ""; return p.substring(p.lastIndexOf("/") + 1); }
function load(i) {
    if (!files.length) return;
    cur = (i + files.length) % files.length;
    Audio.stopMod();
    paused = false;
    Audio.playMod(files[cur], 80);
    frame();
    Display.setTextColor(GREEN, BG); Display.drawString(name(), 8, 171, 2);
    t0 = Date.now();
}
var t0 = Date.now(), lastRow = -1, lastSec = -1, wasTouch = false, held = 0, lastWaveMs = 0, flat = false;
function drawFlat() { box(W - 190, 192, 184, 40, BG); Display.drawLine(W - 188, 212, W - 8, 212, GREEN); }

function drawTracker(s) {
    for (var slot = 0; slot < ROWS; slot++) {
        var r = s.row - MID + slot, y = TY + slot * ROWH, hl = (slot === MID);
        box(TX - 1, y - 1, W - 2 * TX + 2, ROWH, hl ? WHITE : BG);
        var t = Audio.modRow(r);
        if (t) { Display.setTextColor(hl ? BG : GREEN, hl ? WHITE : BG); Display.drawString(t, TX, y, 1); }
    }
}
function drawTop(s) {
    box(8, 5, W - 16, 12, BG);
    Display.setTextColor(GREEN, BG);
    Display.drawString("Pos " + s.order + "/" + s.length + " Row " + s.row + "/63 BPM " + s.bpm + " Spd " + s.speed + " Ch " + s.channels, 10, 6, 1);
}
function drawWave() {
    var w = Audio.modWave(); if (!w || w.length < 2) return;
    var x0 = W - 188, y0 = 194, h = 36, mn = 32767, mx = -32768, i;
    for (i = 0; i < w.length; i++) { if (w[i] < mn) mn = w[i]; if (w[i] > mx) mx = w[i]; }
    var rg = Math.max(1, mx - mn);
    box(x0 - 2, y0 - 2, 184, h + 4, BG);
    var step = w.length / 180;
    for (i = 0; i < 179; i++) {
        var a = w[Math.floor(i * step)], b = w[Math.floor((i + 1) * step)];
        Display.drawLine(x0 + i, y0 + h - 1 - Math.floor((a - mn) * (h - 1) / rg), x0 + i + 1, y0 + h - 1 - Math.floor((b - mn) * (h - 1) / rg), GREEN);
    }
}

if (!files.length) { Display.fillScreen(BG); Display.setTextColor(WHITE, BG); Display.drawString("No .mod files in /sd/media/music/modules", 6, 6, 2); }
else {
    load(0);
    while (true) {
        var s = Audio.modState();
        if (s) {
            if (s.row !== lastRow) { lastRow = s.row; drawTracker(s); drawTop(s); }
        } else if (!paused) { load(cur + 1); }                       // module ended: next one
        var sec = Math.floor((Date.now() - t0) / 1000);
        if (sec !== lastSec && !paused) {
            lastSec = sec; box(8, 195, 66, 14, BG); Display.setTextColor(GREEN, BG);
            Display.drawString((sec / 60 < 10 ? "0" : "") + Math.floor(sec / 60) + ":" + (sec % 60 < 10 ? "0" : "") + (sec % 60), 10, 195, 2);
        }
        var nowMs = Date.now();
        if (paused) { if (!flat) { drawFlat(); flat = true; } }
        else { flat = false; if (nowMs - lastWaveMs > 70) { lastWaveMs = nowMs; drawWave(); } }
        var t = System.getTouch();
        if (t && t.touched) {
            if (t.x > W - 40 && t.y < 40) { held += 1; if (held > 12) break; } else held = 0;
            if (!wasTouch && t.y >= 212) {
                if (t.x < 46) load(cur - 1);
                else if (t.x < 90) { paused = !paused; Audio.modPause(paused); button(50, 212, paused ? ">" : "||"); }
                else if (t.x < 134) load(cur + 1);
            }
        } else held = 0;
        wasTouch = !!(t && t.touched);
        System.delay(30);
    }
    Audio.stopMod();
}
