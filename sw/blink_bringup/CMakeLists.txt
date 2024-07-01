# Usage:
# 1) Configure
#    cmake -B build -S .
# Build .elf, .uf2 etc.
#    make -C build/src
# Results wiil be in build/src

# Top level cmakefile
cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(blink C CXX ASM)

pico_sdk_init(C CXX ASM)

# source goes here
add_subdirectory(./boot_stage2)
add_subdirectory(./src)

# Select different boot_stage2 file
set(PICO_DEFAULT_BOOT_STAGE2_FILE ${CMAKE_CURRENT_LIST_DIR}/boot_stage2/boot2_w25q080_mod.S)
pico_define_boot_stage2(boot2_mod ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(boot2_mod PRIVATE PICO_FLASH_SPI_CLKDIV=8)
# Apply to all apps
pico_set_boot_stage2(blink boot2_mod)
