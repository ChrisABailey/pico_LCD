# Pico GPIO Pin Tester

Interactive jumper-based test that verifies each user-accessible GPIO on a
Raspberry Pi Pico / Pico 2 works as both a digital output and input, and that
GP26–GP28 work as ADC analog inputs.

## How it works

The program walks through **13 pairs** covering GP0–GP22 and GP26–GP28. For
each pair you place a single jumper wire between the two named pins and press a
key. The program then:

1. drives pin **A** as output, reads pin **B** as input (high and low),
2. swaps roles (B drives, A reads),
3. for an ADC pin, reads the ADC while the partner drives high then low.

A fault flags **both** pins in the pair as `SUSPECT`. Use the `r` option at the
end to re-test a suspect pin against a known-good partner to isolate which pin
is actually bad.

Internal pins **GP23, GP24, GP25, GP29** are not on the header for testing and
are skipped — do not jumper them.

## Build

```bash
cd pin_test

# Pico 2 (RP2350) — default
cmake -B build && cmake --build build

# Original Pico (RP2040)
cmake -B build-pico -DPICO_BOARD=pico && cmake --build build-pico
```

The SDK is found via `PICO_SDK_PATH`, or automatically from `../../pico-sdk`
(the copy beside this repo).

## Flash & run

1. Hold BOOTSEL, plug in the Pico, release. Copy `build/pin_test.uf2` to the
   `RPI-RP2` drive (or use `picotool load build/pin_test.uf2`).
2. Open the USB serial port: `screen /dev/tty.usbmodem* 115200`
   (macOS), `minicom`, `picocom`, etc. Baud is ignored on USB CDC.
3. Follow the prompts: move the jumper as instructed, press a key per pair.

## Pairing scheme

```
(GP0,GP1) (GP2,GP3) (GP4,GP5) (GP6,GP7) (GP8,GP9) (GP10,GP11)
(GP12,GP13) (GP14,GP15) (GP16,GP17) (GP18,GP19) (GP20,GP21)
(GP22,GP26) (GP27,GP28)
```
