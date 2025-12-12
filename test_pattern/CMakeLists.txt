if (TARGET pico_scanvideo_dpi)
    add_executable(test_pattern
            test_pattern.c
            )

    target_compile_definitions(test_pattern 
        PRIVATE
        PICO_SCANVIDEO_ENABLE_CLOCK_PIN=1
        PICO_SCANVIDEO_ENABLE_DEN_PIN=1
        )

    set(PICO_DEFAULT_BINARY_TYPE copy_to_ram)
    set(PICO_COPY_TO_RAM,1)

    target_link_libraries(test_pattern PRIVATE
            pico_multicore
            pico_stdlib
            hardware_flash
            pico_scanvideo_dpi)

    pico_add_extra_outputs(test_pattern)

    #pico_enable_stdio_uart(test_pattern 1)
    pico_enable_stdio_usb(test_pattern 1)
endif ()
