/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SOC_H_
#define _SOC_H_

#define RAM_AMOUNT	(32<<20)
#define RAM_BASE	0x00000000UL
#define DS_ROM_BASE	0x1FC00000UL	/* as per spec */

#include <stdbool.h>
#include "cpu.h"


#define MASS_STORE_OP_GET_SZ	0	//in blocks
#define MASS_STORE_OP_READ	1
#define MASS_STORE_OP_WRITE	2
#define MASS_STORE_OP_BUF_RW	3
#define BLK_DEV_BLK_SZ		512

typedef bool (*MassStorageF)(uint8_t op, uint32_t val, void *buf);


bool socInit(MassStorageF diskF);
void socRun(int gdbPort);


///SoC IRQ numbers:
// 2 - SCSI
// 3 - Ethernet
// 4 - UARTs
// 5 - RTC
// 6 - ?
// 7 - bus interface unit
#define SOC_IRQNO_SCSI		2
#define SOC_IRQNO_ETHERNET	3
#define SOC_IRQNO_UART		4
#define SOC_IRQNO_RTC		5


//externally provided
void socInputCheck(void);


#endif
