add_executable(fb_mem_test
	main.c
)

target_link_libraries(fb_mem_test
	pico_stdlib
	libhyperram
	libfbh
)

pico_add_extra_outputs(fb_mem_test)

# usb output, uart output enables (1 = enable)
pico_enable_stdio_usb(fb_mem_test 0)
pico_enable_stdio_uart(fb_mem_test 1)

# Use J6 as serial port
#target_compile_definitions(fb_mem_test PRIVATE
#  PICO_DEFAULT_UART=1
#  PICO_DEFAULT_UART_TX_PIN=8
#  PICO_DEFAULT_UART_RX_PIN=9
#)

# Use J7 as serial port 
target_compile_definitions(fb_mem_test PRIVATE
  PICO_DEFAULT_UART=0
  PICO_DEFAULT_UART_TX_PIN=16
  PICO_DEFAULT_UART_RX_PIN=17
)

# Enable SRAM only executable
pico_set_binary_type(fb_mem_test no_flash)
