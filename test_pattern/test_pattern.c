/*
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/gpio.h"
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/sync.h"
#include "pico/flash.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "text.h"
#include "userio.h"

#define CYCLE_BUTTON_PIN 28
const char *RELEASE = "1.1.0";

// Location in flash for the Timing settings
// the size of the program stored on flash is about 100K
// You can check this by looking at the value of MIN_FLASH_OFFSET
// this should be at the start of a 1K block (FLASH_PAGE_SIZE)
#define LCD_SETTINGS_OFFSET (256 * 1024)

extern char __flash_binary_end;
#define MIN_FLASH_OFFSET ((uintptr_t) &__flash_binary_end - XIP_BASE)

const uint8_t *lcd_settings_startaddr = (const uint8_t *) (XIP_BASE + LCD_SETTINGS_OFFSET);
static volatile bool wait_for_flash = false;

//#define video_mode vga_mode_640x480_60_clk

typedef enum { black, blue, cyan, green,  yellow, red, magenta,  white, custom, bars, border, text,  animate, grey, lines, box, max_pattern} Pattern;

//PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN ??
//PICO_SCANVIDEO_ENABLE_DEN_PIN
// PICO_SCANVIDEO_ENABLE_CLOCK_PIN

const scanvideo_timing_t dcdu_timing_480x234_60_default =
        {
                
                .clock_freq = 7800000, //7.8MHz

                .h_active = 480,
                .v_active = 234,
                .h_front_porch = 5,
                .h_pulse = 10,
                .h_total = 530,
                .h_sync_polarity = 1,

                .v_front_porch = 8,
                .v_pulse = 2,
                .v_total = 250,
                .v_sync_polarity = 1,

                .enable_clock = 1,
                .clock_polarity = 1,

                .enable_den = 1
        };

const scanvideo_mode_t dcdu_mode_480x234_60 =
        {
                .default_timing = &dcdu_timing_480x234_60_default,
                .pio_program = &video_24mhz_composable,
                .width = 480,
                .height = 234,
                .xscale = 1,
                .yscale = 1,
        };

const scanvideo_timing_t psp_timing_480x272_60_default =
        {
                
                .clock_freq = 10000000,

                .h_active = 480,
                .v_active = 272,
                .h_front_porch = 8,
                .h_pulse = 8, //8
                .h_total = 530, //531
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


const scanvideo_timing_t ay_mode_768x256_60_default =
        {
                
                .clock_freq = 14375000,

                .h_active = 768,
                .v_active = 256,
                .h_front_porch = 50,
                .h_pulse = 12,
                .h_total = 834,
                .h_sync_polarity = 1,

                .v_front_porch = 24,
                .v_pulse = 2,
                .v_total = 288,
                .v_sync_polarity = 1,

                .enable_clock = 1,
                .clock_polarity = 0,

                .enable_den = 1
        };

const scanvideo_mode_t ay_mode_768x256_60 =
        {
                .default_timing = &ay_mode_768x256_60_default,
                .pio_program = &video_24mhz_composable,
                .width = 768,
                .height = 256,
                .xscale = 1,
                .yscale = 1,
        };


// this is the default video timing at program install
// the default can be changed using the "P"rogram command
scanvideo_mode_t video_mode =dcdu_mode_480x234_60;
scanvideo_timing_t video_timing = dcdu_timing_480x234_60_default;
volatile uint8_t custom_red=255;
volatile uint8_t custom_green=191;
volatile uint8_t custom_blue=8;

// forward refrence for render_graphics which is executed on core 1
void render_graphics();


uint32_t pattern_to_color(Pattern pattern){ 
    switch(pattern){
        case black: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 0);
        case blue: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 0x1f);
        case green: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0);
        case cyan: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0x1f);
        case red: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0);
        case magenta: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0x1f);
        case yellow: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0x1f, 0);
        case white: return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0x1f, 0x1f);
        case custom: return PICO_SCANVIDEO_PIXEL_FROM_RGB8(custom_red, custom_green,custom_blue);
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
        case custom: return "custom";
        case lines: return "lines";
        case box: return "box";
        default: return "unknown";
    }
}

//  The system clock has to be a power of 2 times the pixel clock
// return an appropriate system clock speed (in kHz) based on the pix clk
// Note that the system clock us generated by a PLL so not every value is viable
// On a Pico 1 the clock is typically 120MHz but can be overclocked up to about 250MHz
//
uint32_t SysClkForPixClk(uint32_t pixClk)
{
    switch (pixClk)
    {
    case 65000000:
        return 130000;
        break;
    case 7800000:
        return 124800;
        break;    
    case 8000000:
        return 128000;
        break;
    case 9000000:
        return 144000;
        break;
    case 10000000:
        return 160000;
        break;
    case 15000000:
        return 120000;
        break;
    case 25000000:
        return 200000;
        break;
    case 14375000:
        return 230000;
        break;
    default:
        uint32_t sys;
        if ( (sys = pixClk/1000*8) < 100000)
            sys = sys*2;
        return sys;
    }
}

static semaphore_t video_initted;
static uint dummy = 0;
static Pattern pattern = animate;

// for debugging bitmap display
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

void print_timing_settings(scanvideo_timing_t t, uint32_t s)
{
    printf("\r\nVideo Timing Settings:\r\n"
    " System clock %u kHz \r\n"
    " Pixel clock: %u Hz\r\n"
    " Horizontal:\r\n"
    " \tActive pixels: \t%hu\r\n" 
    " \tFront porch:  \t%hu pixels\r\n"
    " \tH-Sync width: \t%hu pixels\r\n"
    " \tBack Porch: \t(%hu) pixels\r\n"
    " \tTotal pixels: \t%hu \r\n"
    " Vertical: \r\n"
    " \tActive lines: \t%hu\r\n" 
    " \tFront porch: \t%hu lines\r\n"
    " \tV-Sync Width: \t%hu lines\r\n"
    " \tBack Porch: \t(%hu) lines\r\n"
    " \tTotal lines: \t%hu \r\n"
    " \tClock polarity: %c\r\n",
    s,
    t.clock_freq,
    t.h_active, t.h_front_porch, t.h_pulse, 
    (t.h_total-(t.h_active+ t.h_front_porch+t.h_pulse)),t.h_total,
    t.v_active, t.v_front_porch, t.v_pulse, 
    (t.v_total-(t.v_active+ t.v_front_porch+t.v_pulse)),t.v_total,
    t.clock_polarity?'+':'-');
    //printf(" X-scale: %u, V-scale: %u \r\n\r\n",m.xscale,m.yscale);
}

// print timing mode settings
void print_mode_settings(scanvideo_mode_t m)
{
    printf("Video Mode Settings:\r\n"
    " Width: %hu pixels\r\n"
    " Height: %hu lines\r\n"
    " X-scale: %u\r\n"
    " Y-scale: %u\r\n\r\n",
    m.width, m.height,
    m.xscale, m.yscale);
}

// This function will be called when it's safe to call flash_range_erase
static void __no_inline_not_in_flash_func(call_flash_range_erase)(void *param) {
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

// This function will be called when it's safe to call flash_range_program
static void __no_inline_not_in_flash_func(call_flash_range_program)(void *param) {
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

//
// Write video settings to flash memory
// This requires stopping the other core and ensuring that nothing is running out of flash
//
// This Flag is used to identify the setting in flash conform to the current format
// if we change the format, pick a new flag to ignore old settings at startup
const uint32_t FLAG = 0x1DCCAB1;
bool __no_inline_not_in_flash_func(update_flash_settings)(const scanvideo_timing_t *timing, const scanvideo_mode_t *mode, uint32_t sysClkKHz, uint32_t flag)
{
    // stop core 1 video
    wait_for_flash = true;
    busy_wait_ms(100);  // wait a few frames for the second core to disable its interupts

    // disable interrupts
    uint32_t saved = save_and_disable_interrupts();
    
    int8_t memblock[FLASH_PAGE_SIZE];
    
    assert(sizeof(scanvideo_timing_t)  < FLASH_PAGE_SIZE);

    memcpy(memblock, timing, sizeof(scanvideo_timing_t));
    memcpy(memblock+sizeof(scanvideo_timing_t),&flag,sizeof(uint32_t));

    // Note that a whole number of sectors must be erased at a time.
    //the total data is less than 100Bytes so it fits in one sector which is 4K
    printf("\nErasing Flash Region...\n");
    busy_wait_ms(100);

    // Flash is "execute in place" and so will be in use when any code that is stored in flash runs, e.g. an interrupt handler
    // or code running on a different core.
    // Calling flash_range_erase or flash_range_program at the same time as flash is running code would cause a crash.
    // flash_safe_execute disables interrupts and tries to cooperate with the other core to ensure flash is not in use
    // See the documentation for flash_safe_execute and its assumptions and limitations
    int rc = flash_safe_execute(call_flash_range_erase, (void*)LCD_SETTINGS_OFFSET, UINT32_MAX);
    hard_assert(rc == PICO_OK);
    //printf("Return Code %d from erase\r\n",rc);

    printf("\nProgramming Flash...\n");
    uintptr_t params[] = { LCD_SETTINGS_OFFSET, (uintptr_t)memblock};
    rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
    hard_assert(rc == PICO_OK);

    memcpy(memblock, mode, sizeof(scanvideo_mode_t));
    memcpy(memblock+sizeof(scanvideo_mode_t), &sysClkKHz, sizeof(uint32_t));
    params[0] = LCD_SETTINGS_OFFSET+FLASH_PAGE_SIZE;
    params[1] = (uintptr_t)memblock;
    rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
    hard_assert(rc == PICO_OK);

    restore_interrupts(saved);
    wait_for_flash = false;
    printf("Settings Saved. Restart board to use the new settings.\r\n");
    return true;
}

//
// Retreive Video Settings from Flash
// return tru on success or false if no settings have been written
//
bool get_flash_settings(scanvideo_timing_t *timing, scanvideo_mode_t *mode, uint32_t *sysClkKHz)
{
    const uint32_t *flag = (const uint32_t *)(lcd_settings_startaddr + sizeof(scanvideo_timing_t));

    if (*flag != FLAG)
        return false; // this means the memory has not been flashed before
    memcpy(timing,lcd_settings_startaddr,sizeof(scanvideo_timing_t));
    memcpy(mode,lcd_settings_startaddr+FLASH_PAGE_SIZE,sizeof(scanvideo_mode_t));
    mode->pio_program = &video_24mhz_composable;
    mode->default_timing = timing;
    memcpy(sysClkKHz, lcd_settings_startaddr+FLASH_PAGE_SIZE+sizeof(scanvideo_mode_t),sizeof(uint32_t));
    return true;
}

// get custom video timing settings from user
bool get_custom_timing(scanvideo_timing_t *timing,int32_t*sysClkKHz)
{
    int sys_clk,vf,pd1,pd2;
    do
    {
        do
        {
            printf("\r\nSystem Clock frequency in kHz (100000 - 250000) (must be a power of 2 * pixel clock)[%d]: ",*sysClkKHz);

            sys_clk=getInt(true);
            if (check_sys_clock_khz(sys_clk,&vf,&pd1,&pd2) != true)
            {
                printf("Unsupported system clock frequency see:\r\n"
                          "https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf#page=93\r\n"
                          "or pico-sdk/src/rp2_common/hardware_clocks/scripts/vcocalc.py\r\n"
                );
            }
            else
            {
                break;
            }
        } while (true);



        printf("\r\nPixel clock frequency (in Hz)[%d]: ",timing->clock_freq);
        timing->clock_freq=getInt(true);
        printf("\r\nHorizontal active pixels[%d]: ",timing->h_active);
        timing->h_active=getInt(true);
        printf("\r\nVertical active pixels[%d]: ",timing->v_active);
        timing->v_active =getInt(true);
        printf("\r\nHorizontal front porch (pixels)[%d]: ",timing->h_front_porch);
        timing->h_front_porch=getInt(true);
        printf("\r\nHorizontal sync pulse width (pixels)[%d]: ",timing->h_pulse);
        timing->h_pulse =getInt(true);
        printf("\r\nHorizontal total pixels[%d]: ",timing->h_total);
        timing->h_total=getInt(true);
        printf("\r\nVertical front porch (lines)[%d]: ",timing->v_front_porch);
        timing->v_front_porch=getInt(true);
        printf("\r\nVertical sync pulse width (lines)[%d]: ",timing->v_pulse);
        timing->v_pulse=getInt(true);
        printf("\r\nVertical total lines[%d]: ",timing->v_total);
        timing->v_total =getInt(true);
        printf("\r\nClock polarity (0 or 1)[%d]: ",timing->clock_polarity);
        timing->clock_polarity =getInt(true);

        timing->h_sync_polarity = 1;
        timing->v_sync_polarity = 1;
        timing->enable_clock = 1;
        timing->enable_den = 1;


        printf("\r\nYou entered:\r\n");
        print_timing_settings(*timing,sys_clk);;

        printf("S: Save, R: Retry, C: Cancel\r\n");
        int c = stdio_getchar();
        if (c == 'S' || c == 's') {
            *sysClkKHz = sys_clk;
            return true;
        }
        else if (c == 'C' || c == 'c') {
            return false;
        }

    } while (true);

}

// Get Custom video mode settings from user (not used)
bool get_custom_mode(scanvideo_mode_t *mode)
{
    do
    {

        printf("Horizontal resolution (pixels): ");
        mode->width=getInt(true);
        printf("Vertical resolution (lines): ");
        mode->height=getInt(true);
        printf("X scale (1=normal, 2=double width etc): ");
        mode->xscale=getInt(true);
        printf("Y scale (1=normal, 2=double height etc): ");
        mode->yscale=getInt(true);

        mode->pio_program = &video_24mhz_composable;

        printf("You entered:\r\n"
            " Resolution: %hu x %hu\r\n"
            " X scale: %d\r\n"
            " Y scale: %hu\r\n",
            mode->width, mode->height,
            mode->xscale,
            mode->yscale);

        printf("S: Save, R: Retry, C: Cancel\r\n");
        int c = stdio_getchar();
        if (c == 'S' || c == 's') {
            return true;
        }
        else if (c == 'C' || c == 'c') {
            return false;
        }
    } while (true);
}

bool clear_custom_timing()
{
    scanvideo_timing_t *timing = (scanvideo_timing_t *)dcdu_mode_480x234_60.default_timing;
    scanvideo_mode_t *mode = (scanvideo_mode_t *)&dcdu_mode_480x234_60;
    printf("Clearing custom timings from Flash Memory.");
    // write to flash
    return update_flash_settings(timing, mode,SysClkForPixClk(timing->clock_freq),0x1DC);
}

// get video timing settings and write them to flash to be user
// on the next program startup
bool program_video_timing()
{
    printf("Select Video Timings: \r\n"
        "1: 480x234 @60Hz (DCDU) \r\n"
        "2: 480x272 @60Hz (PSP) \r\n"
        "3: 640x480 @60Hz (VGA) \r\n"
        "4: 768x256 @60Hz (1X3) \r\n"
        "5: Custom\r\n"
        "6: Clear Flash\r\n"
        "7: Cancel\r\n"
        );

    const scanvideo_timing_t *timing;
    const scanvideo_mode_t *mode;
    scanvideo_timing_t custom_timing = video_timing;
    scanvideo_mode_t custom_mode = video_mode;
    uint32_t sysClkKHz;
    uint32_t flag = FLAG;

    int c = stdio_getchar();
    switch (c) {
        case '1':
            timing = &dcdu_timing_480x234_60_default;
            mode = &dcdu_mode_480x234_60;
            sysClkKHz = SysClkForPixClk(timing->clock_freq);
            printf("Writing DCDU timings to Flash Memory\r\n");
            break;
        case '2':
            timing = &psp_timing_480x272_60_default;
            mode = &psp_mode_480x272_60;
            sysClkKHz = SysClkForPixClk(timing->clock_freq);
            printf("Writing PSP timings to Flash Memory\r\n");
            break;
        case '3':
            timing = &vga_timing_640x480_60_default;
            mode = &vga_mode_640x480_60;
            sysClkKHz = SysClkForPixClk(timing->clock_freq);
            printf("Writing VGA timings to Flash Memory\r\n");
            break;
        case '4':
            timing = &ay_mode_768x256_60_default;
            mode = &ay_mode_768x256_60;
            sysClkKHz = SysClkForPixClk(timing->clock_freq);
            printf("Writing 1X3 timings to Flash Memory\r\n");
            break;
        case '5':
            // Custom timing settings
            sysClkKHz = clock_get_hz(clk_sys)/1000;
            if (get_custom_timing(&custom_timing,&sysClkKHz )) //&& get_custom_mode(&mode)
            {
                timing = &custom_timing;
                custom_mode.height =timing->v_active;
                custom_mode.width = timing->h_active;
                custom_mode.default_timing = timing;
                custom_mode.xscale = 1;
                custom_mode.yscale = 1;
                custom_mode.pio_program = &video_24mhz_composable;
                mode = &custom_mode;
                printf("Writing Custom timings to Flash Memory:\r\n");
            }
            else
            {
                printf("Update Canceled\r\n");
                return false; // Cancel
            }   
            break;
        case '6':
            return clear_custom_timing();
            break;
        case PICO_ERROR_TIMEOUT:
        default:
            printf("Update Canceled\r\n");
            return false; // Cancel
    }

    // write to flash
    print_timing_settings(*timing,sysClkKHz);
    print_mode_settings(*mode);
    update_flash_settings(timing, mode,sysClkKHz,flag);
    return true;
}

// debug code to print block of memory
void print_buf(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
        if (i % 16 == 15)
            printf("\n");
        else
            printf(" ");
    }
} 


void print_help()
{
    puts("Test screens:\r\n"
         "  SPACE: cycle patterns\r\n"
         "  r: (R)ed\r\n"
         "  g: (G)reen\r\n"
         "  b: (B)lue\r\n"
         "  c: (C)yan\r\n"
         "  m: (M)agenta\r\n"
         "  y: (Y)ellow\r\n"
         "  w: (W)hite\r\n"
         "  k: blac(K)\r\n"
         "  s: color (S)tripes\r\n"
         "  e: border along (E)dge\r\n"
         "  t: (T)ext test\r\n"
         "  a: (A)nimate\r\n"
         "  3: (3)2 grey shades\r\n"
         "  n: (N)ew custom color\r\n"
         "  l: 5x5 grid of white (L)ines\r\n"
         "  x: white bo(X) on black screen\r\n"
         "  p: (P)rogram new video timings\r\n"
         );
}

void get_custom_color(volatile uint8_t*red,volatile uint8_t*green,volatile uint8_t*blue)
{
    printf("pick custom RGB color, currently (%d,%d,%d)\r\n",*red,*green,*blue);
    printf("Red:");
    *red=getInt(true);
    printf("\r\nGreen:");
    *green=getInt(true);
    printf("\r\nBlue:");
    *blue=getInt(true);

}

/******************************************************** 
//
// Main Loop (runs on Core 0)
//
********************************************************/
int main(void) {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN,true);    
    gpio_put(PICO_DEFAULT_LED_PIN,true);
    busy_wait_ms(100);
    gpio_put(PICO_DEFAULT_LED_PIN,false);
    gpio_init(CYCLE_BUTTON_PIN);
    gpio_set_dir(CYCLE_BUTTON_PIN,false);
    gpio_set_pulls(CYCLE_BUTTON_PIN,true,false);


    printf("\r\vPico LCD Test Pattern Generator Version %s\r\n",RELEASE);

    uint32_t clk = clock_get_hz(clk_sys);
    printf("Default sysclk = %dHz\r\n",clk);

    uint32_t sysClkKHz;

    if (!gpio_get(CYCLE_BUTTON_PIN))
    {
        printf("Button held down - cdelaying start by 5 seconds\r\n");
        busy_wait_ms(5000);
    }

    // read video timing from flash (if they were previously stored)
    if (!get_flash_settings(&video_timing,&video_mode,&sysClkKHz))
    {
        sysClkKHz = SysClkForPixClk(video_mode.default_timing->clock_freq);
        printf("suggested sysclk for %dHz pix Clock is %dkHz\r\n",video_mode.default_timing->clock_freq,sysClkKHz);
    }


    set_sys_clock_khz(sysClkKHz, true);
    // init uart now that clk_frequency has changed
    gpio_put(PICO_DEFAULT_LED_PIN,true);
    stdio_init_all();
    busy_wait_ms(100);

    print_timing_settings(video_timing,sysClkKHz);

    clk = clock_get_hz(clk_sys);
    int divider= clk / video_mode.default_timing->clock_freq;
    printf("New PixelClock = %d, sysclock=%dkHz, clock divider=%d\r\n",video_mode.default_timing->clock_freq,clk/1000,divider);

