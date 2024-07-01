#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/command_line/command_line.elf


