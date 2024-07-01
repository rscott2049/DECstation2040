add_executable(mem_test
	main.c
)

target_link_libraries(mem_test
	pico_stdlib
	libhyperram
)

pico_add_extra_outputs(mem_test)

# usb output, uart output enables (1 = enable)
pico_enable_stdio_usb(mem_test 1)
pico_enable_stdio_uart(mem_test 1)

# Use J6 as serial port
#target_compile_definitions(mem_test PRIVATE
#  PICO_DEFAULT_UART=1
#  PICO_DEFAULT_UART_TX_PIN=8
#  PICO_DEFAULT_UART_RX_PIN=9
#)

# Use J7 as serial port 
target_compile_definitions(mem_test PRIVATE
  PICO_DEFAULT_UART=0
  PICO_DEFAULT_UART_TX_PIN=16
  PICO_DEFAULT_UART_RX_PIN=17
)

# Enable SRAM only executable
pico_set_binary_type(mem_test no_flash)
