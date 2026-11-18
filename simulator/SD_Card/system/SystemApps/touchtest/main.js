// Touch test: draws a dot where each touch is read and prints the point; corners are marked.
var W = Display.width(), H = Display.height();
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants
Display.fillScreen(0);
Display.setTextColor(rgb(255, 255, 255), 0);
Display.drawString("Touch the 4 corners and the centre", 10, H / 2 - 30, 2);
var m = [[10, 10], [W - 11, 10], [10, H - 11], [W - 11, H - 11], [W / 2, H / 2]];
for (var i = 0; i < m.length; i++) Display.drawCircle(m[i][0], m[i][1], 8, rgb(0, 255, 0));
var last = 0;
while (true) {
    System.delay(10);
    var t;
    t = System.getTouch();   // the exit (OS_EXIT) must not be caught here
    if (t.touched) {
        Display.fillCircle(t.x, t.y, 3, rgb(255, 0, 0));
        if (System.millis() - last > 300) { console.log("[touch] " + t.x + "," + t.y); last = System.millis(); }
    }
}
