/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

//#include "CortexEmuCpu.h"

#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "timebase.h"
#include "printf.h"
#include "ds1287.h"
#include "esar.h"
#include "mem.h"
#include "soc.h"
#include "cpu.h"


//https://pdfserv.maximintegrated.com/en/ds/DS12885-DS12C887A.pdf

//XXX: dec PROM fucks with year, so linux works around it by storing year offset in 0x3F (ake ram[0x31]) and keeping "year" offset from 72 - 2000
#define DEC_REAL_YEAR_LOC		0x31


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

// We use two timers: one for per Hz timekeeping,
// and one for periodic interrupts (0 to 256 Hz)
struct repeating_timer timerHz;
struct repeating_timer timerRTC;

int32_t reportCount = 0;

bool rtc_callback(struct repeating_timer *t);

struct Config {
	
	//irq flags (bytes to avoid RMW issues)
	volatile uint8_t pf;
	volatile uint8_t af;
	volatile uint8_t uf;
	volatile uint8_t irqf;
	
	//irq enables
	uint8_t pie			: 1;
	uint8_t aie			: 1;
	uint8_t uie			: 1;
	
	//misc & config
	uint8_t set			: 1;
	uint8_t m2412		: 1;	//1 for 24h mode, 0 for 12h
	uint8_t dse			: 1;
	uint8_t rs			: 4;
	uint8_t dv			: 3;
	uint8_t sqwe		: 1;
	uint8_t dm			: 1;	//1 for binary, 0 for BCD
	
	uint8_t ram[0x72];
	
} mDS1287;

