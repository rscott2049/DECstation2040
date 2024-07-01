#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/pico-rv32ima/pico-rv32ima/pico-rv32ima.elf
