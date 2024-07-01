#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/picoprobe/picoprobe.elf
