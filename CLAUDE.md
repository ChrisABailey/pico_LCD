# Pico LCD Test Pattern Generator — CLAUDE.md

## Project Overview
LCD test pattern generator for Raspberry Pi Pico 2 (RP2350) driving a parallel TTL LCD interface.
Uses the `pico-extras` scanvideo library for real-time, dual-core scanline generation without a framebuffer.
Version: **1.1.1**

---

## Build System

CMake project. Requires:
- `PICO_SDK_PATH` environment variable pointing to the Pico SDK
- `pico-extras` checked out alongside this repo (provides `pico_scanvideo_dpi`)

Target platform/board flags (in `.vscode/settings.json` or passed to CMake):
```json
{
    "cmake.configureArgs": [
        "-DPICO_PLATFORM=rp2350",
        "-DPICO_BOARD=pico2"
    ]
}
```

Build output is a `copy_to_ram` binary (program loaded into RAM at startup).

Key compile defines in `test_pattern/CMakeLists.txt`:
- `PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1` — outputs pixel clock on GPIO
- `PICO_SCANVIDEO_ENABLE_DEN_PIN=1` — outputs data enable signal

---

## Key Files

| File | Description |
|------|-------------|
| `test_pattern/test_pattern.c` | Main application — all pattern logic, serial UI, flash persistence |
| `test_pattern/CMakeLists.txt` | Build config |
| `test_pattern/text.h` | 188×98px bitmap array (BGRA5515 format) for the text pattern |
| `test_pattern/userio.h` | Serial input helpers: `mygetchar()`, `getString()`, `getFloat()`, `getInt()` |
| `test_pattern/vcocalc.py` | PLL calculator: given target MHz → REFDIV/FBDIV/PD1/PD2 params |
| `pinout.png` | GPIO pinout diagram for the LCD interface |

---

## Hardware

- **MCU**: Raspberry Pi Pico 2 (RP2350)
- **Interface**: Parallel TTL LCD (driven via PIO scanvideo)
- **GPIO 28**: Cycle button — advances to next pattern on press
- **USB**: Serial terminal for interactive control and debug output

---

## Display Timing Modes

| Name | Resolution | Pixel Clock | Notes |
|------|-----------|-------------|-------|
| DCDU | 480×234 @ 60Hz | 7.8 MHz | Default startup mode |
| PSP  | 480×272 @ 60Hz | 10.0 MHz | |
| AY   | 768×256 @ 60Hz | 14.375 MHz | |
| VGA  | 640×480 @ 60Hz | 25.175 MHz | From Pico SDK default |

Timing is persisted to flash. On boot, saved timing is restored; if none saved, DCDU is used.

---

## Test Patterns

16 patterns defined in the `Pattern` enum:

| Key | Pattern | Description |
|-----|---------|-------------|
| `space` / button | — | Cycle to next pattern |
| `r` | red | Solid red field |
| `g` | green | Solid green field |
| `b` | blue | Solid blue field |
| `c` | cyan | Solid cyan field |
| `m` | magenta | Solid magenta field |
| `y` | yellow | Solid yellow field |
| `w` | white | Solid white field |
| `k` | black | Solid black field |
| `u` | custom | User-defined color (5-bit RGB) |
| `n` | — | Enter new user color interactively (then auto-selects custom) |
| `s` | bars | 8 horizontal color bars |
| `e` | border | White outer + red inner single-pixel border on black |
| `t` | text | Bitmap text (from `text.h`) rendered at position (15, 15) |
| `a` | animate | Bouncing colored rectangle |
| `3` / `q` | grey | 32 greyscale shade bars |
| `l` | lines | 5×5 grid of white lines |
| `x` | box | Centered white box on black |
| `p` | — | Program new video timing (interactive menu) |
| `z` | — | Print current video clock timing to serial |
| `?` / other | — | Print help |

---

## Flash Persistence

Settings stored at flash offset `0x40000` (256 KB):
- **Page 0** (`+0B`): `scanvideo_timing_t` + magic flag `0x1DCCAB1`
- **Page 1** (`+256B`): `scanvideo_mode_t` + `sysClkKHz`

Key functions:
- `update_flash_settings()` — writes timing/mode to flash, then reboots via watchdog
- `get_flash_settings()` — reads and validates flash; returns false if no valid settings
- `clear_custom_timing()` — erases saved settings (option `6` in `p` menu)

Flash writes use `flash_safe_execute()` to safely pause Core 1 rendering.

---

## Dual-Core Architecture

