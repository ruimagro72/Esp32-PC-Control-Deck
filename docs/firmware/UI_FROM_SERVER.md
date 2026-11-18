# Deck UI from the server

Status: **proposal, nothing built** (state of 18 Nov 2026). Roadmap item: 07-FEATURES-AND-ROADMAP.md (not in this branch), section 3.
The earlier analysis is in (archived, not in this branch).

The Deck's interface (theme, screens, layouts) is defined on the server per Deck type and per Deck, and travels in
protocol 4 as binary, compressed; no JSON on the card. The visual design is the Stitch work (25 screens, 320x240 and
480x320; see (archived, not in this branch)).

## 1. Who does what

| Part | Where | Why |
|---|---|---|
| Design (colours, fonts, sizes, screens) | server, from the Stitch design | one place to change; every Deck follows |
| **Layout** (flex columns/rows -> pixel rectangles) | **server**, per panel size | the Deck gets absolute rectangles: no layout engine on the device (close to the metal) |
| Drawing widgets (gauge, bar, button, row, chart, text) | **kernel, native C++** | per-pixel work is native (the VNC and MOD lesson) |
| Navigation, taps, data binding, animations of values | JS (the shell) | logic in JS, small and changeable from the card |
| Live values | the existing stats and catalog frames | a widget is bound to a metric id or an action id |

## 2. Transfer and cache

- hello -> host_hello carries `ui_version` (u32 hash of the pack for this Deck).
- The Deck keeps the last pack in `/sd/var/ui/` (binary, as received). Same version: nothing is sent. Different:
  the Deck asks `ui_request`; the server answers `ui_pack` in parts (like the existing pack transfer),
  **deflate-compressed** (the ESP32 ROM has `tinfl` to inflate).
- Fonts (Montserrat as TFT_eSPI smooth fonts, `.vlw`) and icons are files in the pack, fetched once, kept on the card.

## 3. The pack (binary, little endian, TLV sections)

```
header : "DUI1" u32 version u16 panelW u16 panelH u8 sections
THEME  : u8 nColours [u16 RGB565]...          palette, referenced by index (Stitch tokens)
         u8 nFonts   [u8 fileId u8 sizePx]... referenced by index (label-sm, body-sm, ...)
         metrics: u8 header u8 footer u8 pad u8 gap u8 radiusCard u8 radiusRow
STRINGS: u16 n [str16]...                      every text, by id (the server's texts, per language)
ASSETS : u8 n [u8 id u8 kind str name u32 size] fonts and icons (fetched as files)
SCREEN : u8 id str16 name u8 nWidgets [widget]...
widget : u8 type u16 x u16 y u16 w u16 h u8 style  u8 nProps [u8 key value]...
         type: 1 label 2 value 3 gauge 4 bar 5 button 6 row 7 chart 8 tabs 9 icon 10 panel
         props: text (string id), metric (catalog id), action (0 none 1 screen 2 macro
                3 console 4 app), target, min/max, unit (string id), colour rules
STYLE  : u8 n [style]: fill colour, gradient to, border colour, border px, radius, font, text colour
```

A gauge of the Monitor screen: `type 3, rect, style "card", props: metric=cpu_percent, max=100, colour rules
(green <70, amber <85, red)`.

## 4. What the kernel needs (native, one flash when ready)

- `Display.drawSmoothArc`, `fillGradientRect` (vertical/horizontal), `fillSmoothRoundRect`, smooth fonts from the
  card (`loadFont` .vlw), alpha blend for the glows.
- `UI.load(pack)`, `UI.show(screenId)`, `UI.set(widgetId, value)` (redraws only that widget), `UI.hit(x, y)` ->
  widget id and action. JS does the rest.

None of these exist yet.

## 5. What stays out (the board's limits)

- Shadows and glows: faked with 1 px lighter or darker edges and a pre-blended halo colour, not real blur (blur per
  pixel is too slow over SPI).
- Animations: value glides (300 ms) on gauges, bars and numbers only; screen fades as a quick redraw. No full-screen
  alpha fades.

## 6. First step proposed

Theme and the **Monitor** screen end to end at 320x240: the server builds the pack, sends it, the Deck draws the
gauges with live CPU, RAM and GPU values. Then the other 24 screens are data.
