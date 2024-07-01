#!/bin/bash
source ../source_this
make -C build/src
../program.sh $PWD/build/src/blink.elf


