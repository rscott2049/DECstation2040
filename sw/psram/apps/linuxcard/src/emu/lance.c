/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "printf.h"
#include "lance.h"
#include "mem.h"
#include "soc.h"


#pragma GCC optimize ("Os")


#define VERBOSE				0
#define VERY_VERBOSE		0

struct Lance {
	uint32_t iadr	:24;
	uint32_t csrNo	: 2;
	uint32_t bswp	: 1;
	uint32_t acon	: 1;
	uint32_t bcon	: 1;
	uint16_t csr0;
};

#define LANCE_BUFFER_SIZE		(65536)

static struct Lance mLance;

#ifndef MICRO_LANCE
	static uint8_t mLanceBuffer[LANCE_BUFFER_SIZE];
#endif

#define LANCE_CSR0_ERR			0x8000
#define LANCE_CSR0_BABL			0x4000
#define LANCE_CSR0_CERR			0x2000
#define LANCE_CSR0_MISS			0x1000
#define LANCE_CSR0_MERR			0x0800
#define LANCE_CSR0_RINT			0x0400
#define LANCE_CSR0_TINT			0x0200
#define LANCE_CSR0_IDON			0x0100
#define LANCE_CSR0_INTR			0x0080
#define LANCE_CSR0_INEA			0x0040
#define LANCE_CSR0_RXON			0x0020
#define LANCE_CSR0_TXON			0x0010
#define LANCE_CSR0_TDMD			0x0008
#define LANCE_CSR0_STOP			0x0004
#define LANCE_CSR0_STRT			0x0002
#define LANCE_CSR0_INIT			0x0001

#define LANCE_CSR3_BSWP			0x0008
#define LANCE_CSR3_ACON			0x0002
#define LANCE_CSR3_BCON			0x0001

static void lancePrvIrqRecalc(void)
{
	cpuIrq(SOC_IRQNO_ETHERNET, (mLance.csr0 & (LANCE_CSR0_INTR | LANCE_CSR0_INEA)) == (LANCE_CSR0_INTR | LANCE_CSR0_INEA));
}

static void lancePrvCsr0recalc(void)
{
	if (mLance.csr0 & (LANCE_CSR0_BABL | LANCE_CSR0_CERR | LANCE_CSR0_MISS | LANCE_CSR0_MERR))
		mLance.csr0 |= LANCE_CSR0_ERR;
	else
		mLance.csr0 &=~ LANCE_CSR0_ERR;
	
	if (mLance.csr0 & (LANCE_CSR0_BABL | LANCE_CSR0_MISS | LANCE_CSR0_MERR | LANCE_CSR0_RINT | LANCE_CSR0_TINT | LANCE_CSR0_IDON))
		mLance.csr0 |= LANCE_CSR0_INTR;
	else
		mLance.csr0 &=~ LANCE_CSR0_INTR;
	
	lancePrvIrqRecalc();
}

static void lancePrvDoInit(void)
{
	///todo
}

static bool lancePrvCsrWrite(uint_fast16_t v)
{
	uint_fast16_t w1c = LANCE_CSR0_BABL | LANCE_CSR0_CERR | LANCE_CSR0_MISS | LANCE_CSR0_MERR | LANCE_CSR0_RINT | LANCE_CSR0_TINT | LANCE_CSR0_IDON;
	
	if (!mLance.csrNo) {
		
		mLance.csr0 &=~ (v & w1c);
		mLance.csr0 &=~ LANCE_CSR0_INEA;
		mLance.csr0 |= v & (LANCE_CSR0_INEA | LANCE_CSR0_TDMD);
		if (v & LANCE_CSR0_STOP)
			mLance.csr0 = LANCE_CSR0_STOP;
		else if (v & LANCE_CSR0_STRT) {
	//		if (!(mLance.csr0 & LANCE_CSR0_STOP))
	//			return false;
			mLance.csr0 &=~ LANCE_CSR0_STOP;
			mLance.csr0 |= LANCE_CSR0_STRT;
		}
		else if (v & LANCE_CSR0_INIT) {
	//		if (!(mLance.csr0 & LANCE_CSR0_STOP))
	//			return false;
			mLance.csr0 &=~ LANCE_CSR0_STOP;
			mLance.csr0 |= LANCE_CSR0_INIT;
			
			lancePrvDoInit();
			
			mLance.csr0 |= LANCE_CSR0_IDON;
		}
		lancePrvCsr0recalc();
		
		return true;
	}
	else if (!(mLance.csr0 & LANCE_CSR0_STOP))
		return false;
	else switch (mLance.csrNo) {
		
		case 1:
			mLance.iadr = (mLance.iadr &~ 0xffff) + (v & 0xfffe);
			return true;
		
		case 2:
			mLance.iadr = (mLance.iadr & 0xffff) + (((uint32_t)(v & 0xff)) << 16);
			return true;
		
		case 3:
			mLance.bswp = !!(v & LANCE_CSR3_BSWP);
			mLance.acon = !!(v & LANCE_CSR3_ACON);
			mLance.bcon = !!(v & LANCE_CSR3_BCON);
			return true;
		
		default:
			return false;
	}
}

