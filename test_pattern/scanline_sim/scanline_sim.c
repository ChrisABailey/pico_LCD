/*
 * scanline_sim.c — host-side simulator for the pico-extras "composable" scanline
 * token format used by this project's video patterns.
 *
 * Why this exists
 * ---------------
 * The Pico renders video with no framebuffer: each scanline is a stream of 16-bit
 * "composable" tokens that a PIO state machine (video_24mhz_composable in
 * scanvideo.pio) executes to drive the colour pins.  Getting the token stream
 * wrong — a miscounted run, a misplaced EOL, an over-long line — desyncs the
 * timing PIO and corrupts the display (a tell-tale symptom is a stray coloured
 * bar streaking across the screen).  Those bugs are painful to chase on hardware.
 *
 * This program reproduces, on the host, exactly what the PIO does when it walks a
 * token stream, so a pattern's output can be validated and even previewed as
 * ASCII art before flashing.  It models the four token handlers we use:
 *
 *     COLOR_RUN  | color | (N-3)                      -> N pixels of `color`
 *     RAW_RUN    | c1 | (N-3) | c2 .. c(N-1)          -> N pixels, one colour each
 *     RAW_1P     | color                              -> 1 pixel
 *     RAW_2P     | color | color                      -> 2 pixels
 *     EOL_ALIGN / EOL_SKIP_ALIGN                      -> end of line
 *
 * PIO consumption (traced from scanvideo.pio, video_24mhz_composable_default):
 *   raw_run:  out pins (c1); out x (=n); pixel_loop runs n+1 times (out pins each);
 *             falls through to raw_1p (out pins) then `out pc` reads the next
 *             opcode.  => n+2 colour tokens follow the count field, then a valid
 *             opcode token.  Total pixels = n+3 = N.
 *   color_run: out pins (colour) once; a delay loop holds that pin value for the
 *             run, so the count field is N-3 and the run is N pixels.
 *
 * The token generator gen_draw_text() mirrors firmware draw_text() in
 * ../test_pattern.c — keep the two in sync if you change the firmware.  It reuses
 * the real ../font8x8.h so glyph data never diverges.
 *
 * Build & run:
 *     cc -o scanline_sim scanline_sim.c && ./scanline_sim
 * (no Pico SDK needed — pure host C).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../font8x8.h"   /* reuse the firmware's real font + font_glyph() */

typedef unsigned int uint;

/* Composable opcode sentinels.  Real firmware values come from the SDK; here we
 * only need values distinct from any 16-bit colour (colours are 0x0000..0xFFFE),
 * so we tag opcodes above the 16-bit range. */
enum {
    OP_COLOR_RUN = 0x10000,
    OP_RAW_RUN,
    OP_RAW_1P,
    OP_RAW_2P,
    OP_EOL_ALIGN,
    OP_EOL_SKIP_ALIGN,
};

/* A token stream is an array of ints: opcodes (>= 0x10000) interleaved with
 * 16-bit colour / count payload values. */
#define MAX_TOKENS 65536

/* Colours used by the text pattern (5:5:5, alpha bit 0).  Only their identity
 * matters for the sim, so we use simple stand-ins. */
#define COL_BG 0x0000          /* black  */
#define COL_FG 0xFFFE          /* white  */

/* ---- emit_run: smallest-token form for n pixels of one colour --------------
 * Mirrors firmware emit_run() so margins/fills are generated identically. */
static int *emit_run(int *p, int color, uint n) {
    if (n == 1) {
        *p++ = OP_RAW_1P; *p++ = color;
    } else if (n == 2) {
        *p++ = OP_RAW_2P; *p++ = color; *p++ = color;
    } else if (n >= 3) {
        *p++ = OP_COLOR_RUN; *p++ = color; *p++ = (int)(n - 3);
    }
    return p;
}

/* Append the end-of-line marker, choosing ALIGN vs SKIP_ALIGN by the current
 * half-word parity exactly like the firmware's ((uintptr_t)p & 3) test. */
static int *emit_eol(int *p, int *base) {
    int idx = (int)(p - base);          /* token index == (bytes/2) */
    *p++ = (idx & 1) ? OP_EOL_ALIGN     /* odd index  -> high word -> ALIGN      */
                     : OP_EOL_SKIP_ALIGN;/* even index -> low word  -> SKIP_ALIGN */
    *p++ = 0;
    return p;
}

