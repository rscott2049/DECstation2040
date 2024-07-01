/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _MEM_H_
#define _MEM_H_

#include <stdbool.h>
#include <stdint.h>

#define MAX_MEM_REGIONS		9


typedef bool (*MemAccessF)(uint32_t pa, uint_fast8_t size, bool write, void* buf);



bool memRegionAdd(uint32_t pa, uint32_t sz, MemAccessF af);
bool memRegionDel(uint32_t pa, uint32_t sz);

bool memAccess(uint32_t addr, uint_fast8_t size, bool write, void* buf);

void memReport(uint32_t addr, uint32_t size, bool write, uint32_t extra);

void printRegions(void);

void prMemAccess(void);

#endif
