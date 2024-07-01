# Leaf CMakeLists.txt
add_executable(blink blink.c)
target_link_libraries(blink pico_stdlib hardware_interp)
pico_add_extra_outputs(blink)

# Configure outputs
pico_enable_stdio_usb(blink 1)
pico_enable_stdio_uart(blink 1)

# Use J6 as serial port
#target_compile_definitions(blink PRIVATE
#  PICO_DEFAULT_UART=1
#  PICO_DEFAULT_UART_TX_PIN=8
#  PICO_DEFAULT_UART_RX_PIN=9
#)

# Use J7 as serial port 
target_compile_definitions(blink PRIVATE
  PICO_DEFAULT_UART=0
  PICO_DEFAULT_UART_TX_PIN=16
  PICO_DEFAULT_UART_RX_PIN=17
)


# run from RAM
#pico_set_binary_type(blink no_flash)