/* A solid background line, matching firmware draw_color_line(). */
static int gen_color_line(int *s, uint width, int color) {
    int *p = s;
    *p++ = OP_COLOR_RUN; *p++ = color; *p++ = (int)(width - 3);
    *p++ = OP_RAW_1P;    *p++ = 0;      /* mandatory trailing black before EOL  */
    p = emit_eol(p, s);
    return (int)(p - s);
}

/* ---- gen_draw_text: mirror of firmware draw_text() -------------------------
 * Generates the token stream for one scanline of `text` at (x,y), scaled, for a
 * display `width` pixels wide.  Returns the token count. */
static int gen_draw_text(int *s, uint width, const char *text,
                         int x, int y, int bg, int fg, uint scale, int line_num) {
    uint band_h  = (uint)FONT8X8_H * scale;
    uint len     = (uint)strlen(text);
    uint text_px = len * (uint)FONT8X8_W * scale;

    if (scale == 0 || text_px < 3 || x < 0 || (uint)x >= width ||
        (uint)line_num < (uint)y || (uint)line_num >= (uint)y + band_h)
        return gen_color_line(s, width, bg);

    uint avail = width - (uint)x;
    uint vis = text_px;
    if (vis > avail - 1) vis = avail - 1;   /* clip, leaving 1 px for trailing black */
    if (vis < 3)
        return gen_color_line(s, width, bg);

    uint glyph_row = (line_num - (uint)y) / scale;

    int *p = s;
    p = emit_run(p, bg, (uint)x);                  /* left margin */

    *p++ = OP_RAW_RUN;
    int *pixel0 = p++;                             /* reserve colour-1 slot */
    *p++ = (int)(vis - 3);                         /* count */
    /* Division-free nested walk, matching firmware draw_text() (kept cheap so it
     * fits the time-critical scanline budget on hardware). */
    uint pidx = 0;
    for (uint ci = 0; ci < len && pidx < vis; ci++) {
        uint8_t rowbits = font_glyph(text[ci])[glyph_row];
        for (uint col = 0; col < (uint)FONT8X8_W && pidx < vis; col++) {
            int color = (rowbits & (1u << col)) ? fg : bg;
            for (uint sc = 0; sc < scale && pidx < vis; sc++) {
                if (pidx == 0) *pixel0 = color;
                else           *p++ = color;
                pidx++;
            }
        }
    }

    p = emit_run(p, bg, width - (uint)x - vis - 1u);/* right margin */
    *p++ = OP_RAW_1P; *p++ = 0;                    /* trailing black */
    p = emit_eol(p, s);
    return (int)(p - s);
}

/* ---- pio_run: execute a token stream like the PIO ---------------------------
 * Writes the produced pixels into out[] (capped at maxout) and returns the pixel
 * count.  Sets *desync if the stream runs out mid-token or hits a value where an
 * opcode was expected — i.e. the firmware would have corrupted the display. */
static int pio_run(const int *s, int ntok, int *out, int maxout, bool *desync) {
    int i = 0, np = 0;
    *desync = false;
    while (i < ntok) {
        int op = s[i++];                 /* `out pc` reads next token as opcode */
        if (op == OP_EOL_ALIGN || op == OP_EOL_SKIP_ALIGN)
            return np;                   /* clean end of line */

        if (op == OP_COLOR_RUN) {
            if (i + 1 >= ntok) { *desync = true; return np; }
            int color = s[i++];
            int cnt   = s[i++];
            if (cnt < 0 || cnt > 0xFFFF) { *desync = true; return np; }
            for (int k = 0; k < cnt + 3; k++) { if (np < maxout) out[np] = color; np++; }
        } else if (op == OP_RAW_1P) {
            if (i >= ntok) { *desync = true; return np; }
            int color = s[i++];
            if (np < maxout) out[np] = color; np++;
        } else if (op == OP_RAW_2P) {
            if (i + 1 >= ntok) { *desync = true; return np; }
            int c1 = s[i++], c2 = s[i++];
            if (np < maxout) out[np] = c1; np++;
            if (np < maxout) out[np] = c2; np++;
        } else if (op == OP_RAW_RUN) {
            if (i + 1 >= ntok) { *desync = true; return np; }
            int color1 = s[i++];
            int n      = s[i++];
            if (n < 0 || n > 0xFFFF) { *desync = true; return np; }
            if (np < maxout) out[np] = color1; np++;       /* colour 1 */
            for (int k = 0; k < n + 1; k++) {              /* pixel_loop: n+1 */
                if (i >= ntok) { *desync = true; return np; }
                int c = s[i++]; if (np < maxout) out[np] = c; np++;
            }
            if (i >= ntok) { *desync = true; return np; }  /* raw_1p fall-through */
            int cf = s[i++]; if (np < maxout) out[np] = cf; np++;
        } else {
            *desync = true; return np;   /* a payload value landed where an opcode
                                            was expected: real PIO would desync */
        }
    }
    *desync = true;                      /* ran off the end without an EOL */
    return np;
}