- **Core 0**: Serial/USB input processing, button polling, UI menus, flash operations
- **Core 1**: `render_graphics()` — infinite scanline rendering loop

Synchronization:
- `sem_t video_initted` — Core 1 signals Core 0 when scanvideo is initialized
- `volatile bool wait_for_flash` — Core 0 sets this to pause Core 1 during flash writes

Time-critical note: `draw_bitmap()` is decorated with `__time_critical_func` to ensure it runs from RAM and avoids flash access latency during scanline generation.

---

## Bitmap Encoding

To add a new bitmap image:
```bash
./flash_stream/img/packtiles -sf bgar5515 <input.png> <output.h>
```
Then update the `text_width` and `text_height` macros in the generated header.

Format is BGRA5515 (5 bits per channel, 16-bit pixels).

---

## PLL / System Clock Calculator

```bash
cd test_pattern
python3 vcocalc.py <target_MHz>         # show PLL params
python3 vcocalc.py --cmake <target_MHz> --cmake-executable-name test_pattern
```

`SysClkForPixClk()` in `test_pattern.c` maps known pixel clock frequencies to system clock (kHz).

---

## Scanvideo Library — How Video Generation Works

The video pipeline is implemented across two pico-extras libraries:

| Library | Path | Role |
|---------|------|------|
| `pico_scanvideo` | `pico-extras/src/common/pico_scanvideo/` | Platform-independent API, composable format, VGA modes |
| `pico_scanvideo_dpi` | `pico-extras/src/rp2_common/pico_scanvideo_dpi/` | RP2xxx implementation: PIO programs, DMA, IRQs |

Key source files:
- `scanvideo.c` — full implementation (~1873 lines): setup, IRQs, DMA, buffer management
- `scanvideo.pio` — PIO program that outputs pixel data from the scanline token stream
- `timing.pio` — PIO program that generates hsync, vsync, DEN, and pixel clock signals
- `composable_scanline.h` — COMPOSABLE_* macros for encoding scanline data
- `scanvideo_base.h` — struct definitions and core API declarations

### PIO State Machines

Four PIO state machines run simultaneously:

| SM | Program | Role |
|----|---------|------|
| SM0 | `video_24mhz_composable` (scanvideo.pio) | Reads token stream via DMA, outputs pixel data to color pins |
| SM3 | `video_htiming` (timing.pio) | Generates hsync/vsync pulses, DEN, pixel clock, and sync IRQs |
| SM1/SM2 | (optional) | Additional planes when `PICO_SCANVIDEO_PLANE_COUNT > 1` |

**scanvideo.pio** — SM0 processes the composable token stream:
- Waits for IRQ 4 from the timing SM at the start of each active scanline
- Jumps to token handlers (`color_run`, `raw_run`, `raw_1p`, etc.) via `out pc, 16`
- Outputs 16-bit pixel values to the color GPIO pins every N clock cycles
- `xscale` controls pixel doubling — built-in delay instructions stretch each pixel

**timing.pio** — SM3 drives all timing signals:
- Outputs encoded state words fed by the CPU to produce sync pulses
- Sideband pin toggles produce the pixel clock (`clock_polarity` controls edge)
- IRQs coordinate with SM0: IRQ 4 = start active scanline, IRQ 0 = prepare DMA, IRQ 1 = vblank

### Composable Scanline Format

Scanline data is run-length encoded as a stream of 16-bit tokens in a `uint32_t` buffer. Each pair of tokens forms one 32-bit word for DMA efficiency.

| Token Type | Encoding | Use |
|-----------|----------|-----|
| `COMPOSABLE_COLOR_RUN` | `cmd \| color \| (N-3)` | N identical pixels — most efficient for solid fills |
| `COMPOSABLE_RAW_RUN` | `cmd \| color1 \| (N-3) \| color2 \| ... \| color(N-1)` | N different pixels — count = N-3 (same as COLOR_RUN). Loop provides pixels 2..(N-1); the PIO falls through to `raw_1p` which outputs pixel N before jumping to the next token. Last loop index is `i < N-1`, not `i < N`. |
| `COMPOSABLE_RAW_1P` | `cmd \| color` | Single pixel |
| `COMPOSABLE_RAW_2P` | `cmd \| color1 \| color2` | Two pixels |
| `COMPOSABLE_EOL_ALIGN` | must be in high word | End of line when token count is odd |
| `COMPOSABLE_EOL_SKIP_ALIGN` | must be in low word | End of line when token count is even |