static bool lancePrvCsrRead(uint16_t *buf)
{
	switch (mLance.csrNo) {
		case 0:
			*buf = mLance.csr0;
			return true;
		
		case 1:
			*buf = (mLance.csr0 & LANCE_CSR0_STOP) ? (mLance.iadr & 0xffff) : 0;
			return true;
		
		case 2:
			*buf = (mLance.csr0 & LANCE_CSR0_STOP) ? (mLance.iadr >> 16) : 0;
			return true;
		
		case 3:
			*buf = (mLance.csr0 & LANCE_CSR0_STOP) ? ((mLance.bswp ? LANCE_CSR3_BSWP : 0) + (mLance.acon ? LANCE_CSR3_ACON : 0) + (mLance.bcon ? LANCE_CSR3_BCON : 0)) : 0;
			return true;
		
		default :
			return false;
	}
}

static bool lancePrvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint16_t *vP = (uint16_t*)buf;
	uint_fast16_t v;
	bool ret = false;
		
	//buffer ram starts at offset 0x01000000
	
	pa &= 0x01ffffff;
	if (pa & 3) {
		
		if (VERBOSE)
			err_str("invalid LANCE access mode: %u @ 0x%04x\n", size, pa);
		return false;
	}
	v = *vP;	//works is we're LE
	
	if (pa & 0x01000000) {
		
		pa &= 0x00ffffff;
		pa /= 2;
		
		if (pa >= LANCE_BUFFER_SIZE)
			return false;
		
		#ifdef MICRO_LANCE
		
			if (!write) {
				
				if (size == 4)		//works if we're LE
					vP[1] = 0;
					
				*vP = 0;
			}
		
		#else	//real
			
			if (write) {
				
				//assume host is LE - i am lazy, so sue me
				*(uint16_t*)(mLanceBuffer + pa) = v;
				if (VERY_VERBOSE)
					err_str("LANCERAM[0x%04x] <- 0x%04x\r\n", pa / 2, *vP);
			}
			else {
				
				if (size == 4)		//works if we're LE
					vP[1] = 0;
				
				*vP = *(uint16_t*)(mLanceBuffer + pa);
				if (VERY_VERBOSE)
					err_str("LANCERAM[0x%04x] -> 0x%04x\r\n", pa / 2, *vP);
			}
		#endif
		
		return true;
	}
	else if (size != 2) {
		
		if (VERBOSE)
			err_str("invalid LANCE access mode: %u @ 0x%04x\n", size, pa);
		return false;
	}
	else {
		
		pa /= 4;
		
		switch (pa) {
			case 0:
				if (write)
					ret = lancePrvCsrWrite(v);
				else
					ret = lancePrvCsrRead(vP);
				break;
				
			case 1:
				if (write)
					mLance.csrNo = v & 3;
				else
					*vP = mLance.csrNo;
				ret = true;
				break;
			
			default:
				break;
		}
	}
	
	if (VERBOSE) {
				
		if (write)
			err_str("%s LANCE reg W: [0x%04x] <- 0x%04x\r\n", ret ? "GOOD" : "BAD ", pa * 4, (unsigned)v);
		else if (!ret)
			err_str("%s LANCE reg R: [0x%04x] -> ??????\r\n", "BAD ", pa * 4);
		else
			err_str("%s LANCE reg R: [0x%04x] -> 0x%04x\r\n", "GOOD", pa * 4, *(uint16_t*)vP);
	}
	
	return ret;
}

bool lanceInit(void)
{
	mLance.csr0 = LANCE_CSR0_STOP;
	
	return memRegionAdd(0x18000000, 0x02000000, lancePrvMemAccess);
}