/* ---------------------------------------------------------------------------
 * Tests / demos
 * ------------------------------------------------------------------------- */

/* Validate one scanline: stream is consumed cleanly and produces ~width pixels. */
static int check_line(uint width, const char *text, int x, int y, uint scale, int line) {
    static int s[MAX_TOKENS];
    static int out[MAX_TOKENS];
    int n = gen_draw_text(s, width, text, x, y, COL_BG, COL_FG, scale, line);
    bool desync;
    int np = pio_run(s, n, out, MAX_TOKENS, &desync);
    /* A valid line must consume cleanly (no desync) and cover the active region.
     * Two trailing-black conventions coexist in the firmware and both display
     * correctly: draw_bitmap() emits exactly `width` pixels, while
     * draw_color_line()/draw_color_bars() emit `width+1` (the extra black pixel
     * lands in the front porch).  Accept either; flag anything else. */
    int ok = (!desync) && (np == (int)width || np == (int)width + 1);
    if (!ok)
        printf("  FAIL width=%u line=%d: pixels=%d (want %u or %u) desync=%d\n",
               width, line, np, width, width + 1, desync);
    return ok;
}

static void sweep_widths(void) {
    const uint W[] = {480, 640, 768, 272, 234, 300, 240, 200, 120, 96, 64, 40, 24, 16};
    const char *msg = "HELLO PICO 0123";
    int x = 8, y = 8; uint scale = 3;
    int fails = 0, total = 0;
    printf("== width sweep: draw_text(\"%s\", x=%d, y=%d, scale=%u) ==\n", msg, x, y, scale);
    for (uint i = 0; i < sizeof(W) / sizeof(W[0]); i++) {
        uint w = W[i];
        /* check every band line plus a couple off-band lines */
        for (int line = 0; line < (int)(y + 8 * scale + 4); line++) {
            total++;
            if (!check_line(w, msg, x, y, scale, line)) fails++;
        }
    }
    printf("  %d lines checked across %zu widths, %d failures -> %s\n\n",
           total, sizeof(W) / sizeof(W[0]), fails, fails ? "PROBLEM" : "all OK");
}

/* Render the whole text pattern for one (width, scale) as ASCII art so glyphs can
 * be eyeballed without hardware.  '#' = foreground, ' ' = background. */
static void preview(uint width, const char *text, int x, int y, uint scale) {
    static int s[MAX_TOKENS];
    static int out[MAX_TOKENS];
    uint band_h = (uint)FONT8X8_H * scale;
    printf("== preview: \"%s\" width=%u x=%d y=%d scale=%u ==\n", text, width, x, y, scale);
    for (int line = y; line < (int)(y + band_h); line++) {
        int n = gen_draw_text(s, width, text, x, y, COL_BG, COL_FG, scale, line);
        bool desync;
        int np = pio_run(s, n, out, MAX_TOKENS, &desync);
        putchar('|');
        for (int i = 0; i < np && i < (int)width; i++)
            putchar(out[i] == COL_BG ? ' ' : '#');
        printf("|%s\n", desync ? "  <DESYNC>" : "");
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    sweep_widths();
    /* A width that comfortably fits the default string, and a narrow one that
     * exercises clipping. */
    preview(480, "HELLO PICO 0123", 8, 8, 2);
    preview(200, "HELLO PICO 0123", 8, 8, 2);   /* clipped */
    (void)argc; (void)argv;
    return 0;
}
