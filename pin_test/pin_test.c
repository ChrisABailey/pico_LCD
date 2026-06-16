/*
 * Pico GPIO Pin Tester
 * --------------------
 * Interactive jumper-based connectivity / function test for the GPIO pins on a
 * Raspberry Pi Pico or Pico 2 (RP2040 / RP2350).
 *
 * Idea: the program walks through pairs of pins. For each pair you place a
 * single jumper wire between the two named pins, then the program:
 *   1. drives pin A as an output and reads pin B as an input (tests A-output,
 *      B-input, and the wire),
 *   2. swaps the roles (B drives, A reads),
 *   3. for ADC-capable pins (GP26/27/28) additionally checks the analog input
 *      by reading the ADC while the partner pin is driven high and low.
 * It reports PASS / FAIL per pin and a summary at the end, then lets you
 * re-test any single pair (the "move the jumper and repeat" flow).
 *
 * Interface: USB serial (115200, but USB CDC ignores baud). Open a terminal
 * such as `minicom`, `screen`, `picocom`, or the Arduino/Thonny serial monitor.
 *
 * NOTE on internal pins (NOT tested, do not jumper them):
 *   GP23 - SMPS power-save control      GP24 - VBUS sense
 *   GP25 - onboard LED                  GP29 - VSYS/3 ADC sense
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

// ---------------------------------------------------------------------------
// Pin set
// ---------------------------------------------------------------------------
// User-accessible GPIOs on the 40-pin header. GP23/24/25/29 are internal and
// deliberately excluded. Listed in a flat array; auto-walk pairs consecutive
// entries (0,1),(2,3),... giving 13 pairs covering all 26 pins.
static const uint8_t TEST_PINS[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 26, 27, 28
};
#define NUM_TEST_PINS (sizeof(TEST_PINS) / sizeof(TEST_PINS[0]))
#define NUM_PAIRS     (NUM_TEST_PINS / 2)

// ADC-capable GPIOs map to ADC channels: GP26->0, GP27->1, GP28->2.
static bool is_adc_pin(uint pin) { return pin == 26 || pin == 27 || pin == 28; }
static uint adc_channel_for(uint pin) { return pin - 26; }

// Settle time after changing a pin's drive state before sampling.
#define SETTLE_US 200

// ---------------------------------------------------------------------------
// Serial helpers
// ---------------------------------------------------------------------------
static void wait_for_key(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    // Drain any pending input, then block for one real keypress.
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { /* flush */ }
    while (getchar_timeout_us(100 * 1000) == PICO_ERROR_TIMEOUT) { /* wait */ }
    printf("\n");
}

// Read a single non-whitespace character (blocking). Returns lowercase.
static int read_char(void) {
    int c;
    do {
        c = getchar_timeout_us(100 * 1000);
    } while (c == PICO_ERROR_TIMEOUT || c == '\r' || c == '\n' || c == ' ');
    if (c >= 'A' && c <= 'Z') c += 32;
    return c;
}

// Read a small unsigned integer terminated by Enter. Returns -1 if empty/invalid.
static int read_int(void) {
    char buf[8];
    int n = 0;
    while (n < (int)sizeof(buf) - 1) {
        int c = getchar_timeout_us(60 * 1000 * 1000);
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == '\r' || c == '\n') {
            if (n == 0) continue; // ignore leading newlines
            break;
        }
        if (c < '0' || c > '9') return -1;
        putchar(c); // echo
        buf[n++] = (char)c;
    }
    printf("\n");
    if (n == 0) return -1;
    buf[n] = 0;
    return atoi(buf);
}

// ---------------------------------------------------------------------------
// Digital connectivity test for one direction (out -> in).
// ---------------------------------------------------------------------------
// Drives `out` high then low; with a pull-down on `in`, a good wire makes `in`
// follow `out`. Returns true on success. On failure, prints the reason.
static bool test_direction(uint out, uint in) {
    gpio_init(out);
    gpio_init(in);
    gpio_set_dir(out, GPIO_OUT);
    gpio_set_dir(in, GPIO_IN);

    bool ok = true;

    // Phase 1: input pull-down. Expect to read what `out` drives.
    gpio_pull_down(in);

    gpio_put(out, 1);
    sleep_us(SETTLE_US);
    if (!gpio_get(in)) {
        printf("    FAIL: drove GP%u HIGH, GP%u read LOW "
               "(open wire, GP%u stuck-low output, or GP%u stuck-low input)\n",
               out, in, out, in);
        ok = false;
    }

    gpio_put(out, 0);
    sleep_us(SETTLE_US);
    if (gpio_get(in)) {
        printf("    FAIL: drove GP%u LOW, GP%u read HIGH "
               "(GP%u stuck-high output or GP%u stuck-high input)\n",
               out, in, out, in);
        ok = false;
    }

    // Phase 2: input pull-up while driving LOW. A healthy output must sink the
    // pull-up; a healthy input must still read the driven LOW. Catches weak /
    // open outputs that Phase 1's pull-down would mask.
    gpio_pull_up(in);
    gpio_put(out, 0);
    sleep_us(SETTLE_US);
    if (gpio_get(in)) {
        printf("    FAIL: GP%u driven LOW could not overcome pull-up on GP%u "
               "(weak/open output GP%u, or open wire)\n",
               out, in, out);
        ok = false;
    }

    gpio_disable_pulls(in);
    return ok;
}

