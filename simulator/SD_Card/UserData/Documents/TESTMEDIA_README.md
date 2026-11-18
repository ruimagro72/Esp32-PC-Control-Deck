# Test media for the Deck (KryonOS port), 12 Nov 2026

Copy to the card with a card reader (serial `put` is ~10 KB/s: fine for small files only).
Card layout used by the tests: `/sd/media/images/{bmp,jpg,png,gif}`, `/sd/media/music/{mp3,modules}`.

| File | Board result (12 Nov) |
|---|---|
| jpg/landscape_320x240_baseline.jpg | OK, Display.drawJPG |
| jpg/large_1024x768.jpg | OK, ~1 s |
| jpg/landscape_320x240_progressive.jpg | FAILS: TJpg_Decoder has no progressive JPEG |
| png/*.png (8-bit RGB, RGBA, palette) | OK, Display.drawPNG |
| 16-bit PNG (ImageMagick default from gradients) | FAILS: PNGdec takes 8-bit only; save with `-depth 8` |
| gif/*.gif | FAILS: no GIF decoder in the kernel |
| bmp/*.bmp | not tested yet (Display.drawBMP) |
| music/mp3/clip_10s_22k_mono_48k.mp3 | PLAYS, Speaker.play (minimp3 in the kernel) |
| music/mp3/full tracks, 44.1 kHz stereo | not tested yet |
| music/modules/*.mod | JS modplayer renders to WAV first (to replace by a native mixer) |
| music/modules/*.it | not supported |
