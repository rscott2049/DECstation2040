/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/


#pragma GCC optimize ("Os")


#include <string.h>
#include <stdio.h>
#include "printf.h"
#include "dz11.h"
#include "mem.h"
#include "soc.h"
#include "cpu.h"


#define VERBOSE				0
//#define VERBOSE				1


#define DZ11_REG_CSR			0
#define DZ11_REG_LPR_RBUF		1	//RBUF is RO, LPR is WO
#define DZ11_REG_TCR			2
#define DZ11_REG_MSR_TDR		3	//MSR is RO, TDR is WO


#define UART_RX_BUF_SZ		64
#define NUM_UARTS			4

#define FIFO_ALARM_COUNT	16	//not a LEVEL!!!

#define CSR_TRDY			0x8000
#define CSR_TIE				0x4000
#define CSR_SA				0x2000
#define CSR_SAE				0x1000
#define CST_TXLINE_MASK		0x0700
#define CST_TXLINE_SHIFT	8
#define CSR_RDONE			0x0080
#define CSR_RIE				0x0040
#define CSR_MSE				0x0020
#define CSR_CLR				0x0010
#define CSR_MAINT			0x0008

#define RBUF_DATAVALID		0x8000
#define RBUF_OVERFLOW		0x4000
#define RBUF_FRERR			0x2000
#define RBUF_PERR			0x1000
#define RBUF_RXLINE_MASK	0x0700
#define RBUF_RXLINE_SHIFT	8
#define RBUF_RBUF_MASK		0x00ff
#define RBUF_RBUF_SHIFT		0

#define LPR_RXON			0x1000
#define LPR_SPEED_MASK		0x0f00
#define LSR_SPEED_SHIFT		8
#define LSR_ODDP			0x0080
#define LSR_PAREN			0x0040
#define LST_STOP			0x0020
#define LPR_CLEN_MASK		0x0018
#define LSR_CLEN_SHIFT		3
#define LSR_LINE_MASK		0x0007
#define LSR_LINE_SHIFT		0


struct Line {
	uint8_t buf[UART_RX_BUF_SZ];
	uint8_t rxReadPtr, rxBytesUsed;
	uint8_t nBytesRxedSinceLastRead;
	uint8_t rxOvr	:1;
	uint8_t rxEna	:1;
};

struct {
	struct Line line[NUM_UARTS];
	uint16_t enabled	: 1;	//CSR.MSE
	uint16_t rie		: 1;	//CSR.RIE
	uint16_t sae		: 1;	//CSR.SAE
	uint16_t tie		: 1;	//CSR.TIE
	uint16_t maint		: 1;	//CSR.MAINT
	
	//calculated
	uint16_t sa			: 1;	//CSR.SA
	uint16_t rdone		: 1;	//CSR.RDONE
	uint16_t trdy		: 1;	//CSR.TRDY
	uint16_t txLine		: 3;
	
	uint8_t tcr;
} gDZ11 = {};





static void dz11PrvRecalc(void)
{
	struct Line *line = &gDZ11.line[NUM_UARTS - 1];
	uint_fast8_t mask = 1 << (NUM_UARTS - 1);
	bool irq = false;
	int_fast8_t i;
	
	if (gDZ11.enabled) {
	
		gDZ11.rdone = false;
		gDZ11.sa = false;
		gDZ11.trdy = false;
		
		for (i = NUM_UARTS - 1; i >= 0; i--, line--, mask >>= 1) {
		
			//RDONE
			if (line->rxBytesUsed)
				gDZ11.rdone = true;
			
			//SA?
			if (line->nBytesRxedSinceLastRead >= FIFO_ALARM_COUNT)
				gDZ11.sa = true;
			
			//TX?
			if (gDZ11.tcr & mask) {	//we are always ready to tx
				
				if (!gDZ11.trdy) {
					
					gDZ11.trdy = true;
					gDZ11.txLine = i;
				}
			}
		}
		
		if (gDZ11.rie) {
			
			if ((gDZ11.sae && gDZ11.sa) || (!gDZ11.sae && gDZ11.rdone))
				irq = true;
		}
		
		if (gDZ11.tie && gDZ11.trdy)
			irq = true;
	}
	
	if (VERBOSE)
		err_str(" DZ11 irq: %d\r\n", irq);
	cpuIrq(SOC_IRQNO_UART, irq);
}

