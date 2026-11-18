// VNC test bench for KryonOS (ES5, Duktape).
// Measures if JS on the ESP32-S3 is fast enough for a VNC client.
// Path: VNC server (TCP 5900) <- WebSocket bridge on the PC (Java) <- WebSocket <- this app.
// Protocol: RFB 3.3/3.7/3.8, auth None or VNC (DES), pixels RGB565 big endian,
// encodings Raw and Hextile. Written new; the state machine follows jsVNC's layout.
// Menu: tap the status bar at the bottom (exit, encoding, colours, updates, view position).
// Exit: the menu, holding the top-right corner 0.8 s, or the console "exit".
// Bridge on the PC (frames must stay under 15 KB, the
// limit of the ESP32 WebSocket library, so the read buffer is cut to 8 KB):
//   firmware/tools/vnc-bridge.sh 5901 127.0.0.1:5900   (Java, no Python; or the server starts one for the virtual monitor)

// ---- Settings ----
var CFG = {
    host: "10.0.1.3",       // PC running the WebSocket bridge
    port: 5901,             // bridge listen port
    password: "1234",       // VNC password ("" = try auth None); test value for krfb
    encoding: "raw",        // "raw" or "hextile" (hextile is slow in JS: about 4x)
    colors: 16,             // 16 = RGB565 (65k colours), 8 = RGB332 (256 colours, half the data)
    fullFrames: false,      // true: ask for the whole view every time (steady benchmark)
                            // false: incremental updates (only what changed, normal use)
    viewX: 0,               // top-left of the desktop area to show
    viewY: 0
};

var SW = Display.width();
var SH = Display.height();
var BAR = 16;               // status bar height at the bottom
var VW = SW, VH = SH - BAR; // VNC view size on screen

