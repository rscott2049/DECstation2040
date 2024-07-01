#!/bin/bash
source $PWD/source_this
export OPENOCD_PATH=$PICO_SDK_PATH/../../openocd
cd $OPENOCD_PATH; sudo src/openocd -f /home/rscott/bindto.cfg -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -s tcl