static void dz11prvReset(void)
{
	memset(&gDZ11, 0, sizeof(gDZ11));
	dz11PrvRecalc();
}

static void dz11prvTx(uint_fast8_t val)		//assumes it will be followed by recalc
{
	if (!gDZ11.enabled)
		err_str("DZ11: write while disabled\r\n");
	else {
		if (!gDZ11.trdy)
			err_str("DZ11: write while no TRDY\r\n");
		else {
		
			dz11charPut(gDZ11.txLine, val);	//our UARTs are instant and never busy
			//loopback
			if (gDZ11.maint)
				dz11charRx(gDZ11.txLine, val);
			gDZ11.trdy = false;
		}
	}
}

static uint_fast16_t dz11prvRx(void)	//assumes it will be followed by recalc
{
	struct Line *line = &gDZ11.line[NUM_UARTS - 1];
	int_fast8_t i;
	
	
	if (!gDZ11.enabled) {
		
		err_str("DZ11: read while disabled");
		return 0;
	}
	
	for (i = NUM_UARTS - 1; i >= 0; i--, line--) {
		
		if (line->rxBytesUsed) {
			
			uint_fast16_t ret = RBUF_DATAVALID;
			if (line->rxOvr) {
				line->rxOvr = false;
				ret |= RBUF_OVERFLOW;
			}
			ret |= (((uint_fast16_t)line->buf[line->rxReadPtr]) << RBUF_RBUF_SHIFT) & RBUF_RBUF_MASK;
			if (++line->rxReadPtr == UART_RX_BUF_SZ)
				line->rxReadPtr = 0;
			line->rxBytesUsed--;
			line->nBytesRxedSinceLastRead = 0;
			ret |= (((uint_fast16_t)i) << RBUF_RXLINE_SHIFT) & RBUF_RXLINE_MASK;
			
			dz11rxSpaceNowAvail(i);
			
			return ret;
		}
	}
	
	return 0;
}

uint_fast8_t dz11numBytesFreeInRxBuffer(uint_fast8_t lineNo)
{
	struct Line *line;
	
	if (lineNo >= NUM_UARTS)
		return 0;
	
	line = &gDZ11.line[lineNo];
	
	return UART_RX_BUF_SZ - line->rxBytesUsed;
}

void dz11charRx(uint_fast8_t lineNo, uint_fast8_t chr)
{
	struct Line *line;
	
	if (lineNo >= NUM_UARTS)
		return;
	
	line = &gDZ11.line[lineNo];
	
	if (!gDZ11.enabled || !line->rxEna)
		return;
	
	if (line->rxBytesUsed >= UART_RX_BUF_SZ)
		line->rxOvr = true;
	else
		line->buf[(line->rxReadPtr + line->rxBytesUsed++) % UART_RX_BUF_SZ] = chr;
	if (!++line->nBytesRxedSinceLastRead)	//do not overflow
		line->nBytesRxedSinceLastRead--;
	dz11PrvRecalc();
}