static const uint8_t mMonthDaysNormal[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const uint8_t mMonthDaysLeap[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct Time {
	uint8_t h, m, s;
};

struct Date {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t dow;
};

union DateTime {
	struct {
		struct Date date;
		struct Time time;
		uint8_t sbz;
	};
	uint64_t raw64;
};

static struct Time mAlarmTime;
static union DateTime mCurTime = {
	.date = {
		.year = 72,		//see note above
		.month = 6,		//june
		.day = 17,		//17th	
		.dow = 6,		//friday
	},
	.time = {
		.h = 1,
		.m = 0,
		.s = 0,
	},
};


static uint_fast8_t ds1287prvToBcdIfNeeded(uint_fast8_t val, bool allowSpecialIgnoreVals)
{
	if (val >= 0xc0 && allowSpecialIgnoreVals)
		return val;
	
	if (!mDS1287.dm)
		val = (val / 10) * 16 + val % 10;
	
	return val;
}

static uint_fast8_t ds1287prvFromBcdIfNeeded(uint_fast8_t val, bool allowSpecialIgnoreVals)
{
	if (val >= 0xc0 && allowSpecialIgnoreVals)
		return val;
	
	if (!mDS1287.dm)
		val = (val / 16) * 10 + val % 16;
	
	return val;
}

static void ds1287prvSimpleAccess8(volatile uint8_t *reg, uint8_t *buf, bool write, bool allowSpecialIgnoreVals)
{
	if (write) {
		*reg = ds1287prvFromBcdIfNeeded(*buf, allowSpecialIgnoreVals);
	}
	else
		*buf = ds1287prvToBcdIfNeeded(*reg, allowSpecialIgnoreVals);
}

static void ds1287prvHoursAccess(volatile uint8_t *reg, uint8_t *buf, bool write, bool allowSpecialIgnoreVals)
{
	uint_fast8_t rv, t;
	
	if (write) {
		
		rv = *buf;
		
		if (allowSpecialIgnoreVals && rv >= 0xc0)
			t = rv;
		else {
		
			t = ds1287prvFromBcdIfNeeded(rv & 0x7f, false /* already handled */);

			if (!mDS1287.m2412) {
				
				if (t == 12)
					t = 0;
				if (rv & 0x80)
					t += 12;
			}
		}
		
		*reg = t;
	}
	else {
		
		rv = *reg;
		
		if (allowSpecialIgnoreVals && rv >= 0xc0)
			t = rv;
		else {
			
			t = rv;
			
			if (!mDS1287.m2412) {
				
				if (t == 0)
					t = 12;
				else if (t == 12)
					t = 0x8c;
				else if (t > 12)
					t = 0x80 + t - 12;
			}
			
			t = (t & 0x80) | ds1287prvToBcdIfNeeded(t & 0x7f, false /* already handled */);
		}
		
		*buf = t;
	}
}

static bool ds1287prvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint_fast8_t val;
		
	pa &= 0x00ffffff;
	
	//check for ESAR (Ethernet Station Address ROM) access
	if (esarMemAccess(pa, size, write, buf))
		return true;
	
	
	if (size != 1 || (pa & 3))
		return false;
	pa /= 4;
	if (pa >= 0x80)
		return false;
	
	switch (pa) {
		case 0x00:		//seconds
			ds1287prvSimpleAccess8(&mCurTime.time.s, buf, write, false);
			break;
			
		case 0x01:		//alarm seconds
			ds1287prvSimpleAccess8(&mAlarmTime.s, buf, write, true);
			break;
		
		case 0x02:		//minutes
			ds1287prvSimpleAccess8(&mCurTime.time.m, buf, write, false);
			break;
			
		case 0x03:		//alarm minutes
			ds1287prvSimpleAccess8(&mAlarmTime.m, buf, write, true);
			break;
	
		case 0x04:		//hours
			ds1287prvHoursAccess(&mCurTime.time.h, buf, write, false);
			break;
		
		case 0x05:		//alarm hours
			ds1287prvHoursAccess(&mAlarmTime.h, buf, write, true);
			break;
	
		case 0x06:		//DoW
			ds1287prvSimpleAccess8(&mCurTime.date.dow, buf, write, false);
			break;
		
		case 0x07:		//date
			ds1287prvSimpleAccess8(&mCurTime.date.day, buf, write, false);
			break;
	
		case 0x08:		//month
			ds1287prvSimpleAccess8(&mCurTime.date.month, buf, write, false);
			break;
		
		case 0x09:		//year
			ds1287prvSimpleAccess8(&mCurTime.date.year, buf, write, false);
			break;
	
		case 0x0a:		//CTRLA
			if (write) {
				
				static const uint16_t hzVals[] = {0, 256, 128, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2};
				val = *(const uint8_t*)buf;
				
				mDS1287.dv = (val & 0x70) >> 4;
				mDS1287.rs = val = val & 0x0f;
				
				//osTimerSetForHz(hzVals[val]);
			}
			else
				*(uint8_t*)buf = (mDS1287.dv << 4) | mDS1287.rs;	//uip is clear always for us
			break;
	
		case 0x0b:		//CTRLB
			if (write) {
				
				val = *(const uint8_t*)buf;
				
				if ((val & RTC_CTRLB_SET) && !mDS1287.set) {		//go to set mode
					
					mDS1287.set = 1;
				}
				else if (!(val & RTC_CTRLB_SET) && mDS1287.set) {	//leave set mode
					
					mDS1287.set = 0;
				}
				
				mDS1287.pie = !!(val & RTC_CTRLB_PIE);
				mDS1287.aie = !!(val & RTC_CTRLB_AIE);
				mDS1287.uie = !!(val & RTC_CTRLB_UIE);
				mDS1287.sqwe = !!(val & RTC_CTRLB_SQWE);
				mDS1287.sqwe = !!(val & RTC_CTRLB_SQWE);
				mDS1287.dm = !!(val & RTC_CTRLB_DM);
				mDS1287.m2412 = !!(val & RTC_CTRLB_2412);
				mDS1287.dse = !!(val & RTC_CTRLB_DSE);
				
#if 0
				if (mDS1287.pie)
					NVIC_EnableIRQ(OsTimer_IRQn);
				else
					NVIC_DisableIRQ(OsTimer_IRQn);
#endif
			}
			else {
				
				val = 0;
				
				if (mDS1287.set)
					val |= RTC_CTRLB_SET;
				if (mDS1287.pie)
					val |= RTC_CTRLB_PIE;
				if (mDS1287.aie)
					val |= RTC_CTRLB_AIE;
				if (mDS1287.uie)
					val |= RTC_CTRLB_UIE;
				if (mDS1287.sqwe)
					val |= RTC_CTRLB_SQWE;
				if (mDS1287.dm)
					val |= RTC_CTRLB_DM;
				if (mDS1287.m2412)
					val |= RTC_CTRLB_2412;
				if (mDS1287.dse)
					val |= RTC_CTRLB_DSE;
				
				*(uint8_t*)buf = val;
			}
			break;

		case 0x0c:		//CTRLC
			if (!write) {
				
				val = 0;
				
				asm volatile("cpsid i");
				if (mDS1287.irqf)
					val |= RTC_CTRLC_IRQF;
				if (mDS1287.uf)
					val |= RTC_CTRLC_UF;
				if (mDS1287.af)
					val |= RTC_CTRLC_AF;
				if (mDS1287.pf)
					val |= RTC_CTRLC_PF;
				
				mDS1287.irqf = 0;
				mDS1287.uf = 0;
				mDS1287.af = 0;
				mDS1287.pf = 0;
				
				cpuIrq(SOC_IRQNO_RTC, false);
				
				asm volatile("cpsie i");
				
				*(uint8_t*)buf = val;
			}
			break;

		case 0x0d:		//CTRLD
			if (!write)
				*(uint8_t*)buf = RTC_CTRLD_VRT;
			break;
		
		default:
			if (write)
				mDS1287.ram[pa - 0x0e] = *(const uint8_t*)buf;
			else
				*(uint8_t*)buf = mDS1287.ram[pa - 0x0e];
			break;
	}
	
	return true;
}

static uint_fast8_t rtcPrvDaysPerMonth(uint_fast16_t year, uint_fast8_t month)
{
	const uint8_t *mdays;
	bool isLeap;
	
	if (year % 4)
		isLeap = false;
	else if (year % 100)
		isLeap = true;
	else
		isLeap = !(year % 400);
	
	mdays = isLeap ? mMonthDaysLeap : mMonthDaysNormal;

	return mdays[month - 1];
}


// 1Hz timer ISR
bool oneHz_callback(struct repeating_timer *t) {

	bool newIrq = false, doAlarm = false;
	
	if (++mCurTime.time.s == 60) {
		
		mCurTime.time.s = 0;
		if (++mCurTime.time.m == 60) {
			
			mCurTime.time.m = 0;
			if (++mCurTime.time.h == 24) {
				
				uint_fast8_t mDays = rtcPrvDaysPerMonth(mCurTime.date.year, mCurTime.date.month);
				mCurTime.time.h = 0;
				
				if (mCurTime.date.dow++ == 7)
					mCurTime.date.dow = 1;
				
				if (mCurTime.date.day++ == mDays) {
					
					mCurTime.date.day = 1;
					if (mCurTime.date.month++ == 12) {
						
						mCurTime.date.month = 1;
						mCurTime.date.year++;
					}
				}
			}
		}
	}
	
	if (!mDS1287.uf) {	//update irq
			
		mDS1287.uf = 1;
		if (mDS1287.uie)
			newIrq = true;
	}
	
	if (mAlarmTime.h >= 0xc0) {	//do not care for hours - alarm every hour
		if (!mCurTime.time.m)
			doAlarm = true;
		if (mAlarmTime.m >= 0xc0) {
			if (!mCurTime.time.s)
				doAlarm = true;
			if (mAlarmTime.s >= 0xc0)
				doAlarm = true;
		}
	}
	else if (mCurTime.time.h == mAlarmTime.h && mCurTime.time.m == mAlarmTime.m && mCurTime.time.s == mAlarmTime.s)
		doAlarm = true;
	
	if (doAlarm && !mDS1287.af) {
			
		mDS1287.af = 1;
		if (mDS1287.aie)
			newIrq = true;
	}
	
	if (newIrq && !mDS1287.irqf) {
		mDS1287.irqf = 1;
		//cpuIrq(SOC_IRQNO_RTC, true);
	}
	
	//NVIC_ClearPendingIRQ(RtcHz_IRQn);

	return true;
}


#if 0
void __attribute__((used)) OS_TIMER_IRQHandler(void)
{
	NVIC_ClearPendingIRQ(OsTimer_IRQn);
	osTimerReset();
	
	//perodic
	if (!mDS1287.pf) {
		
		mDS1287.pf = 1;
		if (mDS1287.pie && !mDS1287.irqf) {
			mDS1287.irqf = 1;
			cpuIrq(SOC_IRQNO_RTC, true);
		}
	}
}
#endif
// Periodic timer ISR
/* From the DS12887 datasheet:
   The Periodic Interrupt Flag (PF) is a read–only bit which is set to a 1
 when an edge is detected on the selected tap of the divider chain. The RS3
 through RS0 bits establish the periodic rate. PF is set to a 1
independent of the state of the PIE bit. When both PF and PIE are 1s, the IRQ signal is active and will set
the IRQF bit. The PF bit is cleared by a RESET or a software read of Register C.
*/

uint8_t prev_rs;

bool rtc_callback(struct repeating_timer *t) {

  // Test if time to change period
  if (prev_rs != mDS1287.rs) {
    static const int64_t usVals[] ={0, -3906, -7812, -122, -244, -488,
				    -977, -1953, -3906,
				    -7813, -15625, -31250,
				    -62500, -125000,
				    -250000, -500000};

    uint8_t val = mDS1287.rs;
    // Remove old repeating timer
    cancel_repeating_timer(&timerRTC);
    // Add new one
    add_repeating_timer_us(usVals[val],
			   rtc_callback, NULL,
			   &timerRTC);
    prev_rs = mDS1287.rs;
  }

  // Deliver an interrupt once per RTC IRQ status assert if enabled.
  // May not be a precise emulation, since the data sheet implies
  // that the irq pin is asserted as long as the pf flag is set

  //  if (mDS1287.pie && !mDS1287.pf) {
  if (mDS1287.pie) {
    if (mDS1287.pf) {
      cpuIrq(SOC_IRQNO_RTC, true);
    } else {
      cpuIrq(SOC_IRQNO_RTC, false);
    }
  }

  // Emulate the periodic event flag. Only cleared by a read from reg 0x0c
  if (!mDS1287.pf) {
    mDS1287.pf = 1;
    // Update interrupt flag
    mDS1287.irqf = 1;
  }

  return true;
}


bool ds1287init(void)
{
	// init data
	mDS1287.dm = 1;
	mDS1287.m2412 = 1;
	mDS1287.ram[DEC_REAL_YEAR_LOC] = 22;	//2022

	// Create alarm pool for repeating timers to use
	alarm_pool_init_default();

	// -1MS so that the timer will repeat 1 per sec, regardless of how
	// long the callback takes to execute
	add_repeating_timer_ms(-1000, oneHz_callback, NULL, &timerHz);

	// Add a default RTC timer, so that we can remove it later
	uint32_t addRet = add_repeating_timer_us(-62500, rtc_callback, NULL, &timerRTC);
	mDS1287.rs = prev_rs = 15;

	//add memory
	return memRegionAdd(0x1d000000, 0x01000000, ds1287prvMemAccess);
}



