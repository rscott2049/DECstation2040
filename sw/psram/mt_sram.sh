#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/apps/mem_test/mem_test.elf