#if PICO_SCANVIDEO_ENABLE_CLOCK_PIN
#ifndef PICO_SCANVIDEO_ENABLE_DEN_PIN
    printf("Warning: #Define PICO_SCANVIDEO_ENABLE_CLOCK_PIN requires DEN pin to also be enabled\r\n"
            "You must #define PICO_SCANVIDEO_ENABLE_DEN_PIN\r\n");
#endif
    if (divider & 1) {
        printf("ERROR: To enable pixel clock, the System clock (%dK) must be an integer multiple of 2 times the requested pixel clock (%d).", SYS_CLK_KHZ, video_mode.default_timing->clock_freq);
    }
#else
    if (video_mode.default_timing->enable_clock) {
        printf("Warning: Pixel clock output enabled, but PICO_SCANVIDEO_ENABLE_CLOCK_PIN is not defined!\n");
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

    bool last_button_pos=true;  

    while (true) {
        // prevent tearing when we change - if you're astute you'll notice this actually causes
        // a fixed tear a number of scanlines from the top. this is caused by pre-buffering of scanlines
        // and is too detailed a topic to fix here.
        scanvideo_wait_for_vblank();
        if (last_button_pos != gpio_get(CYCLE_BUTTON_PIN))
        {
            last_button_pos = gpio_get(CYCLE_BUTTON_PIN);
            if (!last_button_pos)
            {
                pattern = (Pattern)((pattern + 1) % max_pattern);
            }
        }
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
            case '3':
                pattern = grey;
                break;
            case 'a':
                pattern = animate;
                break;
            case 'n':
                pattern = custom;
                get_custom_color(&custom_red,&custom_green,&custom_blue);
                break;
            case 'l':
                pattern = lines;
                break;
            case 'x':
                pattern = box;
                break;
            case 'p':
                program_video_timing();
                break;
            case 'z':
                scanvideo_timing_t t;
                scanvideo_mode_t m;
                uint32_t s;
                if (get_flash_settings(&t,&m,&s))
                {
                    print_timing_settings(t,s);
                    continue;
                }
                else
                {
                    printf("No custom timing stored in flash.\r\n");
                }
                break;
            case PICO_ERROR_TIMEOUT:
                break;
            default:
                print_help();
                break;
        }
        if (c != PICO_ERROR_TIMEOUT)
            printf("Pattern: %s\r\n", pattern_to_string(pattern));
    }
}



