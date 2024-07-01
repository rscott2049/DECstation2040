#!/bin/bash
source ../source_this
make -C build
../program.sh $PWD/build/apps/linuxcard/src/emu/uMIPS.elf

