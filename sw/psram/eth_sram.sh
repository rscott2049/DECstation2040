#!/bin/bash
source ../source_this
make -C build
../sram.sh $PWD/build/pico-rmii-ethernet/examples/httpd/pico_rmii_ethernet_httpd.elf

