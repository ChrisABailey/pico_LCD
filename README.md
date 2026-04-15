# Raspberry Pi Pico LCD Driver
Code for driving test patterns to an LCD using a parallel (ttl) interface.

Based on code from the [pico-playground](https://github.com/raspberrypi/pico-playground) and [pico-extras](https://github.com/raspberrypi/pico-extras)

These also require the PICO SDK which should be pointed with the PICO_SDK_PATH environment variable.

This can be added to environment or can be added in settings->extensions->CMake Tools Build Environment

the settings.json file in .vscode folder looks like the following (the path may vary based on the location that the PICO SDK is stored):

```json
{
    "cmake.environment": {
        "PICO_SDK_PATH": "../../../pico-sdk"
    },
    "cmake.configureArgs": [
        "-DPICO_PLATFORM=rp2350", 
        "-DPICO_BOARD=pico2"
    ]
}
```

## Full Applications

test_pattern.c

- Displays solid fields of red, green, blue, white, black, cyan, yellow, magenta
- Generates a 32 greyshade test patern using the above colors
- Animation (bouncing rectangle)
- LCD border (single pixel white boarder with inner single red pixel on black screen)
- Bitmap (sample text)

Patterns are selectable using a serial terminal connected to the pico USB

![screenshot](test_pattern/screenshot.jpg)

pinnout of the pico is:
![pinout](pinout.png)

## Scanout Video

In _scanout_ video, every pixel is driven by the PIO every frame, and a framebuffer is not (necessarily) used (which
is useful when you only have 264K of RAM).

Currently the library only uses 5bits per color (32 greysghades)

For a fuller description of scanout video see [here](https://github.com/raspberrypi/pico-extras/blob/master/src/common/pico_scanvideo/README.adoc)

For displaying a bitmap encode the bitmap into a C byte array using the python [packtiles program](flash_stream/img/packtiles)

Specifically call: `./packtiles -sf bgar5515 text_box.png textbox.h ` to encode a png into a bytearray in a c header "textbox.h".  Also update the macros for height and width

## Custom Bitmap Pattern

The firmware supports a user-supplied full-screen bitmap stored directly in
flash at a fixed offset (1 MB from the start of flash).  No recompile is
needed — you only need to reflash the bitmap data when you want to change it.

### Requirements

- Python 3 with [Pillow](https://pillow.readthedocs.io/) (`pip install Pillow`)
- Either **picotool** or **uf2conv** to flash the image

### Preparing the image

1. Create a PNG at the resolution of your display:
   - DCDU: 480×234 — PSP: 480×272 — AY: 768×256 — VGA: 640×480
2. Run `make_flash_bitmap.py` from the repo root:

```bash
python3 make_flash_bitmap.py my_image.png my_image.bin
```

This converts the PNG to BGRA5515 format, prepends an 8-byte header
(`BIMP` magic + width + height), and writes the result to `my_image.bin`.
The script prints the exact flash commands to use.

### Flashing with picotool

Put the Pico in BOOTSEL mode (hold BOOTSEL while connecting USB), then:

```bash
picotool load my_image.bin -o 0x10100000
picotool reboot
```

### Flashing with uf2conv (drag-and-drop)

```bash
uf2conv -f rp2350 -b 0x10100000 my_image.bin -o my_image.uf2
```

Copy `my_image.uf2` to the `RPI-RP2` drive that appears when the Pico is in
BOOTSEL mode.  The Pico reboots automatically.

> **Note:** flashing the bitmap does **not** overwrite the firmware — the bitmap
> is stored at 1 MB and the firmware occupies the first ~100 KB.  The display
> timing settings (stored at 256 KB) are also unaffected.

### Displaying the bitmap

Press **`i`** in the serial terminal to switch to the custom bitmap pattern.
The firmware reports at startup whether a valid bitmap is present:

```
Custom bitmap in flash: 480x234 pixels
```

If the stored bitmap width does not match the current display width the
pattern falls back to a black screen.  Reflash a correctly-sized bitmap
after changing the display timing mode.

### Flash memory map

| Region | Offset | Size | Contents |
|--------|--------|------|----------|
| Firmware | 0x000000 | ~100 KB | Compiled binary (copy_to_ram) |
| Settings | 0x040000 | 4 KB | Video timing (saved by `p` command) |
| Bitmap | 0x100000 | up to ~3 MB | Custom bitmap (`BIMP` header + BGRA5515 pixels) |

