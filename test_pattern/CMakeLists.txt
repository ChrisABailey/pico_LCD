if (TARGET pico_scanvideo_dpi)
    add_executable(test_pattern
            test_pattern.c
            )

    target_compile_definitions(test_pattern
        PRIVATE
        PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1
        PICO_SCANVIDEO_ENABLE_DEN_PIN=1
        # RAW_RUN for a full-width (480px) bitmap needs 244 uint32_t words per
        # scanline buffer.  The library default of 180 is too small and causes
        # buffer overflows (and hangs) for bitmaps wider than ~350 pixels.
        PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=256
        )

    # copy_to_ram copies the entire binary into SRAM so all code runs from RAM,
    # avoiding XIP latency during scanline rendering.  Only safe for the plain
    # pico2 build.  The pico2_w build with BTstack Classic is ~442 KB of code +
    # 278 KB BSS = 720 KB total, which far exceeds the RP2350's 520 KB SRAM.
    # With copy_to_ram the C startup BSS-zeroing loop overruns SRAM, corrupting
    # the stack; the debugger then shows a misleading backtrace pointing at
    # stdio_init_all / tud_rhport_init even though main() never ran.
    # Without copy_to_ram only the 278 KB BSS lives in SRAM; code runs from XIP.
    # Time-critical rendering functions are already marked __time_critical_func.
    if(NOT PICO_CYW43_SUPPORTED)
        pico_set_binary_type(test_pattern copy_to_ram)
    endif()

    target_link_libraries(test_pattern PRIVATE
            pico_multicore
            pico_stdlib
            hardware_flash
            pico_scanvideo_dpi)

    pico_add_extra_outputs(test_pattern)

    pico_enable_stdio_uart(test_pattern 0)
    pico_enable_stdio_usb(test_pattern 1)

    # Pico 2 W — Bluetooth serial via BTstack Classic SPP.
    # PICO_CYW43_SUPPORTED is automatically set to 1 by the board header for
    # pico_w and pico2_w; it is absent (or 0) for plain pico2 builds.
    if(PICO_CYW43_SUPPORTED)
        # btstack_config.h lives in the same directory as test_pattern.c.
        # Adding it to the include path makes it findable by BTstack library files.
        target_include_directories(test_pattern PRIVATE ${CMAKE_CURRENT_LIST_DIR})
        target_compile_definitions(test_pattern PRIVATE
            PICO_BT_SERIAL_ENABLED=1
            # The pico2_w binary is ~442 KB of code in flash.  The default
            # LCD_SETTINGS_OFFSET of 256 KB falls inside the code binary and
            # update_flash_settings() would corrupt the firmware.  Use 512 KB
            # instead, which is safely above the code end and below the 1 MB
            # bitmap region.
            LCD_SETTINGS_OFFSET=524288
        )
        target_link_libraries(test_pattern PRIVATE
            # pico_cyw43_arch_none: CYW43 driver WITHOUT lwIP (sets CYW43_LWIP=0).
            # Despite the name it still uses the threadsafe_background async context
            # (PICO_CYW43_ARCH_THREADSAFE_BACKGROUND=1) — required for BTstack callbacks.
            # Do NOT use pico_cyw43_arch_threadsafe_background here: that variant
            # unconditionally pulls in the lwIP network stack, causing linker errors
            # when lwip/netif.h is not present.
            pico_cyw43_arch_none
            # CYW43 + BTstack integration layer (async context wiring, HCI transport)
            pico_btstack_cyw43
            # Classic Bluetooth protocol stack (L2CAP, RFCOMM, SDP, SPP)
            pico_btstack_classic
            pico_btstack_base
        )
    endif()
endif ()