static bool dz11prvRealMemWrite(uint_fast8_t regIdx, uint_fast16_t v)
{
	uint_fast8_t lineNo;
	
	switch (regIdx) {
		case DZ11_REG_CSR:
			if (VERBOSE)
				err_str(" DZ11 0x%04x-> CSR\r\n", (unsigned)v);
			if (v & CSR_CLR)
				dz11prvReset();
			else {
				
				gDZ11.enabled = !!(v & CSR_MSE);
				gDZ11.rie = !!(v & CSR_RIE);
				gDZ11.sae = !!(v & CSR_SAE);
				gDZ11.tie = !!(v & CSR_TIE);
				gDZ11.maint = !!(v & CSR_MAINT);
			}
			break;
		
		case DZ11_REG_LPR_RBUF:	//LPR
			if (VERBOSE)
				err_str(" DZ11 0x%04x-> LPR\r\n", (unsigned)v);
			lineNo = (v & LSR_LINE_MASK) >> LSR_LINE_SHIFT;
			if (lineNo >= NUM_UARTS)
				return false;
			gDZ11.line[lineNo].rxEna = !!(v & LPR_RXON);
			break;
		
		case DZ11_REG_TCR:
			if (VERBOSE)
				err_str(" DZ11 0x%04x-> TCR\r\n", (unsigned)v);
			gDZ11.tcr = v;
			break;
		
		case DZ11_REG_MSR_TDR:	//TDR
			if (VERBOSE)
				err_str(" DZ11 0x%04x-> TDR\r\n", (unsigned)v);
			dz11prvTx(v);
			break;
		
		default:
			if (VERBOSE)
				err_str(" DZ11 unknown write\r\n");
			return false;
	}
	
	dz11PrvRecalc();
	return true;
}

static bool dz11prvRealMemRead(uint_fast8_t regIdx, uint16_t *valP)
{
	uint_fast16_t v;
	
	switch (regIdx) {
		case DZ11_REG_CSR:
			v = 0;
			if (gDZ11.enabled)
				v |= CSR_MSE;
			if (gDZ11.rie)
				v |= CSR_RIE;
			if (gDZ11.sae)
				v |= CSR_SAE;
			if (gDZ11.tie)
				v |= CSR_TIE;
			if (gDZ11.maint)
				v |= CSR_MAINT;
			if (gDZ11.sa)
				v |= CSR_SA;
			if (gDZ11.rdone)
				v |= CSR_RDONE;
			if (gDZ11.trdy) {
				v |= CSR_TRDY;
				v |= (((uint32_t)gDZ11.txLine) << CST_TXLINE_SHIFT) & CST_TXLINE_MASK;
			}
			if (VERBOSE)
				err_str(" DZ11 CSR -> 0x%04x\r\n", (unsigned)v);
			break;
		
		case DZ11_REG_LPR_RBUF:	//RBUF
			v = dz11prvRx();
			if (VERBOSE)
				err_str(" DZ11 RBUF -> 0x%04x\r\n", (unsigned)v);
			break;
		
		case DZ11_REG_TCR:
			v = gDZ11.tcr;
			if (VERBOSE)
				err_str(" DZ11 TCR -> 0x%04x\r\n", (unsigned)v);
			break;
		
		case DZ11_REG_MSR_TDR:	//MSR
			v = 0;
			if (VERBOSE)
				err_str(" DZ11 MSR -> 0x%04x\r\n", (unsigned)v);
			break;
		
		default:
			if (VERBOSE)
				err_str(" DZ11 unknown read\r\n");
			return false;
	}

	*valP = v;	
	dz11PrvRecalc();
	return true;
}

static bool dz11prvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint_fast8_t regIdx;
	uint16_t val;
		
	pa &= 0x00ffffff;
	regIdx = pa / 8;
	
	if ((pa & 7) || pa > 0x18)
		return false;

	switch (size) {
		case 1:
			if (write)
				return false;
			else {
				if (!dz11prvRealMemRead(regIdx, &val))
					return false;
				*(uint8_t*)buf = val;
				return true;
			}
			break;
		
		case 2:
			if (write)
				return dz11prvRealMemWrite(regIdx, *(const uint16_t*)buf);
			else
				return dz11prvRealMemRead(regIdx, (uint16_t*)buf);
			break;
		
		case 4:
			if (write)
				return dz11prvRealMemWrite(regIdx, *(const uint32_t*)buf);
			else {
				if (!dz11prvRealMemRead(regIdx, &val))
					return false;
				*(uint32_t*)buf = val;
				return true;
			}
			break;
		
		default:
			return false;
		
	}
}

bool dz11init(void)
{

	dz11prvReset();
	gDZ11.enabled = 1;
	
	return memRegionAdd(0x1c000000, 0x01000000, dz11prvMemAccess);
}
