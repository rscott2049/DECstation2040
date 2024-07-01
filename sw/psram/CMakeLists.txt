cmake_minimum_required(VERSION 3.12)
#include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
include(pico_sdk_import.cmake)

project(psram_wren C CXX ASM)
#set(CMAKE_C_STANDARD 11)
#set(CMAKE_CXX_STANDARD 17)
pico_sdk_init(C CXX ASM)

#add_compile_options(-Wall)
# Need to enable this for asm version of cpu.c (see linuxcard/src/emu/cpuAsm.S)
add_compile_options(-ffixed-r11)

#add_definitions(-DDEBUG_PIN0=22 -DDEBUG_N_PINS=4)
#add_definitions(-DPICO_DEFAULT_UART_TX_PIN=16 -DPICO_DEFAULT_UART_RX_PIN=17)

# add_definitions(-DDEFAULT_DVI_SERIAL_CONFIG=picodvi_pmod0_cfg)
# add_definitions(-DRUN_FROM_CRYSTAL=1)
# add_definitions(-DDVI_SERIAL_DEBUG=1)

add_subdirectory(boot_stage2)
add_subdirectory(libhyperram)
add_subdirectory(libfbh)
add_subdirectory(apps)
add_subdirectory(no-OS-FatFs/src)
add_subdirectory(pico-rmii-ethernet)

# Select different boot_stage2 file
set(PICO_DEFAULT_BOOT_STAGE2_FILE ${CMAKE_CURRENT_LIST_DIR}/boot_stage2/boot2_w25q080_mod.S)
pico_define_boot_stage2(boot2_mod ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(boot2_mod PRIVATE PICO_FLASH_SPI_CLKDIV=8)
# Apply to all apps
pico_set_boot_stage2(mem_test boot2_mod)
pico_set_boot_stage2(picoprobe boot2_mod)
pico_set_boot_stage2(fb_test boot2_mod)
pico_set_boot_stage2(fb_mem_test boot2_mod)
pico_set_boot_stage2(pico-rv32ima boot2_mod)
pico_set_boot_stage2(uMIPS boot2_mod)
pico_set_boot_stage2(pico_rmii_ethernet_httpd boot2_mod)
