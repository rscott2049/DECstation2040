#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/fb_test/fb_test.elf
#../sram.sh $PWD/build/apps/fb_mono/fb_mono.elf
