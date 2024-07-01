/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _DEC_H_
#define _DEC_H_


#include <stdbool.h>
#include <stdint.h>


bool decBusInit(void);

void decReportBusErrorAddr(uint32_t pa);


#endif
