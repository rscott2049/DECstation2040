/*
        Original:
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr

	Modifications:
	(c) 2023 Rob Scott
*/

#include <time.h>
#include <stdio.h>
#include <pico/stdlib.h>
#include "printf.h"

void timebaseInit(void) {
  pr("init getTime: %lld\n", time_us_64());

}

uint64_t getTime(void) {
  return time_us_64();
}
