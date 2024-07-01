add_executable(fb_mono
	main.c
	fb_mono.c
)

target_link_libraries(fb_mono
	pico_stdlib
	libhyperram
)

pico_add_extra_outputs(fb_mono)

pico_generate_pio_header(fb_mono ${CMAKE_CURRENT_LIST_DIR}/fb_mono.pio)


# usb output, uart output enables (1 = enable)
pico_enable_stdio_usb(fb_mono 1)
pico_enable_stdio_uart(fb_mono 0)

# Enable SRAM only executable
pico_set_binary_type(fb_mono no_flash)