/*************************************************************************
 *
 * Functions below this comment all run on core 1 (rendering the screens)
 * 
 **************************************************************************
 */


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

// Black screen with 5x5 grid squares
void draw_grid(scanvideo_scanline_buffer_t *scanline_buffer)
{
    
    uint line_num = scanvideo_scanline_number(scanline_buffer->scanline_id);
    
    int vspacing = video_mode.height / 5;
    int hspacing = video_mode.width / 5;
    if ( (line_num % vspacing == 0) && (line_num > 0) && (line_num < vspacing*5)) {
        draw_color_line(scanline_buffer, pattern_to_color(white));
    } 
    else
    {
        uint16_t *p = (uint16_t *)scanline_buffer->data;
        
        for (int i=0;i<4;i++)
        {
        *p++ = COMPOSABLE_COLOR_RUN;
        *p++ = pattern_to_color(black);
        *p++ = hspacing-3;
        *p++ = COMPOSABLE_RAW_1P;
        *p++ = pattern_to_color(white);
        }

        *p++ = COMPOSABLE_COLOR_RUN;
        *p++ = pattern_to_color(black);
        *p++ = video_mode.width - (4*(hspacing+1)) -4;

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



// This function is called for each scan line of the display
// it is set to run on core 1 leaving core 0 to run the UI and other tasks
void __time_critical_func(render_graphics)() {
    static int x=50;
    static int y=50;
    static int dx=1;
    static int dy=1;
    static uint16_t last_frame_num=0;
    static uint32_t color=1;
    int box_width = video_mode.width/6;
    int box_height = 4*video_mode.height/(6*3);
    

    // initialize video and interrupts on core 1
    scanvideo_setup(&video_mode);
    scanvideo_timing_enable(true);
    sem_release(&video_initted);

    while (true) {
        if(wait_for_flash)
        {
            printf(" Video Stopped\r\n");

            flash_safe_execute_core_init();
            while(wait_for_flash)
            { /* wait*/ }
            flash_safe_execute_core_deinit();

            printf(" Video started\r\n");
        }
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
        else if (pattern <= custom) 
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
                if ((x <= 0) || (x > (video_mode.width - box_width))) 
                {
                    dx = -dx;
                    x += dx;
                    color = (color+1)%(int)white + 1;
                }
                if ((y <= 0) || (y > (video_mode.height - box_height))) 
                {
                    dy = -dy;
                    y += dy;
                    color = (color+1)%(int)white + 1;
                }
            }
            draw_box(scanline_buffer, x, y, box_width, box_height, pattern_to_color((Pattern)color));
        }
        else if (pattern == lines)
        {
            draw_grid(scanline_buffer);
        }
        else if (pattern == box)
        {
            draw_box(scanline_buffer,video_mode.width * 1/3,video_mode.height * 1/3,video_mode.width /3,video_mode.height / 3,pattern_to_color(white));
        }

        scanvideo_end_scanline_generation(scanline_buffer);
    }
}
