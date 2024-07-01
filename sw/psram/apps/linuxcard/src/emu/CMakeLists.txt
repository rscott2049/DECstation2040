
add_executable(uMIPS
  mem.c
  decBus.c
  dz11.c
  lance.c
  esar.c
  sii.c
  scsiDevice.c
  scsiDisk.c
  scsiNothing.c
  printf.c
  main_uc.c
  spiRamRP2040.c
  timebase_rp2040.c
  ucHwRP2040.c
  usartRP2040.c
  ds1287RP2040.c
  usbDevRP2040.c  
  cpuAsm.S
#  cpu.c
  fpu.c
  sd_hw_config.c
  graphicsRP2040.c
  usbHID.c
)

set_property(TARGET uMIPS APPEND_STRING PROPERTY LINK_FLAGS 
  "-Wl,--print-memory-usage"
)

target_link_libraries(uMIPS
	pico_stdlib 
	pico_time
	pico_multicore
	hardware_spi
	hardware_vreg
	libhyperram
	#FatFs_SPI 
	no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
	libfbh
	tinyusb_host
	tinyusb_board
)


target_include_directories(uMIPS PUBLIC ${CMAKE_CURRENT_LIST_DIR} )

target_compile_definitions(uMIPS PUBLIC
#  DISABLE_ICACHE=1
  ICACHE_NUM_SETS_ORDER=6
  L2CACHE_NUM_SETS=32
  OPTIMAL_RAM_WR_SZ=32
  OPTIMAL_RAM_RD_SZ=32
  err_str=pr
  # Use the RP sdk function: time_us_64, which counts once per usec
  TICKS_PER_SECOND=1000000U
  CPU_TYPE_CM0
#  FPU_SUPPORT_NONE
  FPU_SUPPORT_MINIMAL
  SUPPORT_DEBUG_PRINTF
  MONO_FRAMEBUFFER
  # For SD card library
  #PICO_STACK_SIZE=0x8000
  #PICO_CORE1_STACK_SIZE=0x1000
  #PICO_HEAP_SIZE=0x80000
  #PICO_HEAP_SIZE=0x8000
  )

# usb output, uart output enables (1 = enable)
pico_enable_stdio_usb(uMIPS 0)
pico_enable_stdio_uart(uMIPS 1)

# Use J6 as serial port
#target_compile_definitions(uMIPS PRIVATE
#  PICO_DEFAULT_UART=1
#  PICO_DEFAULT_UART_TX_PIN=8
#  PICO_DEFAULT_UART_RX_PIN=9
#)

# Use J7 as serial port 
target_compile_definitions(uMIPS PRIVATE
  PICO_DEFAULT_UART=0
  PICO_DEFAULT_UART_TX_PIN=16
  PICO_DEFAULT_UART_RX_PIN=17
)

# Enable SRAM only executable
pico_set_binary_type(uMIPS no_flash)
#pico_set_binary_type(uMIPS copy_to_ram)

pico_add_extra_outputs(uMIPS)
