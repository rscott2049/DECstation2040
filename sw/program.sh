#!/bin/bash
#source $PWD/source_this
#prog=$PWD/Pico_1140/Pico_1140_DC/build/Pico_1140.elf
prog=$1
export OPENOCD_PATH=$PICO_SDK_PATH/../../openocd
echo -e "reset\nexit\n" | ncat localhost 4444
#sleep 1
echo -e "reset halt\nprogram $prog verify\n" | ncat localhost 4444
echo -e "reset\nexit\n" | ncat localhost 4444

