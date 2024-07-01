#!/bin/bash
source ../source_this
make -C build
../program.sh $PWD/build/apps/mem_test/mem_test.elf
