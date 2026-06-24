# scanline_sim — offline validator for composable scanline token streams

A small host program that reproduces what the `video_24mhz_composable` PIO state
machine (`pico-extras .../pico_scanvideo/scanvideo.pio`) does when it walks a
scanline's token stream. It lets you validate a pattern's output — and preview it
as ASCII art — **without flashing the Pico**.

## Why

The firmware renders video with no framebuffer: each scanline is a run-length
stream of 16-bit "composable" tokens (`COLOR_RUN`, `RAW_RUN`, `RAW_1P`, `RAW_2P`,
`EOL_*`). A miscounted run, a misplaced end-of-line marker, or a line that isn't
exactly the display width will **desync the timing PIO** and corrupt the picture
— often as a stray coloured bar across the screen. These bugs are slow to chase
on hardware. This sim catches them in a second.

It was written while debugging the 8×8-font `draw_text()` pattern, where an
un-clipped string produced an over-long scanline (the classic "blue bar" desync).

## What it checks

`scanline_sim.c` contains:

- **A faithful PIO interpreter** (`pio_run`) that consumes a token stream exactly
  like the hardware: `COLOR_RUN` → `N` pixels (count = `N-3`); `RAW_RUN` → one
  colour-1 token, then `n+2` colour tokens (count = `n = N-3`), then a following
  opcode; `RAW_1P`/`RAW_2P` → 1/2 pixels. It reports the pixel count and flags a
  **desync** if the stream runs out mid-token or a payload value lands where an
  opcode was expected.
- **A token generator** (`gen_draw_text`) that mirrors firmware `draw_text()` in
  `../test_pattern.c`, reusing the real `../font8x8.h` so glyph data never drifts.
- A **width sweep** that asserts every scanline consumes cleanly and covers the
  active region (accepting both trailing-black conventions: `width` like
  `draw_bitmap`, or `width+1` like `draw_color_line`).
- An **ASCII-art preview** so you can eyeball the rendered glyphs and clipping.

## Build & run

Pure host C — no Pico SDK required:

```bash
cc -Wall -o scanline_sim scanline_sim.c && ./scanline_sim
```

Expected: `... 0 failures -> all OK`, followed by readable previews of
`HELLO PICO 0123` at full width and clipped to a narrow display.

## Extending it

To validate another pattern, write a `gen_<pattern>()` that emits the same tokens
as the firmware handler in `../test_pattern.c`, then feed it to `pio_run()` and
check `!desync` plus the pixel count. The `emit_run()` / `emit_eol()` helpers
match the firmware's, so most handlers port over almost line-for-line.

> Keep `gen_draw_text()` in sync with firmware `draw_text()`. If you change the
> firmware's token layout, mirror it here (or the sim will validate stale logic).