**Critical rules when writing patterns:**
1. N in COLOR_RUN and RAW_RUN is the actual count **minus 3** (minimum run = 3 pixels)
2. The last pixel before EOL must be black (color = 0) to prevent color bleed into blanking
3. EOL alignment must match the current word boundary — use `EOL_ALIGN` vs `EOL_SKIP_ALIGN` accordingly
4. Set `buf->data_used` to the number of 32-bit words written (token count ÷ 2, rounded up)
5. Set `buf->status = SCANLINE_OK` before calling `scanvideo_end_scanline_generation()`

### Pixel Color Format

5-bit per channel (5:5:5 RGB) packed into a 16-bit word:

```
Bit: [15..11]  [10..6]   [5..1]   [0]
      Blue      Green     Red     Alpha
```

Helper macros:
```c
PICO_SCANVIDEO_PIXEL_FROM_RGB5(r5, g5, b5)   // 0-31 per channel
PICO_SCANVIDEO_PIXEL_FROM_RGB8(r8, g8, b8)   // 0-255 per channel, scaled down
```

### Key API Functions

```c
// Initialize video hardware for a given mode
scanvideo_setup(const scanvideo_mode_t *mode);

// Start/stop PIO state machines and IRQs
scanvideo_timing_enable(bool enable);

// Acquire a free scanline buffer to fill (blocks until one is available)
scanvideo_scanline_buffer_t *scanvideo_begin_scanline_generation(bool block);

// Submit the filled buffer for display
scanvideo_end_scanline_generation(scanvideo_scanline_buffer_t *buf);

// Block until vertical blanking interval (60Hz sync point)
scanvideo_wait_for_vblank();

// Get current scanline number from a scanline_id
uint16_t line = scanvideo_scanline_number(buf->scanline_id);
```

### scanvideo_timing_t Fields

```c
typedef struct scanvideo_timing {
    uint32_t clock_freq;       // Pixel clock in Hz (e.g. 7800000 for DCDU)
    uint16_t h_active;         // Active pixels per line
    uint16_t v_active;         // Active lines per frame
    uint16_t h_front_porch;    // Pixels from end of active to hsync start
    uint16_t h_pulse;          // Hsync pulse width in pixels
    uint16_t h_total;          // Total clocks per line (active + fp + pulse + bp)
    uint8_t  h_sync_polarity;  // 1 = active high
    uint16_t v_front_porch;    // Lines from end of active to vsync start
    uint16_t v_pulse;          // Vsync pulse width in lines
    uint16_t v_total;          // Total lines per frame
    uint8_t  v_sync_polarity;  // 1 = active high
    uint8_t  enable_clock;     // 1 = output pixel clock (needs PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1)
    uint8_t  clock_polarity;   // 0 = rising edge, 1 = falling edge
    uint8_t  enable_den;       // 1 = output DEN signal (needs PICO_SCANVIDEO_ENABLE_DEN_PIN=1)
} scanvideo_timing_t;
```

### scanvideo_mode_t Fields

```c
typedef struct scanvideo_mode {
    const scanvideo_timing_t *default_timing;  // Timing parameters
    const scanvideo_pio_program_t *pio_program; // Always &video_24mhz_composable
    uint16_t width;              // Logical pixel width
    uint16_t height;             // Logical pixel height
    uint8_t  xscale;             // Horizontal pixel repeat (1 = none, 2 = double)
    uint16_t yscale;             // Vertical line repeat
    uint16_t yscale_denominator; // Fractional yscale divisor (usually 1)
} scanvideo_mode_t;
```

### System Clock Requirement

When `PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1` (as used here), the system clock must be an **even integer multiple** of the pixel clock. The PIO uses its clock divider to derive the pixel rate from the system clock.

`SysClkForPixClk()` in `test_pattern.c` handles the mapping for known pixel clock values. If adding a new timing mode, either add a case there or use `vcocalc.py` to find a valid system clock.

### Render Loop Pattern (Core 1)

```c
void render_graphics() {
    // scanvideo_setup() and scanvideo_timing_enable() called here
    sem_release(&video_initted);  // signal Core 0

    while (true) {
        scanvideo_scanline_buffer_t *buf = scanvideo_begin_scanline_generation(true);
        uint16_t line = scanvideo_scanline_number(buf->scanline_id);

        // write COMPOSABLE_* tokens into buf->data[]
        // set buf->data_used (word count) and buf->status = SCANLINE_OK

        scanvideo_end_scanline_generation(buf);
    }
}
```
