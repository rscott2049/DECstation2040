//#include <stdio.h>
#include "r3k_config.h"
#include "hyperram.h"
#include "printf.h"

bool spiRamInit(uint8_t *eachChipSzP, uint8_t *numChipsP, uint8_t *chipWidthP) {

	// -1 is failure
	if (hyperram_ram_init() < 0) {
	  pr("hyperrram init failure\n");
	  return false;
	}

	return true;
}

uint32_t spiRamGetAmt(void) {
        return (EMULATOR_RAM_MB << 20);
}

//crossing chip boundary is not permitted AND not checked for. Crossing 1K coundary is not permitted and not checked for. Enjoy...
void spiRamRead(uint32_t addr, void *data, uint_fast16_t sz) {

  if (sz < 4) {
    uint8_t localdata[4];
    uint8_t* dataptr = (uint8_t *)data;
    uint32_t align;

    //pr("Read addr/size: %08x %d\n", addr, sz);
    hyperram_read(addr, localdata, 4);
    align = addr & 0x1;

    for(int i = 0; i < sz; i++) {
      *dataptr++ = localdata[align + i];
    }
  } else {
    hyperram_read(addr, data, sz);
  }
  
}

void spiRamWrite(uint32_t addr, const void *data, uint_fast16_t sz) {

  if (sz < 4) {
    // Do RMW
    uint8_t rmwdata[4];
    uint8_t *dptr = (uint8_t *)data;
    uint32_t start_wr = addr & 0x1;

    //if (sz > 1) pr("Write addr/size: %08x %d\n", addr, sz);

    hyperram_read(addr, rmwdata, 4);

    for (int i = 0; i < sz; i++) {
      rmwdata[i + start_wr] = *dptr++;
    } 

    hyperram_write(addr, rmwdata, 4);
  } else {
    hyperram_write(addr, data, sz);
  }
}

