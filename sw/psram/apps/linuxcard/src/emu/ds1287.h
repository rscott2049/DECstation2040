/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _DS1287_H_
#define _DS1287_H_

#include <stdbool.h>


bool ds1287init(void);

void ds1287step(uint_fast16_t nTicks);	//check for RTC irqs... on pc also tick 1/8192th of a sec


#endif
