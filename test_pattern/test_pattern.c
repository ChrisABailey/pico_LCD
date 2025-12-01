/*
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/sync.h"
#include "hardware/clocks.h"
#include "text.h"
#define video_mode psp_mode_480x272_60
//#define video_mode vga_mode_640x480_60_clk

typedef enum { black, blue, cyan, green,  yellow, red, magenta,  white, bars, grey, border,text, animate, max_pattern} Pattern;

//PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN ??
//PICO_SCANVIDEO_ENABLE_DEN_PIN
// PICO_SCANVIDEO_ENABLE_CLOCK_PIN

const scanvideo_timing_t psp_timing_480x272_60_default =
        {
                
                .clock_freq = 10000000,

                .h_active = 480,
                .v_active = 272,
                .h_front_porch = 8,
                .h_pulse = 8,
                .h_total = 531,
                .h_sync_polarity = 1,

                .v_front_porch = 10,
                .v_pulse = 2,
                .v_total = 294,
                .v_sync_polarity = 1,

                .enable_clock = 1,
                .clock_polarity = 0,

                .enable_den = 1
        };

const scanvideo_mode_t psp_mode_480x272_60 =
        {
                .default_timing = &psp_timing_480x272_60_default,
                .pio_program = &video_24mhz_composable,
                .width = 480,
                .height = 272,
                .xscale = 1,
                .yscale = 1,
        };

const scanvideo_timing_t vga_timing_640x480_60_clk =
        {
                .clock_freq = 10000000,

                .h_active = 480,
                .v_active = 272,

                .h_front_porch = 8,
                .h_pulse = 8,
                .h_total = 531,
                .h_sync_polarity = 1,

                .v_front_porch = 8,
                .v_pulse = 4,
                .v_total = 292,
                .v_sync_polarity = 1,

                .enable_clock = 1,
                .clock_polarity = 0,

                .enable_den = 1
        };

const scanvideo_mode_t vga_mode_640x480_60_clk =
        {
                .default_timing = &vga_timing_640x480_60_clk,
                .pio_program = &video_24mhz_composable,
                .width = 480,
                .height = 272,
                .xscale = 1,
                .yscale = 1,
        };

void render_graphics();

void draw_border(scanvideo_scanline_buffer_t *scanline_buffer);

void print_help();

uint32_t pattern_to_color(Pattern pattern){ 
    switch(pattern){
        case black: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 0);
        case blue: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 0x1f);
        case green: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0);
        case cyan: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0x1f);
        case red: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0);
        case magenta: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0x1f);
        case yellow: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0x1f, 0);
        case white: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0x1f, 0x1f);;
        default: return 0x0000;
    }
}

const char* pattern_to_string(Pattern pattern){ 
    switch(pattern){
        case black: return "black";
        case blue: return "blue";
        case green: return "green";
        case cyan: return "cyan";
        case red: return "red";
        case magenta: return "magenta";
        case yellow: return "yellow";
        case white: return "white";
        case bars: return "bars";
        case grey: return "greyshades";
        case border: return "border";
        case text: return "text";
        case animate: return "animate";
        default: return "unknown";
    }
}
// Simple color bar program, which draws test patterns including:
//      solid colored black, blue, green, cyan, red, magenta, yellow, and white
//      color bars
//      grey shades
//      border test
//      text test
//
// Note this program also demonstrates running video on core 1, leaving core 0 free. It supports
// user input over USB or UART stdin, although all it does with it is invert the colors when you press SPACE

static semaphore_t video_initted;
static uint dummy = 0;
static Pattern pattern = animate;

void dump_bmp(const uint16_t *bmp, uint width, uint height)
{
    printf("\n(%02dx%02d)  ", width, height);
    for (uint x = 0; x < width; x++) {
            printf("c%03d ", x);
        }
    for (uint y = 0; y < height; y++) {
        printf("\n Row %02d: ", y );
        for (uint x = 0; x < width; x++) {
            uint16_t pixel = bmp[y * width + x];
            printf("%04x ", pixel);
        }  
    }
    printf("\n");
}

int main(void) {
    //stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN,true);    
    //gpio_put(27, 0);
    gpio_put(PICO_DEFAULT_LED_PIN,true);
    busy_wait_ms(100);
    gpio_put(PICO_DEFAULT_LED_PIN,false);
    int clk = clock_get_hz(clk_sys);

    switch (video_mode.default_timing->clock_freq) {
        case 65000000:
            set_sys_clock_khz(130000, true);
            break;
        case 8000000:
            set_sys_clock_khz(128000,true);
            break;
        case 9000000:
            set_sys_clock_khz(144000,true);
            break;
        case 10000000:
            set_sys_clock_khz(160000,true);
            break;
        case 25000000:
            set_sys_clock_khz(200000,true);
            break;
        default:
            printf("Pixel clock = %d\r\n",video_mode.default_timing->clock_freq);
            break;
    }
    // init uart now that clk_peri has changed
    stdio_init_all();


    clk = clock_get_hz(clk_sys);
    int divider= clk / video_mode.default_timing->clock_freq;
    printf("New PixelClock = %d, sysclock=%d, clock divider=%d\r\n",video_mode.default_timing->clock_freq,clk/1000,divider);
#if PICO_SCANVIDEO_ENABLE_CLOCK_PIN
#ifndef PICO_SCANVIDEO_ENABLE_DEN_PIN
    printf("Warning: Clock pin requires DEN pin to be enabled");
#endif

    if (divider & 1) {
        printf("To enable pixel clock, the System clock (%dK) must be an integer multiple of 2 times the requested pixel clock (%d).", SYS_CLK_KHZ, video_mode.default_timing->clock_freq);
    }
#else
    if (video_mode.default_timing->enable_clock) {
        printf("Pixel clock output enabled, but PICO_SCANVIDEO_ENABLE_CLOCK_PIN is not set!\n");
    }
#endif

    gpio_put(PICO_DEFAULT_LED_PIN,true);

    // create a semaphore to be posted when video init is complete
    sem_init(&video_initted, 0, 1);

    // launch all the video on core 1, so it isn't affected by USB handling on core 0
    multicore_launch_core1(render_graphics);

    // wait for initialization of video to be complete
    sem_acquire_blocking(&video_initted);

    print_help();

    while (true) {
        // prevent tearing when we change - if you're astute you'll notice this actually causes
        // a fixed tear a number of scanlines from the top. this is caused by pre-buffering of scanlines
        // and is too detailed a topic to fix here.
        scanvideo_wait_for_vblank();
        int c = getchar_timeout_us(0);
        switch (c) {
            case ' ':
                pattern = (Pattern)((pattern + 1) % max_pattern);
                break;
            case 'r':
                pattern = red;
                break;
            case 'g':
                pattern = green;
                break;
            case 'b':
                pattern = blue;
                break;
            case 'w':
                pattern = white;
                break;
            case 'k':
                pattern = black;
                break;
            case 'c':
                pattern = cyan;
                break;
            case 'm':
                pattern = magenta;
                break;
            case 'y':
                pattern = yellow;
                break;
            case 's':
                pattern = bars;
                break;
            case 'e':
                pattern = border;
                break;
            case 't':
                pattern = text;
                break;
            case 'q':
                pattern = grey;
                break;
            case 'a':
                pattern = animate;
                break;
            case PICO_ERROR_TIMEOUT:
                break;
            default:
                print_help();
                break;
        }
        if (c != PICO_ERROR_TIMEOUT)
            printf("Pattern: %s\n", pattern_to_string(pattern));
    }
}

void print_help()
{
    puts("test screens:\r\n"
         "  SPACE: cycle patterns\r\n"
         "  r: red\r\n"
         "  g: green\r\n"
         "  b: blue\r\n"
         "  c: cyan\r\n"
         "  m: magenta\r\n"
         "  y: yellow\r\n"
         "  w: white\r\n"
         "  k: black\r\n"
         "  s: color stripes\r\n"
         "  e: border along edge\r\n"
         "  t: text test\r\n"
         "  q: grey shades\r\n"
         "  a: animate\r\n"
         );
}


// Draw solid horizontal line of specified color
void draw_color_line(scanvideo_scanline_buffer_t *buffer, uint32_t color) {
    uint16_t *p = (uint16_t *) buffer->data;


    *p++ = COMPOSABLE_COLOR_RUN;
    *p++ = color;
    *p++ = video_mode.width - 3;

    // add black pixel to end line
    *p++ = COMPOSABLE_RAW_1P;
    *p++ = 0;


    // end of line with alignment padding
    if (((uintptr_t)p) & 3) {
        *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *p++ = 0;
    } else {
        *p++ = COMPOSABLE_EOL_ALIGN;
        *p++ = 0;
    }

    buffer->data_used = ((uint32_t *) p) - buffer->data;
    assert(buffer->data_used < buffer->data_max);

    buffer->status = SCANLINE_OK;

}

// draw 8 color bars from left to right
void draw_color_bars(scanvideo_scanline_buffer_t *buffer) {
    uint16_t bar_width = (video_mode.width-1) / 8;
    uint32_t color;

    uint16_t *p = (uint16_t *) buffer->data;

    for (uint bar = 8; bar > 0; bar--) {
        *p++ = COMPOSABLE_COLOR_RUN;
        color = pattern_to_color((Pattern)(bar - 1));
        *p++ = color;
        *p++ = bar_width - 3;
    }
    if (8 * bar_width < video_mode.width) {
        // fill any remaining pixels (due to rounding)
        *p++ = COMPOSABLE_COLOR_RUN;
        *p++ = pattern_to_color(black);
        *p++ = video_mode.width - 3 - 8 * bar_width;
    }
    //black pixel to end line
    *p++ = COMPOSABLE_RAW_1P;
    *p++ = 0;

    if (((uintptr_t)p) & 3) {
        *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *p++ = 0;
    } else {
        *p++ = COMPOSABLE_EOL_ALIGN;
        *p++ = 0;
    }

    buffer->data_used = ((uint32_t *) p) - buffer->data;
    assert(buffer->data_used < buffer->data_max);

    buffer->status = SCANLINE_OK;
}

// draw 32 shades of grey for each color increasing from left to right
void draw_color_shades(scanvideo_scanline_buffer_t *buffer) {
    // figure out 1/32 of the color value
    uint line_num = scanvideo_scanline_number(buffer->scanline_id);
    uint32_t primary_color = 1u + (line_num * 7 / video_mode.height);
    uint32_t color_mask = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f * (primary_color & 1u), 0x1f * ((primary_color >> 1u) & 1u), 0x1f * ((primary_color >> 2u) & 1u));
    uint bar_width = video_mode.width / 32;

    uint16_t *p = (uint16_t *) buffer->data;

    for (uint bar = 0; bar < 32; bar++) {
        *p++ = COMPOSABLE_COLOR_RUN;
        uint32_t color = PICO_SCANVIDEO_PIXEL_FROM_RGB5(bar, bar, bar);
        *p++ = (color & color_mask);
        *p++ = bar_width - 3;
    }

    // 32 * 3, so we should be word aligned
    assert(!(3u & (uintptr_t) p));

    // black pixel to end line
    *p++ = COMPOSABLE_RAW_1P;
    *p++ = 0;
    // end of line with alignment padding
    *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
    *p++ = 0;

    buffer->data_used = ((uint32_t *) p) - buffer->data;
    assert(buffer->data_used < buffer->data_max);

    buffer->status = SCANLINE_OK;
}



// Black screen with 1 pixel of white and one pixel of red border
void draw_border(scanvideo_scanline_buffer_t *scanline_buffer)
{
    
    uint line_num = scanvideo_scanline_number(scanline_buffer->scanline_id);
    if (line_num < 1 || line_num == video_mode.height - 1) {
        draw_color_line(scanline_buffer, pattern_to_color(white));
    } 
    else if ((line_num ==1)||(line_num == video_mode.height-2))
    {
        draw_color_line(scanline_buffer, pattern_to_color(red));
    }
    else
    {
        uint16_t *p = (uint16_t *)scanline_buffer->data;
        // left border
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = pattern_to_color(white);
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = pattern_to_color(red);
        // middle
        *p++ = COMPOSABLE_COLOR_RUN;
        *p++ = pattern_to_color(black);
        *p++ = video_mode.width - 4 - 3;
        // right border
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = pattern_to_color(red);
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = pattern_to_color(white);
        // black pixel to end line
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = 0;

        // end of line with alignment padding
        if (((uintptr_t)p) & 3)
        {
            *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
            *p++ = 0;
        }
        else
        {
            *p++ = COMPOSABLE_EOL_ALIGN;
            *p++ = 0;
        }

        scanline_buffer->data_used = ((uint32_t *)p) - scanline_buffer->data;
        assert(scanline_buffer->data_used < scanline_buffer->data_max);

        scanline_buffer->status = SCANLINE_OK;
    }
}

void draw_box(scanvideo_scanline_buffer_t *scanline_buffer, uint x, uint y, uint width, uint height, uint32_t color)
{
    uint line_num = scanvideo_scanline_number(scanline_buffer->scanline_id);
    if (line_num >= y && line_num < (y + height)) {
        uint16_t *p = (uint16_t *)scanline_buffer->data;
        // left blank
        if (x > 0) {
            *p++ = COMPOSABLE_COLOR_RUN;
            *p++ = 0;
            *p++ = x - 1-3;
        }
        // box
        *p++ = COMPOSABLE_COLOR_RUN;
        *p++ = color;
        *p++ = width - 1-3;
        // right blank
        if ((x + width) < video_mode.width) {
            *p++ = COMPOSABLE_COLOR_RUN;
            *p++ = 0;
            *p++ = video_mode.width - (x + width) - 1 -3;
        }
        // black pixel to end line
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = 0;

        // end of line with alignment padding
        if (((uintptr_t)p) & 3)
        {
            *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
            *p++ = 0;
        }
        else
        {
            *p++ = COMPOSABLE_EOL_ALIGN;
            *p++ = 0;
        }

        scanline_buffer->data_used = ((uint32_t *)p) - scanline_buffer->data;
        assert(scanline_buffer->data_used < scanline_buffer->data_max);

        scanline_buffer->status = SCANLINE_OK;
    } else {
        draw_color_line(scanline_buffer, 0);
    }
}

void __time_critical_func(draw_bitmap)(scanvideo_scanline_buffer_t *scanline_buffer, const uint16_t *bitmap, uint bmp_width, uint bmp_height, uint x, uint y)
{

    uint line_num = scanvideo_scanline_number(scanline_buffer->scanline_id);

    if (line_num >= y && line_num < (y + bmp_height)) 
    {
        uint16_t *p = (uint16_t *)scanline_buffer->data;
        // left blank
        if (x > 0) {
            *p++ = COMPOSABLE_COLOR_RUN;
            *p++ = 0;
            *p++ = x - 1-3;
        }
        // bitmap
        uint bmp_line = line_num - y;
        *p++ = COMPOSABLE_RAW_RUN;
        *p++ = bitmap[bmp_line * bmp_width];
        *p++ = bmp_width - 1-3;
        for (uint i = 1; i < bmp_width -1; i++) {
            *p++ = bitmap[bmp_line * bmp_width + i];
        }

        // right blank
        if ((x + bmp_width) < video_mode.width) {
            *p++ = COMPOSABLE_COLOR_RUN;
            *p++ = 0;
            *p++ = video_mode.width - (x + bmp_width) - 1 -3;
        }
        // black pixel to end line
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = 0;

        // end of line with alignment padding
        if (((uintptr_t)p) & 3)
        {
            *p++ = COMPOSABLE_EOL_SKIP_ALIGN;
            *p++ = 0;
        }
        else
        {
            *p++ = COMPOSABLE_EOL_ALIGN;
            *p++ = 0;
        }

        scanline_buffer->data_used = ((uint32_t *)p) - scanline_buffer->data;
        if(scanline_buffer->data_used > scanline_buffer->data_max)
        {
            gpio_put(PICO_DEFAULT_LED_PIN,false);
        }

        scanline_buffer->status = SCANLINE_OK;
    } else {
        draw_color_line(scanline_buffer, 0);
    }
}

#define box_size 50

// This function is called for each scan line of the display
// it is set to run on core 1 leaving core 0 to run the UI and other tasks
void __time_critical_func(render_graphics)() {
    static int x=50;
    static int y=50;
    static int dx=1;
    static int dy=1;
    static uint16_t last_frame_num=0;
    static uint32_t color=1;
    // initialize video and interrupts on core 1
    scanvideo_setup(&video_mode);
    scanvideo_timing_enable(true);
    sem_release(&video_initted);

    while (true) {
        scanvideo_scanline_buffer_t *scanline_buffer = scanvideo_begin_scanline_generation(true);

        if (pattern == text) 
        {
            draw_bitmap(scanline_buffer, (const uint16_t *)textbox, text_width, text_height, 15, 15);
        } 
        else if (pattern == bars)
        {
            draw_color_bars(scanline_buffer);
        } 
        else if (pattern == border)
        {   
            draw_border(scanline_buffer);
        } 
        else if (pattern <= white) 
        {
            draw_color_line(scanline_buffer, pattern_to_color(pattern));
        } 
        else if (pattern == grey) 
        {
            draw_color_shades(scanline_buffer);
        } 
        else if (pattern == animate) 
        {
            static uint16_t scanline_color = 0;
            uint line_num = scanvideo_scanline_number(scanline_buffer->scanline_id);
            uint16_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
            if (frame_num != last_frame_num) 
            {
                last_frame_num = frame_num;
                x += dx;
                y += dy;
                if ((x <= 0) || (x > (video_mode.width - box_size))) 
                {
                    dx = -dx;
                    x += dx;
                    color = (color+1)%(int)white + 1;
                }
                if ((y <= 0) || (y > (video_mode.height - box_size))) 
                {
                    dy = -dy;
                    y += dy;
                    color = (color+1)%(int)white + 1;
                }
            }
            draw_box(scanline_buffer, x, y, box_size, box_size, pattern_to_color((Pattern)color));
        } 

        scanvideo_end_scanline_generation(scanline_buffer);
    }
}
