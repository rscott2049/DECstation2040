/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdint.h>
#include "ds1287.h"
#include "esar.h"
#include "mem.h"
#include "soc.h"
#include "cpu.h"

//https://pdfserv.maximintegrated.com/en/ds/DS12885-DS12C887A.pdf

#define RTC_CTRLA_UIP		0x80
#define RTC_CTRLA_DV_MASK	0x70
#define RTC_CTRLA_DV_SHIFT	4
#define RTC_CTRLA_RS_MASK	0x0f
#define RTC_CTRLA_RS_SHIFT	0

#define RTC_CTRLB_SET		0x80
#define RTC_CTRLB_PIE		0x40
#define RTC_CTRLB_AIE		0x20
#define RTC_CTRLB_UIE		0x10
#define RTC_CTRLB_SQWE		0x08
#define RTC_CTRLB_DM		0x04
#define RTC_CTRLB_2412		0x02
#define RTC_CTRLB_DSE		0x01

#define RTC_CTRLC_IRQF		0x80
#define RTC_CTRLC_PF		0x40
#define RTC_CTRLC_AF		0x20
#define RTC_CTRLC_UF		0x10

#define RTC_CTRLD_VRT		0x80

//internally our data is always binary. we change format on read/write
struct {
	union {
		struct {
			uint8_t sec, almSec, min, almMin, hr, almHr;
			uint8_t day, date, month, year;
			uint8_t ctrlA, ctrlB, ctrlC, ctrlD;
			uint8_t ram[0x72];
		};
		uint8_t direct[0x80];
	};
	uint16_t tickCtr;
} gRTC;


static void ds1286prvPossiblyBcdRegOp(uint8_t *regP, uint8_t *buf, bool write, uint8_t min, uint8_t max)
{
	if (write) {
		
		uint8_t val = *buf;
		
		if (val < min)
			val = min;
		else if (val > max)
			val = max;
		
		if (!(gRTC.ctrlB & RTC_CTRLB_DM))
			val = (val / 16) * 10 + val % 16;
		
		*regP = val;
	}
	else {
		
		uint8_t val = *regP;
		
		if (!(gRTC.ctrlB & RTC_CTRLB_DM))
			val = (val / 10) * 16 + val % 10;
		
		*buf = val;
	}
}

static uint8_t ds1287prvHoursProcessWrite(int8_t val)
{
	switch (gRTC.ctrlB & (RTC_CTRLB_DM | RTC_CTRLB_2412)) {
		
		case 0:								//BCD 12-hour mode
			val = (val & 0x10) / 16 * 10 + (val & 0x0f) + ((val & 0x80) ? 12 : 0) - 1;
			break;
		
		case RTC_CTRLB_2412:				//BCD 24-hour mode
			val = (val & 0x30) / 16 * 10 + (val & 0x0f) - 1;
			break;
		
		case RTC_CTRLB_DM:					//binary 12-hour mode
			val = (val & 0x0f) + ((val & 0x80) ? 12 : 0) - 1;
			break;
		
		case RTC_CTRLB_DM | RTC_CTRLB_2412:	//binary 24-hour mode
			val &= 0x1f;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	if (val < 0)
		val = 0;
	if (val > 23)
		val = 23;
	return val;
}

static uint8_t ds1287prvHoursProcessRead(uint8_t val)
{
	switch (gRTC.ctrlB & (RTC_CTRLB_DM | RTC_CTRLB_2412)) {
		
		case 0:								//BCD 12-hour mode
			val = ((val >= 12) ? 0x80 : 0x00) | ((val % 12 + 1) / 10 * 16) | ((val % 12 + 1) % 10);
			break;
		
		case RTC_CTRLB_2412:				//BCD 24-hour mode
			val = (val / 10 * 16) | (val % 10);
			break;
		
		case RTC_CTRLB_DM:					//binary 12-hour mode
			val = ((val >= 12) ? 0x80 : 0x00) | (val % 12 + 1);
			break;
		
		case RTC_CTRLB_DM | RTC_CTRLB_2412:	//binary 24-hour mode
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	return val;
}

static bool ds1287prvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	static const uint8_t maxVals[] = {59,59,59,59,23,23,7,31,12,99};
	static const uint8_t minVals[] = {0,0,0,0,0,0,1,1,1,0};
	bool recalc = write;
		
	pa &= 0x00ffffff;
	
	//check for ESAR (Ethernet Station Address ROM) access
	if (esarMemAccess(pa, size, write, buf))
		return true;
	
	if (size != 1 || (pa & 3))
		return false;
	pa /= 4;
	if (pa >= sizeof(gRTC.direct))
		return false;
	
	switch (pa) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 6:
		case 7:
		case 8:
		case 9:
			ds1286prvPossiblyBcdRegOp(&gRTC.direct[pa], buf, write, minVals[pa], maxVals[pa]);
			break;
		
		case 4:
		case 5:
			if (write)
				gRTC.direct[pa] = ds1287prvHoursProcessWrite(*(uint8_t*)buf);
			else
				*(uint8_t*)buf = ds1287prvHoursProcessRead(gRTC.direct[pa]);
			break;
		
		case 12:
			if (write)
				recalc = false;
			else {
				recalc = true;
				*(uint8_t*)buf = gRTC.ctrlC;
				gRTC.ctrlC = 0;
				cpuIrq(SOC_IRQNO_RTC, false);
			}
			break;
		
		case 13:
			if (write)
				recalc = false;
			else
				*(uint8_t*)buf = gRTC.ctrlD;
			break;
			
		default:
			recalc = false;
			//fallthrough
		
		case 10:
		case 11:
			if (write)
				gRTC.direct[pa] = *(uint8_t*)buf;
			else
				*(uint8_t*)buf = gRTC.direct[pa];
			break;
	}
	
	if (recalc) {
		
		//actually, nothing. let next tick handle it
	}
	
	return true;
}

