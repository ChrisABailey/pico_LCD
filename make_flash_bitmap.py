#!/usr/bin/env python3
"""make_flash_bitmap.py — convert a PNG to a flash-ready BGRA5515 bitmap.

The output binary has an 8-byte header followed by raw BGRA5515 pixel data:

    Offset  Size  Field
    0       4     Magic (0x42494D50 = "BIMP")
    4       2     Width  (pixels, little-endian)
    6       2     Height (pixels, little-endian)
    8       W*H*2 BGRA5515 pixel data, row-major

The bitmap is stored at flash offset 0x100000 (1 MB from the start of flash),
which maps to XIP address 0x10100000 on RP2350.

Usage:
    python3 make_flash_bitmap.py <input.png> <output.bin>

Flash with picotool (Pico must be in BOOTSEL mode, or running picotool-accessible fw):
    picotool load <output.bin> -o 0x10100000

Or convert to UF2 for drag-and-drop (requires uf2conv in PATH):
    uf2conv -f rp2350 -b 0x10100000 <output.bin> -o <output.uf2>
    # then copy <output.uf2> to the Pico RPI-RP2 drive

The image dimensions should match the active display resolution.  If they do
not, the pattern will render correctly for lines within the bitmap height and
fill remaining lines with black, but the width MUST be <= the display width to
avoid a scanline buffer overflow.

Supported display resolutions:
    DCDU  480x234     PSP  480x272     AY  768x256     VGA  640x480
"""

import struct
import sys
import os
import subprocess
import tempfile

BITMAP_MAGIC  = 0x42494D50   # "BIMP"
BITMAP_OFFSET = 0x10100000   # XIP_BASE (0x10000000) + LCD_BITMAP_OFFSET (0x100000)


def find_packtiles():
    """Return the path to the packtiles script, searching from this file's location."""
    here = os.path.dirname(os.path.abspath(__file__))
    candidate = os.path.join(here, "flash_stream", "img", "packtiles")
    if os.path.isfile(candidate):
        return candidate
    raise FileNotFoundError(
        f"Cannot find packtiles at {candidate}\n"
        "Make sure you are running this script from the repo root."
    )


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        print("Usage: python3 make_flash_bitmap.py <input.png> <output.bin>")
        sys.exit(1)

    input_png  = sys.argv[1]
    output_bin = sys.argv[2]

    if not os.path.isfile(input_png):
        print(f"Error: input file not found: {input_png}", file=sys.stderr)
        sys.exit(1)

    # Get image dimensions.
    try:
        from PIL import Image
    except ImportError:
        print("Error: Pillow is not installed.  Run:  pip install Pillow", file=sys.stderr)
        sys.exit(1)

    with Image.open(input_png) as img:
        width, height = img.size

    print(f"Input:  {input_png}  ({width}x{height} pixels)")

    packtiles = find_packtiles()

    # Run packtiles to produce a raw binary (no .h extension → raw bytes output).
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        raw_path = tmp.name
    try:
        result = subprocess.run(
            [sys.executable, packtiles, "-sf", "bgar5515", input_png, raw_path],
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout.strip():
            print(result.stdout.strip())

        with open(raw_path, "rb") as f:
            pixel_data = f.read()
    finally:
        if os.path.exists(raw_path):
            os.unlink(raw_path)

    expected_bytes = width * height * 2
    if len(pixel_data) != expected_bytes:
        print(
            f"Error: expected {expected_bytes} bytes of pixel data, got {len(pixel_data)}",
            file=sys.stderr,
        )
        sys.exit(1)

    # Write: 8-byte header + pixel data.
    with open(output_bin, "wb") as f:
        f.write(struct.pack("<IHH", BITMAP_MAGIC, width, height))
        f.write(pixel_data)

    total_bytes = 8 + len(pixel_data)
    print(f"Output: {output_bin}  ({total_bytes} bytes, {total_bytes // 1024} KB)")
    print()
    print("Flash commands (Pico must be in BOOTSEL mode):")
    print()
    print(f"  picotool:")
    print(f"    picotool load {output_bin} -o 0x{BITMAP_OFFSET:08X}")
    print()
    print(f"  uf2conv (drag-and-drop to RPI-RP2 drive):")
    uf2_name = os.path.splitext(output_bin)[0] + ".uf2"
    print(f"    uf2conv -f rp2350 -b 0x{BITMAP_OFFSET:08X} {output_bin} -o {uf2_name}")
    print()
    print(f"Then press 'i' in the serial terminal to display the bitmap.")
    if width not in (480, 640, 768) or height not in (234, 256, 272, 480):
        print()
        print(f"Note: {width}x{height} is not a standard display resolution for this firmware.")
        print("      Supported: 480x234 (DCDU), 480x272 (PSP), 768x256 (AY), 640x480 (VGA)")


if __name__ == "__main__":
    main()
