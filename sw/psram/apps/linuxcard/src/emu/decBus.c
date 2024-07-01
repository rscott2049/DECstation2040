/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "printf.h"
#include "decBus.h"
#include "mem.h"


#pragma GCC optimize ("Os")

//#if defined(COLOR_FRAMEBUFFER) || defined(NO_FRAMEBUFFER)
#if 0
	#define CSR_INITIAL_VAL		0x0000
	//ultrix will not touch color framebuffer if we ask for console boot
#else
	#define CSR_INITIAL_VAL		0x0800
	//for black and white framebuffer it will touch so if we have none, say we have color
#endif

static uint32_t mBusErrorAddr;
	
static uint16_t mBusCsr = CSR_INITIAL_VAL;


void decReportBusErrorAddr(uint32_t pa)
{
	mBusErrorAddr = pa;
}


static bool accessDecBusErrorReporter(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	pa &= 0x00ffffff;
	
	if (size == 4 && !pa && !write) {
		*(uint32_t*)buf = mBusErrorAddr;
		return true;
	}
	return false;
}

static void decBusPrvLeds(uint_fast8_t val)
{
	err_str("LEDS set to 0x%02x\r\n", (uint8_t)~val);
}

static bool accessDecCSR(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{

	pa &= 0x00ffffff;
	
	if (pa)
		return false;
		
	if (write) {
	  pr("raw write: %08x\n", *(uint32_t *)buf);
		//we assume host is LE so this is easy
		decBusPrvLeds(*(uint8_t*)buf);
		if (size > 1) {
			
			uint_fast16_t writtenVal = *(uint16_t*)buf;
			//see DS3100 spec page 75-ish
			mBusCsr &=~ 0x0600 & writtenVal;
			mBusCsr &=~ 0x6000;
			mBusCsr |= 0x6000 & writtenVal;
		}
		pr("final: %08x\n", mBusCsr);
		return true;
	}
	//read
	//we do not care about vsync but VINT must change - change it every read
	//pr("read: %08x\n", mBusCsr);
	switch (size) {
		case 1:
			*(uint8_t*)buf = mBusCsr;
			mBusCsr ^= 0x200;
			return true;
		case 2:
			*(uint16_t*)buf = mBusCsr;
			mBusCsr ^= 0x200;
			return true;
		case 4:
			*(uint32_t*)buf = mBusCsr;
			mBusCsr ^= 0x200;
			return true;
		default:
			return false;
	}

}

bool decBusInit(void)
{
	return memRegionAdd(0x17000000, 0x01000000, accessDecBusErrorReporter) && 
			memRegionAdd(0x1e000000, 0x01000000, accessDecCSR);
}
