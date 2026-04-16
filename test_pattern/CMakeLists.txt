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

    set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)
    set(PICO_COPY_TO_RAM,1)

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