// ---- Settings file and virtual monitor ----
// vnc.json (<paths.data>/vnc.json, from the per-app paths descriptor in app.json: /sd/UserData/vnc) overrides CFG (edit it on the card; no settings screen needed).
// With "virtual": true, "server" and "token", the Control Deck server starts a virtual monitor
// the size of this screen (krfb-virtualmonitor + the Java WebSocket bridge) and the app connects to it.
//   {"virtual": true, "server": "http://10.0.1.3:8080", "token": "...", "encoding": "raw"}
var VIRT = "";
(function () {
    var f = null;
    // Per-app paths descriptor (app.json -> paths): data dir of the mutable vnc.json.
    var PATHS = {};
    try { PATHS = JSON.parse(FS.readTextFile("app.json")).paths || {}; } catch (e) {}
    var VNC_JSON = PATHS.data ? PATHS.data + "/vnc.json" : null;
    if (VNC_JSON) { try { f = JSON.parse(FS.readTextFile(VNC_JSON)); } catch (e) {} }
    if (!f) return;
    for (var k in f) if (CFG.hasOwnProperty(k)) CFG[k] = f[k];
    if (!f.virtual || !f.server || !f.token) return;
    var r = Network.post(f.server + "/api/vmonitor", JSON.stringify({ resolution: VW + "x" + VH }),
        "application/json", { "Authorization": "Bearer " + f.token }, 8000);
    var m = null;
    try { m = r && r.status === 200 ? JSON.parse(r.body) : null; } catch (e) {}
    if (!m || !m.running) { VIRT = "virtual monitor failed: " + (r ? r.status + " " + (r.error || r.body) : "no answer"); return; }
    CFG.host = f.server.replace(/^https?:\/\//, "").replace(/:\d+.*$/, "");
    CFG.port = m.ws_port; CFG.password = m.password; CFG.viewX = 0; CFG.viewY = 0;
    VIRT = "virtual " + m.resolution;
})();

// ---- Colours: 8-bit RGB332 from the server, turned into RGB565 for the panel ----
var BPP = CFG.colors === 8 ? 1 : 2;
var LUT = new Array(256);
for (var li = 0; li < 256; li++) {
    var r3 = (li >> 5) & 7, g3 = (li >> 2) & 7, b2 = li & 3;
    LUT[li] = (Math.round(r3 * 31 / 7) << 11) | (Math.round(g3 * 63 / 7) << 5) | Math.round(b2 * 31 / 3);
}
var LINE = new Uint8Array(8192);   // RGB565 scratch for 8-bit pixels
// Turn n 8-bit pixels at BUF[from] into RGB565 bytes in LINE.
function to565(from, n) {
    for (var i = 0, o = 0; i < n; i++, o += 2) {
        var c = LUT[BUF[from + i]];
        LINE[o] = c >> 8; LINE[o + 1] = c & 255;
    }
    return LINE.subarray(0, n * 2);
}
function px() { return BPP === 2 ? u16() : LUT[u8()]; }

// ---- Input byte stream (two buffers, swapped on compact) ----
var BUF = new Uint8Array(32768), BUF2 = new Uint8Array(32768);
var rp = 0, wp = 0;

function feed(d) {
    var n = d.length;
    if (wp + n > BUF.length) {
        var keep = wp - rp;
        var size = BUF.length;
        while (keep + n > size) size *= 2;
        if (size > BUF2.length) BUF2 = new Uint8Array(size);
        BUF2.set(BUF.subarray(rp, wp), 0);
        var t = BUF; BUF = BUF2; BUF2 = t;
        if (BUF2.length < BUF.length) BUF2 = new Uint8Array(BUF.length);
        rp = 0; wp = keep;
    }
    BUF.set(d, wp);
    wp += n;
}
function avail() { return wp - rp; }
function u8() { return BUF[rp++]; }
function u16() { var v = (BUF[rp] << 8) | BUF[rp + 1]; rp += 2; return v; }
function u32() { var v = BUF[rp] * 16777216 + ((BUF[rp + 1] << 16) | (BUF[rp + 2] << 8) | BUF[rp + 3]); rp += 4; return v; }
function s32() { var v = (BUF[rp] << 24) | (BUF[rp + 1] << 16) | (BUF[rp + 2] << 8) | BUF[rp + 3]; rp += 4; return v; }
function str(n) { var s = ""; for (var i = 0; i < n; i++) s += String.fromCharCode(BUF[rp++]); return s; }

// ---- Output helpers ----
var ws = null;
var bytesOut = 0;
function send(a) { bytesOut += a.length; ws.send(new Uint8Array(a)); }
function p16(a, v) { a.push((v >> 8) & 255, v & 255); }
function p32(a, v) { a.push((v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255); }

// --- DES begin (FIPS 46-3, encrypt one block; bits as 0/1 arrays; only used for VNC auth) ---
var DES_IP = [58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7];
var DES_FP = [40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25];
var DES_E = [32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,16,17,18,19,20,21,20,21,
    22,23,24,25,24,25,26,27,28,29,28,29,30,31,32,1];
var DES_P = [16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25];
var DES_PC1 = [57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,
    63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4];
var DES_PC2 = [14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,
    41,52,31,37,47,55,30,40,51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32];
var DES_SH = [1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1];
var DES_S = [
 [14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13],
 [15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9],
 [10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12],
 [7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14],
 [2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3],
 [12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13],
 [4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12],
 [13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11]];

function desPerm(bits, table) { var o = []; for (var i = 0; i < table.length; i++) o.push(bits[table[i] - 1]); return o; }
function desBits(bytes, off) {
    var o = [];
    for (var i = 0; i < 8; i++) for (var b = 7; b >= 0; b--) o.push((bytes[off + i] >> b) & 1);
    return o;
}
function desRot(a, n) { return a.slice(n).concat(a.slice(0, n)); }
// key: 8 bytes, input: bytes at off. Returns 8 bytes.
function desEncrypt(key, input, off) {
    var k = desPerm(desBits(key, 0), DES_PC1);
    var c = k.slice(0, 28), d = k.slice(28);
    var blk = desPerm(desBits(input, off), DES_IP);
    var l = blk.slice(0, 32), r = blk.slice(32);
    for (var round = 0; round < 16; round++) {
        c = desRot(c, DES_SH[round]);
        d = desRot(d, DES_SH[round]);
        var k48 = desPerm(c.concat(d), DES_PC2);
        var x = desPerm(r, DES_E);
        for (var i = 0; i < 48; i++) x[i] ^= k48[i];
        var s = [];
        for (var bx = 0; bx < 8; bx++) {
            var j = bx * 6;
            var row = x[j] * 2 + x[j + 5];
            var col = x[j + 1] * 8 + x[j + 2] * 4 + x[j + 3] * 2 + x[j + 4];
            var v = DES_S[bx][row * 16 + col];
            s.push((v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1);
        }
        var f = desPerm(s, DES_P);
        var nr = [];
        for (i = 0; i < 32; i++) nr.push(l[i] ^ f[i]);
        l = r; r = nr;
    }
    var out = desPerm(r.concat(l), DES_FP), res = [];
    for (i = 0; i < 8; i++) {
        var by = 0;
        for (var b = 0; b < 8; b++) by = (by << 1) | out[i * 8 + b];
        res.push(by);
    }
    return res;
}
// VNC auth answer: password (max 8 chars, each byte bit-reversed) as key, two blocks.
function vncAuth(password, ch) {
    var key = [0, 0, 0, 0, 0, 0, 0, 0];
    for (var i = 0; i < 8 && i < password.length; i++) {
        var b = password.charCodeAt(i) & 255, r = 0;
        for (var bit = 0; bit < 8; bit++) r = (r << 1) | ((b >> bit) & 1);
        key[i] = r;
    }
    return desEncrypt(key, ch, 0).concat(desEncrypt(key, ch, 8));
}
// --- DES end ---

// ---- RFB state machine ----
var S_VER = 0, S_SECTYPES = 1, S_SEC33 = 2, S_CHALLENGE = 3, S_SECRESULT = 4, S_INIT = 5,
    S_MSG = 6, S_RECT = 7, S_RAW = 8, S_HEX = 9, S_SKIP = 10, S_FAIL = 99;
var state = S_VER;
var minor = 8;
var fbW = 0, fbH = 0, serverName = "";
var status = "connecting";
var reqW = VW, reqH = VH;    // area asked from the server
var rectsLeft = 0;
var rx = 0, ry = 0, rw = 0, rh = 0, rowsDone = 0;   // current rect
var tx = 0, ty = 0, hexBg = 0, hexFg = 0;           // hextile state
var skipLeft = 0;
var frames = 0, bytesIn = 0, rectPixels = 0;

function fail(msg) { status = "ERR " + msg; state = S_FAIL; console.log("[vnc] " + msg); }

function clientInit() { send([1]); state = S_INIT; }   // shared = 1

function requestUpdate(incremental) {
    var a = [3, incremental ? 1 : 0];
    p16(a, CFG.viewX); p16(a, CFG.viewY); p16(a, reqW); p16(a, reqH);
    send(a);
}

function afterInit() {
    // SetPixelFormat: RGB565 big endian (16 bpp) or RGB332 (8 bpp), true colour.
    if (BPP === 2) send([0, 0, 0, 0, 16, 16, 1, 1, 0, 31, 0, 63, 0, 31, 11, 5, 0, 0, 0, 0]);
    else send([0, 0, 0, 0, 8, 8, 0, 1, 0, 7, 0, 7, 0, 3, 5, 2, 0, 0, 0, 0]);
    var enc = CFG.encoding === "hextile" ? [5, 0] : [0];
    var a = [2, 0];
    p16(a, enc.length);
    for (var i = 0; i < enc.length; i++) p32(a, enc[i]);
    send(a);
    reqW = Math.max(1, Math.min(VW, fbW - CFG.viewX));
    reqH = Math.max(1, Math.min(VH, fbH - CFG.viewY));
    Display.fillRect(0, 0, VW, VH, 0);
    requestUpdate(false);
    state = S_MSG;
    status = "ok";
}

// Draw rows of a Raw rect straight from the input buffer (no copy in JS).
function rawStep() {
    var rowBytes = rw * BPP;
    var rows = Math.min(rh - rowsDone, Math.floor(avail() / rowBytes));
    if (BPP === 1) rows = Math.min(rows, Math.floor(LINE.length / (rw * 2)));
    if (rows <= 0) return false;
    var n = rows * rowBytes;
    var y = ry + rowsDone - CFG.viewY;
    // Rows under the status bar are skipped (TFT_eSPI clips the rest).
    if (y < VH && !menuOpen) {
        var data = BPP === 2 ? BUF.subarray(rp, rp + n) : to565(rp, n);
        Display.pushImage(rx - CFG.viewX, y, rw, Math.min(rows, VH - y), data);
    }
    rp += n;
    rowsDone += rows;
    if (rowsDone >= rh) { rectsLeft--; state = S_RECT; }
    return true;
}

// Bytes one hextile tile needs, or -1 if the header is not here yet.
function hexTileLen(tw, th) {
    if (avail() < 1) return -1;
    var sub = BUF[rp];
    if (sub & 1) return 1 + tw * th * BPP;
    var n = 1;
    if (sub & 2) n += BPP;
    if (sub & 4) n += BPP;
    if (sub & 8) {
        if (avail() < n + 1) return -1;
        var cnt = BUF[rp + n];
        n += 1 + cnt * ((sub & 16) ? BPP + 2 : 2);
    }
    return n;
}

function hexStep() {
    var tw = Math.min(16, rx + rw - tx), th = Math.min(16, ry + rh - ty);
    var need = hexTileLen(tw, th);
    if (need < 0 || avail() < need) return false;
    var sx = tx - CFG.viewX, sy = ty - CFG.viewY;
    var vis = !menuOpen && sx < VW && sy < VH && sx + tw > 0 && sy + th > 0;
    var sub = u8();
    if (sub & 1) {
        var tb = tw * th * BPP;
        if (vis) Display.pushImage(sx, sy, tw, th, BPP === 2 ? BUF.subarray(rp, rp + tb) : to565(rp, tb));
        rp += tb;
    } else {
        if (sub & 2) hexBg = px();
        if (sub & 4) hexFg = px();
        if (vis) Display.fillRect(sx, sy, tw, th, hexBg);
        if (sub & 8) {
            var cnt = u8(), col = (sub & 16) !== 0;
            for (var i = 0; i < cnt; i++) {
                var c = col ? px() : hexFg;
                var xy = u8(), wh = u8();
                if (vis) Display.fillRect(sx + (xy >> 4), sy + (xy & 15), (wh >> 4) + 1, (wh & 15) + 1, c);
            }
        }
    }
    tx += 16;
    if (tx >= rx + rw) { tx = rx; ty += 16; }
    if (ty >= ry + rh) { rectsLeft--; state = S_RECT; }
    return true;
}

// One parse step. Returns false when more bytes are needed.
function step() {
    var n, i;
    switch (state) {
    case S_VER:
        if (avail() < 12) return false;
        var ver = str(12);
        minor = parseInt(ver.substr(8, 3), 10);
        if (minor >= 8) minor = 8; else if (minor !== 7) minor = 3;
        var reply = "RFB 003.00" + minor + "\n", a = [];
        for (i = 0; i < 12; i++) a.push(reply.charCodeAt(i));
        send(a);
        console.log("[vnc] server " + ver.substr(0, 11) + ", using 3." + minor);
        state = minor === 3 ? S_SEC33 : S_SECTYPES;
        return true;
    case S_SECTYPES:
        if (avail() < 1) return false;
        n = BUF[rp];
        if (avail() < 1 + n) return false;
        rp++;
        if (n === 0) { fail("server refused (no security types)"); return false; }
        var types = [];
        for (i = 0; i < n; i++) types.push(u8());
        var pick = 0;
        if (CFG.password && types.indexOf(2) >= 0) pick = 2;
        else if (types.indexOf(1) >= 0) pick = 1;
        else if (types.indexOf(2) >= 0) pick = 2;
        if (!pick) { fail("no supported auth, server offers " + types.join(",")); return false; }
        send([pick]);
        if (pick === 2) state = S_CHALLENGE;
        else if (minor === 8) state = S_SECRESULT;
        else clientInit();
        return true;
    case S_SEC33:
        if (avail() < 4) return false;
        n = u32();
        if (n === 1) clientInit();
        else if (n === 2) state = S_CHALLENGE;
        else { fail("auth type " + n + " not supported"); return false; }
        return true;
    case S_CHALLENGE:
        if (avail() < 16) return false;
        var ch = [];
        for (i = 0; i < 16; i++) ch.push(u8());
        send(vncAuth(CFG.password, ch));
        state = S_SECRESULT;
        return true;
    case S_SECRESULT:
        if (avail() < 4) return false;
        if (u32() !== 0) { fail("authentication failed"); return false; }
        clientInit();
        return true;
    case S_INIT:
        if (avail() < 24) return false;
        var nl = BUF[rp + 20] * 16777216 + ((BUF[rp + 21] << 16) | (BUF[rp + 22] << 8) | BUF[rp + 23]);
        if (avail() < 24 + nl) return false;
        fbW = u16(); fbH = u16();
        rp += 16;                    // server pixel format, ignored (we set our own)
        rp += 4;
        serverName = str(nl);
        console.log("[vnc] desktop " + fbW + "x" + fbH + " '" + serverName + "'");
        afterInit();
        return true;
    case S_MSG:
        if (avail() < 1) return false;
        var t = BUF[rp];
        if (t === 0) {
            if (avail() < 4) return false;
            rp += 2;
            rectsLeft = u16();
            state = S_RECT;
        } else if (t === 1) {      // colour map: not used in true colour, skip it
            if (avail() < 6) return false;
            n = (BUF[rp + 4] << 8) | BUF[rp + 5];
            if (avail() < 6 + n * 6) return false;
            rp += 6 + n * 6;
        } else if (t === 2) {      // bell
            rp++;
        } else if (t === 3) {      // server cut text
            if (avail() < 8) return false;
            n = BUF[rp + 4] * 16777216 + ((BUF[rp + 5] << 16) | (BUF[rp + 6] << 8) | BUF[rp + 7]);
            rp += 8;
            skipLeft = n;
            state = S_SKIP;
        } else {
            fail("unknown message " + t);
            return false;
        }
        return true;
    case S_SKIP:
        n = Math.min(skipLeft, avail());
        if (n <= 0 && skipLeft > 0) return false;
        rp += n; skipLeft -= n;
        if (skipLeft === 0) state = S_MSG;
        return true;
    case S_RECT:
        if (rectsLeft === 0) {
            frames++;
            requestUpdate(!CFG.fullFrames);
            state = S_MSG;
            return true;
        }
        if (avail() < 12) return false;
        rx = u16(); ry = u16(); rw = u16(); rh = u16();
        var enc = s32();
        rectPixels += rw * rh;
        if (rw === 0 || rh === 0) { rectsLeft--; return true; }
        if (enc === 0) { rowsDone = 0; state = S_RAW; }
        else if (enc === 5) { tx = rx; ty = ry; hexBg = 0; hexFg = 0; state = S_HEX; }
        else { fail("encoding " + enc + " not supported"); return false; }
        return true;
    case S_RAW:
        return rawStep();
    case S_HEX:
        return hexStep();
    }
    return false;
}

function processInput() {
    while (state !== S_FAIL && step()) {}
    if (rp === wp) { rp = 0; wp = 0; }
}

// ---- Status bar ----
function drawBar(text) {
    if (menuOpen) return;
    Display.fillRect(0, VH, SW, BAR, 0x0010);
    Display.setTextColor(0xFFFF, 0x0010);
    Display.drawString(text, 2, VH + 1, 2);
    Display.fillRect(SW - 40, VH, 40, BAR, 0x4208);
    Display.setTextColor(0xFFE0, 0x4208);
    Display.drawString("MENU", SW - 36, VH + 1, 2);
}

// ---- Menu (tap the status bar) ----
var menuOpen = false;
var ROW = 30;
var STEP = 120;   // view move per tap, in desktop pixels
function menuItems() {
    return [
        { label: "Back to VNC", act: "close" },
        { label: "Encoding: " + CFG.encoding, act: "enc" },
        { label: "Colours: " + (CFG.colors === 8 ? "256 (8-bit)" : "65k (16-bit)"), act: "col" },
        { label: "Updates: " + (CFG.fullFrames ? "whole view" : "changes only"), act: "upd" },
        { label: "View " + CFG.viewX + "," + CFG.viewY, act: "move" },
        { label: "View to 0,0", act: "home" },
        { label: "Reconnect", act: "recon" },
        { label: "Exit", act: "exit" }
    ];
}
function drawMenu() {
    var items = menuItems();
    Display.fillScreen(0);
    for (var i = 0; i < items.length; i++) {
        var y = i * ROW;
        Display.fillRect(4, y + 2, SW - 8, ROW - 4, items[i].act === "exit" ? 0x8000 : 0x2124);
        Display.setTextColor(0xFFFF, items[i].act === "exit" ? 0x8000 : 0x2124);
        if (items[i].act === "move") {
            Display.drawString(items[i].label, 10, y + 8, 2);
            var arrows = ["<", ">", "^", "v"];
            for (var k = 0; k < 4; k++) {
                Display.fillRect(SW - 4 - (4 - k) * 28, y + 4, 26, ROW - 8, 0x4208);
                Display.setTextColor(0xFFE0, 0x4208);
                Display.drawString(arrows[k], SW - 4 - (4 - k) * 28 + 9, y + 8, 2);
            }
        } else {
            Display.drawString(items[i].label, 10, y + 8, 2);
        }
    }
}
function openMenu() { menuOpen = true; drawMenu(); }
function closeMenu(refresh) {
    menuOpen = false;
    Display.fillRect(0, 0, VW, VH, 0);
    drawBar(status);
    if (refresh && state >= S_MSG) requestUpdate(false);   // repaint the whole view
}
function reconnect() {
    try { ws.close(); } catch (e) {}
    BPP = CFG.colors === 8 ? 1 : 2;
    menuOpen = false;
    Display.fillScreen(0);
    drawBar("VNC: reconnecting ...");
    connect();
}
function menuTap(x, y) {
    var items = menuItems(), i = Math.floor(y / ROW);
    if (i < 0 || i >= items.length) return;
    var act = items[i].act;
    if (act === "close") { closeMenu(true); return; }
    if (act === "exit") { try { ws.close(); } catch (e) {} throw new Error("OS_EXIT"); }
    if (act === "enc") { CFG.encoding = CFG.encoding === "raw" ? "hextile" : "raw"; reconnect(); return; }
    if (act === "col") { CFG.colors = CFG.colors === 8 ? 16 : 8; reconnect(); return; }
    if (act === "recon") { reconnect(); return; }
    if (act === "upd") { CFG.fullFrames = !CFG.fullFrames; drawMenu(); return; }
    if (act === "home") { CFG.viewX = 0; CFG.viewY = 0; viewChanged(); return; }
    if (act === "move") {
        var k = Math.floor((x - (SW - 4 - 4 * 28)) / 28);
        if (k < 0 || k > 3) return;
        if (k === 0) CFG.viewX = Math.max(0, CFG.viewX - STEP);
        if (k === 1) CFG.viewX = Math.min(Math.max(0, fbW - VW), CFG.viewX + STEP);
        if (k === 2) CFG.viewY = Math.max(0, CFG.viewY - STEP);
        if (k === 3) CFG.viewY = Math.min(Math.max(0, fbH - VH), CFG.viewY + STEP);
        viewChanged();
    }
}
function viewChanged() {
    reqW = Math.max(1, Math.min(VW, fbW - CFG.viewX));
    reqH = Math.max(1, Math.min(VH, fbH - CFG.viewY));
    drawMenu();
}

// ---- Connect ----
function connect() {
    var url = "ws://" + CFG.host + ":" + CFG.port + "/";
    console.log("[vnc] connecting " + url);
    ws = new WebSocket(url, "binary");
    ws.binaryType = "arraybuffer";   // native side gives binary frames as Uint8Array
    ws.onopen = function () {
        console.log("[vnc] websocket open");
        rp = 0; wp = 0; state = S_VER; status = "handshake";
    };
    ws.onmessage = function (e) {
        if (typeof e.data === "string") return;
        bytesIn += e.data.length;
        feed(e.data);
    };
    ws.onerror = function (e) { console.log("[vnc] ws error " + (e && e.data)); };
    ws.onclose = function (e) {
        console.log("[vnc] ws closed " + (e && e.code));
        if (state !== S_FAIL) status = "closed";
    };
}

Display.fillScreen(0);
drawBar((VIRT ? VIRT + " " : "") + "VNC: " + CFG.host + ":" + CFG.port + " ...");
connect();

var lastT = System.millis(), lastFrames = 0, lastIn = 0, lastPix = 0;
var touchDown = false, lastPx = 0, lastPy = 0;
var barDown = false, tapX = 0, tapY = 0;

while (true) {
    System.delay(0);   // OS housekeeping + socket pump
    ws.poll();
    processInput();

    // Touch. A short tap in the top-right corner is a normal touch (the kernel exits only on a
    // 0.8 s hold there, or the console "exit"; both throw OS_EXIT, which must end the app).
    var tc = System.getTouch();
    // Menu: a tap (on release) on the status bar opens it; while open, taps go to it.
    if (menuOpen || barDown || (tc.touched && !touchDown && tc.y >= VH)) {
        if (tc.touched) { barDown = true; tapX = tc.x; tapY = tc.y; }
        else if (barDown) {
            barDown = false;
            if (menuOpen) menuTap(tapX, tapY); else openMenu();
        }
        continue;
    }
    if (state >= S_MSG && state <= S_SKIP && (tc.touched || touchDown)) {
        var nx = tc.touched ? tc.x : lastPx, ny = tc.touched ? Math.min(tc.y, VH - 1) : lastPy;
        if (tc.touched !== touchDown || nx !== lastPx || ny !== lastPy) {   // only on change
            lastPx = nx; lastPy = ny;
            var pa = [5, tc.touched ? 1 : 0];
            p16(pa, CFG.viewX + lastPx); p16(pa, CFG.viewY + lastPy);
            send(pa);
        }
        touchDown = tc.touched;
    }

    var now = System.millis();
    if (now - lastT >= 1000) {
        var dt = (now - lastT) / 1000;
        var fps = (frames - lastFrames) / dt;
        var kbs = (bytesIn - lastIn) / dt / 1024;
        var mpx = (rectPixels - lastPix) / dt / 1000;
        var line = status === "ok"
            ? fps.toFixed(1) + " fps  " + kbs.toFixed(0) + " KB/s  " + mpx.toFixed(0) + " kpx/s  " + CFG.encoding
            : status;
        drawBar(line);
        var si = System.getInfo();
        console.log("[vnc] " + line + "  frames=" + frames + " in=" + bytesIn + " out=" + bytesOut +
                    "  ram=" + si.freeRAM + " minram=" + si.minFreeRAM + " maxblock=" + si.maxAllocRAM);
        lastT = now; lastFrames = frames; lastIn = bytesIn; lastPix = rectPixels;
    }
}
