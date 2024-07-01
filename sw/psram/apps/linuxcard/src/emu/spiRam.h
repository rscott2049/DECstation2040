/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SPI_RAM_H_
#define _SPI_RAM_H_


#include <stdbool.h>
#include <stdint.h>


bool spiRamInit(uint8_t *eachChipSzP, uint8_t *numChipsP, uint8_t *chipWidthP);
uint32_t spiRamGetAmt(void);

//crossing chip boundary is not permitted AND not checked for. Crossing 1K coundary is not permitted and not checked for. Enjoy...
void spiRamRead(uint32_t addr, void *data, uint_fast16_t sz);
void spiRamWrite(uint32_t addr, const void *data, uint_fast16_t sz);



#endif
