// Deck protocol 4 in JavaScript (ES5, Duktape). docs/04-PROTOCOL.md: frames, COBS, CRC-16.
// Over Wi-Fi: POST /api/deck, text/plain base64 of the COBS frames (KryonOS HTTP is text only).
// Load in an app with: eval(FS.readTextFile("decknet.js"));   (the app's own folder; see deck/main.js)
var DeckNet = (function () {
    var seq = 0;
    function crc16(b, n) {
        var c = 0xFFFF;
        for (var i = 0; i < n; i++) {
            c ^= b[i] << 8;
            for (var k = 0; k < 8; k++) c = (c & 0x8000) ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
        }
        return c;
    }
    // ---- payload writer ----
    function W() { this.a = []; }
    W.prototype.u8 = function (v) { this.a.push(v & 255); return this; };
    W.prototype.u16 = function (v) { this.a.push(v & 255, (v >> 8) & 255); return this; };
    W.prototype.u32 = function (v) { this.a.push(v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255); return this; };
    W.prototype.str = function (s) {
        var b = utf8(s);
        if (b.length > 255) {                              // cut on a character start, never inside a UTF-8 sequence
            var n = 255;
            while (n > 0 && (b[n] & 0xC0) === 0x80) n--;   // b[n] is the first dropped byte; walk back to a lead byte
            b = b.slice(0, n);
        }
        this.a.push(b.length); for (var i = 0; i < b.length; i++) this.a.push(b[i]); return this;
    };
    function utf8(s) {
        var o = [];
        for (var i = 0; i < s.length; i++) {
            var c = s.charCodeAt(i);
            if (c < 0x80) o.push(c);
            else if (c < 0x800) o.push(0xC0 | (c >> 6), 0x80 | (c & 63));
            else o.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        }
        return o;
    }
    function fromUtf8(b, p, n) {
        var s = "", end = p + n, i = p;
        function cont() { return i < end ? (b[i++] & 63) : 0; }   // bounds: a truncated sequence reads as 0
        while (i < end) {
            var c = b[i++], cp;
            if (c < 0x80) s += String.fromCharCode(c);
            else if (c >= 0xF0) {                                  // 4 bytes: surrogate pair in the JS string
                cp = ((c & 7) << 18) | (cont() << 12) | (cont() << 6) | cont();
                if (cp < 0x10000 || cp > 0x10FFFF) s += "�";
                else { cp -= 0x10000; s += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)); }
            }
            else if (c >= 0xE0) s += String.fromCharCode(((c & 15) << 12) | (cont() << 6) | cont());
            else if (c >= 0xC0) s += String.fromCharCode(((c & 31) << 6) | cont());
            else s += "�";                                   // stray continuation byte
        }
        return s;
    }
    // ---- payload reader ----
    function R(b, p, end) { this.b = b; this.p = p; this.end = end; }
    R.prototype.left = function () { return this.end - this.p; };
    R.prototype.u8 = function () { return this.b[this.p++]; };
    R.prototype.u16 = function () { var v = this.b[this.p] | (this.b[this.p + 1] << 8); this.p += 2; return v; };
    R.prototype.u32 = function () { var b = this.b, p = this.p; this.p += 4; return (b[p] | (b[p + 1] << 8) | (b[p + 2] << 16)) + b[p + 3] * 16777216; };
    R.prototype.f32 = function () {
        var bits = this.u32(), sign = bits >>> 31 ? -1 : 1, e = (bits >>> 23) & 255, m = bits & 0x7FFFFF;
        if (e === 255) return m ? NaN : sign * Infinity;
        if (e === 0) return sign * m * Math.pow(2, -149);
        return sign * (1 + m / 8388608) * Math.pow(2, e - 127);
    };
    R.prototype.str = function () { var n = this.u8(), s = fromUtf8(this.b, this.p, n); this.p += n; return s; };
    R.prototype.str16 = function () { var n = this.u16(), s = fromUtf8(this.b, this.p, n); this.p += n; return s; };
    // ---- frame + COBS ----
    function frame(type, payload) {
        var n = payload.length, f = [0x40, 0, type, 0, seq & 255, (seq >> 8) & 255, n & 255, (n >> 8) & 255];
        seq = (seq + 1) & 0xFFFF;
        for (var i = 0; i < n; i++) f.push(payload[i]);
        var c = crc16(f, f.length); f.push(c & 255, c >> 8);
        return f;
    }
    function cobs(src) {
        var out = [0], code = 1, ci = 0;
        for (var i = 0; i < src.length; i++) {
            if (src[i] === 0) { out[ci] = code; ci = out.length; out.push(0); code = 1; }
            else { out.push(src[i]); if (++code === 0xFF) { out[ci] = code; ci = out.length; out.push(0); code = 1; } }
        }
        out[ci] = code; out.push(0);
        return out;
    }
    function uncobs(b, p, end) {   // b[p..end) without the final 0x00
        var out = [];
        while (p < end) {
            var code = b[p++];
            for (var i = 1; i < code && p < end; i++) out.push(b[p++]);
            if (code < 0xFF && p < end) out.push(0);
        }
        return out;
    }
    // ---- base64 ----
    var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    function b64enc(a) {
        var s = "";
        for (var i = 0; i < a.length; i += 3) {
            var n = (a[i] << 16) | ((a[i + 1] || 0) << 8) | (a[i + 2] || 0);
            s += B64.charAt(n >> 18) + B64.charAt((n >> 12) & 63) +
                (i + 1 < a.length ? B64.charAt((n >> 6) & 63) : "=") + (i + 2 < a.length ? B64.charAt(n & 63) : "=");
        }
        return s;
    }
    function b64dec(s) {
        var o = [], buf = 0, bits = 0;
        for (var i = 0; i < s.length; i++) {
            var v = B64.indexOf(s.charAt(i));
            if (v < 0) continue;
            buf = (buf << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; o.push((buf >> bits) & 255); }
        }
        return o;
    }
    // ---- messages (0x4E): the payload is checked before each read; a truncated list is dropped whole ----
    function have(r, n) { return r.left() >= n; }
    function messagesPayload(r) {
        var out = [], k;
        if (!have(r, 1)) return null;
        k = r.u8();
        while (k-- > 0) {
            if (!have(r, 10)) return null;                       // u32 id, u32 time, u8 kind, u8 flags
            var id = r.u32(), ts = r.u32(), kind = r.u8(), flags = r.u8();
            if (!have(r, 1) || !have(r, 1 + r.b[r.p])) return null;   // str from
            var from = r.str();
            if (!have(r, 2) || !have(r, 2 + (r.b[r.p] | (r.b[r.p + 1] << 8)))) return null;   // str16 text
            var text = r.str16();
            out.push({ id: id, from: from, text: text, ts: ts, read: (flags & 1) === 1, voice: kind === 1 });
        }
        return out;
    }
    // ---- decode one frame into a message ----
    var NAMES = { 0x40: "host_hello", 0x41: "ping", 0x42: "stats", 0x43: "config", 0x48: "wifi_token", 0x4E: "messages", 0x7F: "error" };
    function decode(f) {
        if (f.length < 10 || (f[0] & 0xF0) !== 0x40) return null;
        var n = f[6] | (f[7] << 8);
        if (f.length < 10 + n) return null;
        var crc = f[8 + n] | (f[9 + n] << 8);
        if (crc16(f, 8 + n) !== crc) return { type: "damaged" };
        var t = f[2], r = new R(f, 8, 8 + n), m = { type: NAMES[t] || ("0x" + t.toString(16)), code: t };
        if (t === 0x40) { m.protocol = r.u8(); m.statsMs = r.u16(); var k = r.u8(); m.windows = []; while (k--) m.windows.push(r.u8()); m.time = r.u32(); }
        else if (t === 0x42) { m.seconds = r.u32(); m.day = r.u8(); m.month = r.u8(); m.values = []; for (var i = 0; i < 19 && r.left() >= 4; i++) m.values.push(r.f32()); }
        else if (t === 0x43) { var c = r.u8(); m.macros = []; while (c--) m.macros.push({ key: r.u8(), label: r.str() }); }
        else if (t === 0x4E) { m.messages = messagesPayload(r); if (!m.messages) return null; }
        else if (t === 0x7F) { m.errorCode = r.u8(); m.detail = r.str(); }
        return m;
    }
    function parse(bytes) {
        var out = [], start = 0;
        for (var i = 0; i < bytes.length; i++) {
            if (bytes[i] === 0) { if (i > start) { var m = decode(uncobs(bytes, start, i)); if (m) out.push(m); } start = i + 1; }
        }
        return out;
    }
    var STATS = ["CPU %", "CPU C", "System C", "RAM %", "RAM used GB", "RAM total GB", "GPU %", "VRAM %",
        "VRAM used GB", "VRAM total GB", "GPU C", "GPU MHz", "GPU W", "Disk %", "Disk read MB/s",
        "Disk write MB/s", "Disk C", "Net down Mbit/s", "Net up Mbit/s"];
    return {
        W: W, frame: frame, cobs: cobs, parse: parse, decode: decode, b64enc: b64enc, b64dec: b64dec, crc16: crc16, STATS: STATS,
        hello: function (id, fw, board) { return frame(0x01, new W().u8(4).str(id).str(fw).str(board).a); },
        statsRequest: function (id) { return frame(0x04, new W().str(id).a); },
        configRequest: function () { return frame(0x05, []); },
        macro: function (key, id) { return frame(0x06, new W().u8(key).str(id).a); },
        // newest: the newest message id the Deck has (0 none); the answer holds the messages after it.
        messagesRequest: function (id, newest) { return frame(0x10, new W().str(id).u32(newest).a); },
        messageRead: function (id, msg) { return frame(0x11, new W().str(id).u32(msg).a); },
        // Send frames over Wi-Fi; post(url, body, type, headers) is Network.post (or a stand-in on the PC).
        exchange: function (post, base, token, frames) {
            var all = [];
            for (var i = 0; i < frames.length; i++) { var c = cobs(frames[i]); for (var j = 0; j < c.length; j++) all.push(c[j]); }
            var res = post(base + "/api/deck", b64enc(all), "text/plain", { "Authorization": "Bearer " + token });
            if (!res || res.status !== 200) return { error: res ? (res.status + " " + (res.error || res.body)) : "no answer" };
            return { messages: parse(b64dec(res.body)) };
        }
    };
})();