// Full digital test of a pair, both directions. Returns true if both pass.
static bool test_pair_digital(uint a, uint b) {
    printf("  Digital GP%u <-> GP%u\n", a, b);
    bool ab = test_direction(a, b);
    if (ab) printf("    OK: GP%u output -> GP%u input\n", a, b);
    bool ba = test_direction(b, a);
    if (ba) printf("    OK: GP%u output -> GP%u input\n", b, a);
    return ab && ba;
}

// ---------------------------------------------------------------------------
// Analog (ADC) test: drive `driver` high/low and read the ADC on `adc_pin`.
// ---------------------------------------------------------------------------
#define ADC_MAX 4095
static bool test_adc(uint adc_pin, uint driver) {
    printf("  Analog GP%u (ADC%u), driven by GP%u\n",
           adc_pin, adc_channel_for(adc_pin), driver);

    adc_gpio_init(adc_pin);            // high-Z analog mode on the ADC pin
    adc_select_input(adc_channel_for(adc_pin));

    gpio_init(driver);
    gpio_set_dir(driver, GPIO_OUT);

    bool ok = true;

    gpio_put(driver, 1);
    sleep_us(SETTLE_US);
    uint16_t hi = adc_read();
    if (hi < (ADC_MAX * 3 / 4)) {       // expect near full-scale (>~3072)
        printf("    FAIL: driver HIGH, ADC read %u (expected > %u)\n",
               hi, ADC_MAX * 3 / 4);
        ok = false;
    }

    gpio_put(driver, 0);
    sleep_us(SETTLE_US);
    uint16_t lo = adc_read();
    if (lo > (ADC_MAX / 4)) {           // expect near zero (<~1024)
        printf("    FAIL: driver LOW, ADC read %u (expected < %u)\n",
               lo, ADC_MAX / 4);
        ok = false;
    }

    if (ok) printf("    OK: ADC reads %u (high) / %u (low)\n", hi, lo);

    // Return the ADC pin to plain digital state for any later digital re-test.
    gpio_init(adc_pin);
    return ok;
}

// ---------------------------------------------------------------------------
// Test one pair (digital + analog where applicable). Updates per-pin results.
// ---------------------------------------------------------------------------
// pin_ok[] is indexed by GPIO number (0..28).
static void run_pair(uint a, uint b, bool pin_ok[29]) {
    char prompt[96];
    snprintf(prompt, sizeof(prompt),
             ">> Place a jumper between GP%u and GP%u, then press any key... ",
             a, b);
    wait_for_key(prompt);

    bool ok = test_pair_digital(a, b);

    if (is_adc_pin(a)) ok &= test_adc(a, b);
    if (is_adc_pin(b)) ok &= test_adc(b, a);

    if (!ok) {
        pin_ok[a] = false;
        pin_ok[b] = false;
    }
    printf("  Pair result: %s\n\n", ok ? "PASS" : "FAIL (see above)");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    stdio_init_all();
    adc_init();

    // Wait for the USB serial terminal to attach.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(300);

    printf("\n=== Raspberry Pi Pico GPIO Pin Tester ===\n");
    printf("Tests GP0-GP22 and GP26-GP28 (ADC). Internal pins GP23/24/25/29 are skipped.\n");
    printf("Use jumper wires only between the pins named in the prompts.\n\n");

    while (true) {
        bool pin_ok[29];
        for (int i = 0; i < 29; i++) pin_ok[i] = true;

        printf("--- Auto-walk: %u pairs, move one jumper at a time ---\n\n",
               (unsigned)NUM_PAIRS);

        for (uint p = 0; p < NUM_PAIRS; p++) {
            uint a = TEST_PINS[2 * p];
            uint b = TEST_PINS[2 * p + 1];
            printf("[Pair %u/%u]\n", p + 1, (unsigned)NUM_PAIRS);
            run_pair(a, b, pin_ok);
        }

        // Summary
        printf("================= SUMMARY =================\n");
        bool any_fail = false;
        for (uint i = 0; i < NUM_TEST_PINS; i++) {
            uint pin = TEST_PINS[i];
            if (!pin_ok[pin]) {
                printf("  GP%-2u : SUSPECT\n", pin);
                any_fail = true;
            }
        }
        if (!any_fail) {
            printf("  All %u tested pins PASSED.\n", (unsigned)NUM_TEST_PINS);
        } else {
            printf("\n  NOTE: a fault flags BOTH pins in the pair. Re-test a\n");
            printf("  suspect pin against a known-good partner to isolate it.\n");
        }
        printf("===========================================\n\n");

        // Optional single-pair re-test loop ("move the jumper and repeat").
        printf("Options: [r] re-test a single pair   [a] run full auto-walk again\n");
        int c = read_char();
        if (c == 'r') {
            printf("Enter first GP number: ");
            fflush(stdout);
            int a = read_int();
            printf("Enter second GP number: ");
            fflush(stdout);
            int b = read_int();
            if (a < 0 || b < 0 || a > 28 || b > 28 || a == b) {
                printf("Invalid pins.\n\n");
            } else {
                bool tmp[29];
                for (int i = 0; i < 29; i++) tmp[i] = true;
                printf("\n");
                run_pair((uint)a, (uint)b, tmp);
            }
        }
        // any other key (or after a re-test) loops back to a fresh auto-walk.
    }
    return 0;
}
