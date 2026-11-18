// Microphone test for the 2.8": top half records 5 s to /sd/media/recordings/rec.wav, bottom half plays it.
// Playback stops while recording (one I2S port), and the screen waits until the recording ends.
// The first time, KryonOS asks for storage permission (the file is outside the app folder): tap Always Allow.
var W = Display.width(), H = Display.height(), FILE = "/sd/media/recordings/rec.wav";
function rgb(r, g, b) { var c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return c; }   // RGB565, bytes swapped as KryonOS JS wants
var RED = rgb(170, 30, 30), GREEN = rgb(30, 130, 60), WHITE = rgb(255, 255, 255), YELLOW = rgb(255, 220, 0);
function draw(msg) {
    Display.fillScreen(0);
    Display.fillRect(0, 0, W, H / 2 - 2, RED);
    Display.fillRect(0, H / 2 + 2, W, H / 2 - 2, GREEN);
    Display.setTextColor(WHITE, RED); Display.drawString("RECORD 5 s", 20, H / 4 - 8, 4);
    Display.setTextColor(WHITE, GREEN); Display.drawString("PLAY", 20, 3 * H / 4 - 8, 4);
    if (msg) { Display.setTextColor(YELLOW, 0); Display.drawString(msg, 4, H / 2 - 8, 2); }
}
draw("");
var down = false, lastY = 0;
while (true) {
    System.delay(10);
    var t = System.getTouch();
    if (t.touched) { down = true; lastY = t.y; continue; }
    if (!down) continue;
    down = false;
    if (lastY < H / 2) {
        draw("recording...");
        var ok = Audio.record(FILE, 5);
        draw(ok ? "saved " + FILE : "record failed");
    } else {
        draw("playing");
        Audio.playWav(FILE);
    }
}
