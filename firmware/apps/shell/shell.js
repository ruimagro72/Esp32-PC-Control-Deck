// The Deck's main screen (the shell, like explorer.exe). Runs after /sd/autoexec.js and again
// each time an app closes. First version: one button per app, plus System (KryonOS's launcher,
// the safe mode). It will become the Deck's pages, macros and server status.
var W = Display.width(), H = Display.height();
var BAR = 28, ROW = 38;
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants

function apps() {
    var roots = ["/sd/UserApps/", "/local/apps"], found = [], key = "", r, i;
    for (r = 0; r < roots.length; r++) {
        var items = [];
        try { items = FS.listDir(roots[r]) || []; } catch (e) {}
        for (i = 0; i < items.length; i++) { found.push(items[i]); key += items[i] + ";"; }
    }
    // The titles come from each app's app.json (about 40 files): read them once and keep them in a cache that is
    // valid while the folder list is unchanged, so the shell starts fast.
    var CACHE = "/sd/tmp/apps.cache";
    try { var c = JSON.parse(FS.readTextFile(CACHE)); if (c && c.key === key && c.list) return c.list; } catch (e) {}
    var list = [];
    for (i = 0; i < found.length; i++) {
        var name = found[i].substring(found[i].lastIndexOf("/") + 1);
        if (!name || name === "deckshell" || !FS.exists(found[i] + "/main.js")) continue;   // deckshell: the launcher's way back here; no main.js: not an app
        var title = name;
        try { var aj = JSON.parse(FS.readTextFile(found[i] + "/app.json")); if (aj && aj.name) title = aj.name; } catch (e) {}
        list.push({ name: title, path: found[i] + "/main.js" });
    }
    try { FS.writeTextFile(CACHE, JSON.stringify({ key: key, list: list })); } catch (e) {}
    return list;
}

var list = apps(), page = 0, perPage = Math.floor((H - BAR - ROW) / ROW);

function draw() {
    Display.fillScreen(0);
    Display.fillRect(0, 0, W, BAR, rgb(30, 70, 160));
    Display.setTextColor(rgb(255, 255, 255), rgb(30, 70, 160));
    Display.drawString("Deck", 8, 6, 4);
    var info = System.getInfo();
    Display.drawString(Math.round(info.freeRAM / 1024) + " KB", W - 70, 8, 2);
    for (var i = 0; i < perPage; i++) {
        var k = page * perPage + i;
        if (k >= list.length) break;
        var y = BAR + 4 + i * ROW;
        Display.fillRect(6, y, W - 12, ROW - 6, rgb(70, 80, 100));
        Display.setTextColor(rgb(255, 255, 255), rgb(70, 80, 100));
        Display.drawString(list[k].name, 14, y + 7, 2);
    }
    var by = H - ROW + 2;
    Display.fillRect(6, by, 66, ROW - 6, rgb(90, 90, 90)); Display.setTextColor(rgb(255, 220, 0), rgb(90, 90, 90));
    Display.drawString("<", 34, by + 7, 2);
    Display.fillRect(84, by, 66, ROW - 6, rgb(90, 90, 90)); Display.setTextColor(rgb(255, 220, 0), rgb(90, 90, 90));
    Display.drawString(">", 112, by + 7, 2);
    Display.setTextColor(rgb(160, 160, 160), 0); Display.drawString((page + 1) + "/" + Math.max(1, Math.ceil(list.length / perPage)), 160, by + 9, 2);
    Display.fillRect(W - 86, by, 80, ROW - 6, rgb(170, 30, 30)); Display.setTextColor(rgb(255, 255, 255), rgb(170, 30, 30));
    Display.drawString("System", W - 74, by + 7, 2);
}

draw();
var down = false, tx = 0, ty = 0;
while (true) {
    System.delay(20);
    var t = System.getTouch();
    if (t.touched) { down = true; tx = t.x; ty = t.y; continue; }
    if (!down) continue;
    down = false;
    if (ty >= H - ROW) {
        if (tx >= W - 86) break;                         // System: leave the shell (safe mode)
        if (tx < 76) {                                    // previous page, wraps
            page = page > 0 ? page - 1 : Math.max(0, Math.ceil(list.length / perPage) - 1);
            draw();
        } else if (tx >= 80 && tx < 156) {              // next page, wraps
            page = (page + 1) * perPage < list.length ? page + 1 : 0;
            draw();
        }
        continue;
    }
    var k = page * perPage + Math.floor((ty - BAR - 4) / ROW);
    if (ty > BAR && k >= 0 && k < list.length) System.launch(list[k].path);
}