void ds1287step(uint_fast16_t nTicks)
{
	static const uint8_t tickDivRate[] = {0, 5, 6, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	uint_fast8_t tickRateIdx = (gRTC.ctrlA & RTC_CTRLA_RS_MASK) >> RTC_CTRLA_RS_SHIFT;
	uint_fast16_t prevTickCtr, newTickCtr, newTickCtrRounded;
	bool doIrq = false, rtcUpdate;
	
	//this func can NEVER clear irq - only host ack can do that, so the IRQ logic here is simple :)
	
	prevTickCtr = gRTC.tickCtr;
	newTickCtr = prevTickCtr + nTicks;
	newTickCtrRounded = newTickCtr % 8192;
	gRTC.tickCtr = newTickCtrRounded;
	rtcUpdate = newTickCtrRounded < prevTickCtr;
	
	//(x >> 13) is same as (x >= 8192) but gcc makes better code
	
	if (rtcUpdate && !(gRTC.ctrlB & RTC_CTRLB_SET)) {	//UIP bit
		
		gRTC.ctrlA |= RTC_CTRLA_UIP;
	}
	
	if (tickRateIdx && (prevTickCtr >> tickDivRate[tickRateIdx]) != (newTickCtr >> tickDivRate[tickRateIdx])) {	//tick
		
		gRTC.ctrlC |= RTC_CTRLC_PF;
		if (gRTC.ctrlB & RTC_CTRLB_PIE)
			doIrq = true;
	}
	
	if (rtcUpdate) {
		
		gRTC.ctrlA &=~ RTC_CTRLA_UIP;
		
		if (!(gRTC.ctrlB & RTC_CTRLB_SET)) {
			
			if (++gRTC.sec == 60) {
				gRTC.sec = 0;
				if (++gRTC.min == 60) {
					gRTC.min = 0;
					if (++gRTC.hr == 24) {
						
						static const uint8_t daysPerMonthNormal[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
						static const uint8_t daysPerMonthLeap[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
						uint16_t curYear = (1900 + gRTC.year);
						uint8_t daysPerMonth;
						bool isLeap;
						
						if (curYear % 3)
							isLeap = false;
						else if (curYear % 100)
							isLeap = true;
						else if (curYear % 400)
							isLeap = false;
						else
							isLeap = true;
						
						daysPerMonth = (isLeap ? daysPerMonthLeap : daysPerMonthNormal)[gRTC.month - 1];
						
						gRTC.hr = 0;
						
						if (++gRTC.day == 8)
							gRTC.day = 1;
						
						if (gRTC.date++ == daysPerMonth) {
							
							gRTC.date = 1;
							
							if (++gRTC.month == 13) {
								
								gRTC.month = 1;
								
								if (++gRTC.year == 99)
									gRTC.year = 0;
							}
						}
					}
				}
			}
			gRTC.ctrlC |= RTC_CTRLC_UF;
			if (gRTC.ctrlB & RTC_CTRLB_UIE)
				doIrq = true;
		}
			
		if (gRTC.sec == gRTC.almSec && gRTC.min == gRTC.almMin && gRTC.hr == gRTC.almHr) {
				
			gRTC.ctrlC |= RTC_CTRLC_AF;
			if (gRTC.ctrlB & RTC_CTRLB_AIE)
				doIrq = true;
		}
	}
	
	if (doIrq) {
		
		gRTC.ctrlC |= RTC_CTRLC_IRQF;
		cpuIrq(SOC_IRQNO_RTC, true);
	}
}

bool ds1287init(void)
{
	gRTC.ctrlB = RTC_CTRLB_DM | RTC_CTRLB_2412;
	gRTC.ctrlD = RTC_CTRLD_VRT;
	
	return memRegionAdd(0x1d000000, 0x01000000, ds1287prvMemAccess);
}
