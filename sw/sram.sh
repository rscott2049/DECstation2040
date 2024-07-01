#!/bin/bash
#echo $PWD
#source $PWD/source_this
prog=$1
export OPENOCD_PATH=$PICO_SDK_PATH/../../openocd
echo -e "reset\nexit\n" | ncat localhost 4444
#sleep 1
echo -e "reset halt\nload_image $prog\nresume 0x20000000\nexit\n" | ncat localhost 4444




