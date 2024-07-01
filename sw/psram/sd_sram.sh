#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/no-OS-FatFs/examples/simple_sdio/simple_sdio.elf


