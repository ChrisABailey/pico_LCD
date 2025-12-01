# Raspberry Pi Pico LCD Driver
Code for driving test patterns to an LCD using a parallel (ttl) interface.

Based on code from the [pico-playground](https://github.com/raspberrypi/pico-playground) and [pico-extras](https://github.com/raspberrypi/pico-extras)

These also require the PICO SDK which should be pointed with the PICO_SDK_PATH environment variable.

This can be added to environment or can be added in settings->extensions->CMake Tools Build Environment

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

