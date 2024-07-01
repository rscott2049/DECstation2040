/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
#elif defined(CPU_SAMD)
	#include "samda1e16b.h"
#elif defined(CPU_CORTEXEMU)
	#include "CortexEmuCpu.h"
#endif

//#include <stdio.h>
//#include <string.h>
//#include "pico/stdlib.h"
#include "hw_config.h"
#include "sd_card.h"
#include "ff.h"
#include "f_util.h"
#include "ff_stdio.h"
#include "r3k_config.h"
#include "../hypercall.h"
#include "scsiNothing.h"
#include "scsiDisk.h"
#include "graphics.h"
#include "timebase.h"
#include "spiRam.h"
#include "printf.h"
#include "decBus.h"
#include "ds1287.h"
#include "lance.h"
#include "usart.h"
#include "ucHw.h"
#include "sdHw.h"
#include "dz11.h"
#include "soc.h"
#include "mem.h"
#include "sii.h"
#include "sd.h"
#include "usbHID.h"

uint32_t mFbBase, mPaletteBase, mCursorBase;
static uint32_t mSiiRamBase, mRamTop;
static uint8_t mDiskBuf[SD_BLOCK_SIZE];
static struct ScsiNothing gNoDisk;
static struct ScsiDisk gDisk;

static sd_card_t *pSD0;

static FIL gDiskFile;

static const uint8_t gRom[] = 
{
	#include "loader.inc"
};


void delayMsec(uint32_t msec)
{
	uint64_t till = getTime() + (uint64_t)msec * (TICKS_PER_SECOND / 1000);
	
	while (getTime() < till);
}


void prPutchar(char chr)
{
	#ifdef SUPPORT_DEBUG_PRINTF
	
#if 0
		#if (ZWT_ADDR & 3)		//byte
		
			*(volatile uint8_t*)ZWT_ADDR = chr;
					
		#else
			volatile uint32_t *addr = (volatile uint32_t*)ZWT_ADDR;
			
			while(addr[0] & 0x80000000ul);
			addr[0] = 0x80000000ul | (uint8_t)chr;
		#endif
#endif
	#endif
	
	#if 1
		if (chr == '\n')
			usartTx('\r');
			
		usartTx(chr);
		//putchar(chr);
	#endif
}

void fastpathReport(uint32_t val, uint32_t addr)
{
	pr("fastpath report: [%08x] = %08x\n", addr
	   , val);
}

void dz11charPut(uint_fast8_t line, uint_fast8_t chr)
{
	(void)chr;

	#ifdef MULTICHANNEL_UART
		
	usartTxEx(line, chr);
		
	#else
	
	if (line == 3)
	  usartTx(chr);
	
	#endif
	
	if (line == 0) decKeyboardTx(chr);	

	if (line == 1) decMouseTx(chr);

	if (line == 3) {
		/*	--	for benchmarking
		static uint8_t state = 0;
		
		switch (state) {
			case 0:
				if (chr == 'S')
					state = 1;
				break;
			case 1:
				state = (chr == 'C') ? 2 : 0;
				break;
			
			case 2:
				state = (chr == 'S') ? 3 : 1;
				break;
		
			case 3:
				state = (chr == 'I') ? 4 : 0;
				break;
			
			case 4:
				;
				uint64_t time = getTime();
				pr("\ntook 0x%08x%08x\n", (uint32_t)(time >> 32), (uint32_t)time);
				state = 5;
				break;
		}
		
		//*/
	//	prPutchar(chr);
	}
}

#if 0
#ifdef SUPPORT_MULTIBLOCK_ACCESSES_TO_SD		//worked on previous boards, will not anymore since we now share an SPI bus between RAM and SD
	

	static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
	{
		static uint32_t mCurSec = 0xffffffff;
		static uint8_t mCurOp = 0xff;
		uint_fast8_t nRetries;
		
		switch (op) {
			case MASS_STORE_OP_GET_SZ:
				 *(uint32_t*)buf = sdGetNumSecs();
				 return true;
			
			case MASS_STORE_OP_READ:
			//	return sdSecRead(sector, buf);
				
				if (mCurOp == MASS_STORE_OP_READ && mCurSec == sector){
					
					if (sdReadNext(buf)) {
						mCurSec++;
						return true;
					}
					else {
						
						pr("failed to read next\n");
						sdReportLastError();
						mCurOp = 0xff;
						(void)sdReadStop();
						return false;
					}
				}
				
				if (mCurOp == MASS_STORE_OP_WRITE) {
					
					mCurOp = 0xff;
					if (!sdWriteStop()) {
						
						pr("failed to stop a write\n");
						sdReportLastError();
						return false;
					}
				}
				else if (mCurOp == MASS_STORE_OP_READ) {
					
					mCurOp = 0xff;
					if (!sdReadStop()) {
						
						pr("failed to stop a read\n");
						sdReportLastError();
						return false;
					}
				}
				
				for (nRetries = 0; nRetries < 2; nRetries++) {
				
					if (!sdReadStart(sector, 0)) {
						
						(void)sdReadStop();
						pr("failed to start a read\n");
						sdReportLastError();
						continue;
					}
					if (!sdReadNext(buf)) {
						
						(void)sdReadStop();
						pr("failed to read first sec\n");
						sdReportLastError();
						continue;
					}
					mCurOp = MASS_STORE_OP_READ;
					mCurSec = sector + 1;
					return true;
				}
				pr("giving up\n");
				return false;
				
			case MASS_STORE_OP_WRITE:
			
			//	return sdSecWrite(sector, buf);
			
				if (mCurOp == MASS_STORE_OP_WRITE && mCurSec == sector){
					
					if (sdWriteNext(buf)) {
						mCurSec++;
						return true;
					}
					else {
						
						pr("failed to write next\n");
						sdReportLastError();
						mCurOp = 0xff;
						(void)sdWriteStop();
						return false;
					}
				}
				
				if (mCurOp == MASS_STORE_OP_WRITE) {
					
					mCurOp = 0xff;
					if (!sdWriteStop()) {
						
						pr("failed to stop a write\n");
						sdReportLastError();
						return false;
					}
				}
				else if (mCurOp == MASS_STORE_OP_READ) {
					
					mCurOp = 0xff;
					if (!sdReadStop()) {
						
						pr("failed to stop a read\n");
						sdReportLastError();
						return false;
					}
				}
				
				///aaa
				mCurOp = 0xff;
				return sdSecWrite(sector, buf);
				///aaa
				
				
				for (nRetries = 0; nRetries < 2; nRetries++) {
				
					if (!sdWriteStart(sector, 0)) {
						
						pr("failed to start a write\n");
						sdReportLastError();
						(void)sdWriteStop();
						continue;
					}
					if (!sdWriteNext(buf)) {
						
						(void)sdWriteStop();
						pr("failed to write first sec\n");
						sdReportLastError();
						continue;
					}
					mCurOp = MASS_STORE_OP_WRITE;
					mCurSec = sector + 1;
					return true;
				}
				pr("giving up\n");
				return false;
		}
		return false;
	}
	
#else

	static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
	{
		uint_fast8_t nRetries;
		
		switch (op) {
			case MASS_STORE_OP_GET_SZ:
				 *(uint32_t*)buf = sdGetNumSecs();
				 return true;
			
			case MASS_STORE_OP_READ:
				return sdSecRead(sector, buf);
				
			case MASS_STORE_OP_WRITE:
				return sdSecWrite(sector, buf);
		}
		return false;
	}

#endif
#endif
// rp2040 FAT file system level access
static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
{
  FRESULT fr;
  unsigned int br;

	switch (op) {
		case MASS_STORE_OP_GET_SZ:
		  *(uint32_t*)buf = (uint32_t)(f_size(&gDiskFile)/(uint64_t)BLK_DEV_BLK_SZ);
		  return true;
		  
		case MASS_STORE_OP_READ:
		  f_lseek(&gDiskFile, (uint64_t)sector * (uint64_t)BLK_DEV_BLK_SZ);
		  f_read(&gDiskFile, buf, BLK_DEV_BLK_SZ, &br);
		  return br == BLK_DEV_BLK_SZ;

		case MASS_STORE_OP_WRITE:
		  f_lseek(&gDiskFile, (uint64_t)sector * (uint64_t)BLK_DEV_BLK_SZ);
		  f_write(&gDiskFile, buf, BLK_DEV_BLK_SZ, &br);
		  return br == BLK_DEV_BLK_SZ;
	}
	return false;
}


static bool accessRom(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	const uint8_t *mem = gRom;

	pa -= (DS_ROM_BASE & 0x1FFFFFFFUL);
	
	if (write)
		return false;
	else if (size == 4)
		*(uint32_t*)buf = *(uint32_t*)(mem + pa);
	else if (size == 1)
		*(uint8_t*)buf = mem[pa];
	else if (size == 2)
		*(uint16_t*)buf = *(uint16_t*)(mem + pa);
	else
		memcpy(buf, mem + pa, size);
	
	return true;
}

static bool accessRam(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{	

	if (write)
		spiRamWrite(pa, buf, size);
	else
		spiRamRead(pa, buf, size);

#if 0
	if (write) {
	  if ((pa & 0x3) != 0) {
	    pr("wr: unaligned access at: %08x size: %d\n", pa, size);
	    while(1) {}
	  }
	} else {
	  if ((pa & 0x3) != 0) {
	    pr("rd: unaligned access at: %08x size: %d data: %02x\n",
	       pa, size, *(uint8_t*)buf);
	  }
	}
#endif
	return true;
}

bool cpuExtHypercall(void)	//call type in $at, params in $a0..$a3, return in $v0, if any
{
	uint32_t hyperNum = cpuGetRegExternal(MIPS_REG_AT), t,  ramMapNumBits, ramMapEachBitSz;
	uint_fast16_t ofst;
	uint32_t blk, pa;
	uint8_t chr;
	bool ret;

	switch (hyperNum) {
		case H_GET_MEM_MAP:		//stays for booting older images which expect this
		
			switch (pa = cpuGetRegExternal(MIPS_REG_A0)) {
				case 0:
					pa = 1;
					break;
				
				case 1:
					pa = spiRamGetAmt();
					break;
				
				case 2:
					pa = 1;
					break;
				
				default:
					pa = 0;
					break;
			}
			cpuSetRegExternal(MIPS_REG_V0, pa);
			break;
		
		case H_CONSOLE_WRITE:
			chr = cpuGetRegExternal(MIPS_REG_A0);
			if (chr == '\n') {
			  //	prPutchar('\r');
			  usartTx('\r');
			}
			//prPutchar(chr);
			usartTx(chr);
			break;
		
		case H_STOR_GET_SZ:
			if (!massStorageAccess(MASS_STORE_OP_GET_SZ, 0, &t))
				return false;
			cpuSetRegExternal(MIPS_REG_V0, t);
			break;
		
		case H_STOR_READ:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			ret = massStorageAccess(MASS_STORE_OP_READ, blk, mDiskBuf);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_WR_SZ)
				spiRamWrite(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_WR_SZ);
			cpuSetRegExternal(MIPS_REG_V0, ret);

#if 0
			uint8_t tmp_buf[SD_BLOCK_SIZE];
			uint32_t csum = 0;
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_WR_SZ)
				spiRamRead(pa + ofst, tmp_buf + ofst, OPTIMAL_RAM_RD_SZ);

			for (int i = 0; i < SD_BLOCK_SIZE; i++) {
			  if (tmp_buf[i] != mDiskBuf[i]) {
			    pr("rd fail exp/act: %02x %02x\n", 
				   mDiskBuf[i], tmp_buf[i]);
			  }
			  csum += tmp_buf[i];
			}
			pr("rd blk csum: %d %08x\n", blk, csum);
			//if (blk > 31) while(1) {}
#endif
			if (!ret) {
				
				pr(" rd_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
				while(1);
				hwError(6);
			}
			break;
		
		case H_STOR_WRITE:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_RD_SZ)
				spiRamRead(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_RD_SZ);
			ret = massStorageAccess(MASS_STORE_OP_WRITE, blk, mDiskBuf);
			cpuSetRegExternal(MIPS_REG_V0, ret);
			if (!ret) {
				
				pr(" wr_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
				hwError(5);
			}
			break;
		
		case H_TERM:
			pr("termination requested\n");
			hwError(7);
			break;

		default:
			pr("hypercall %u @ 0x%08x\n", hyperNum, cpuGetRegExternal(MIPS_EXT_REG_PC));
			return false;
	}
	return true;
}

static bool mReportCy = false;

void cycleReport(uint32_t instr, uint32_t addr)
{
	uint_fast8_t i;
	
	if (mReportCy){
	
		//proper save/restore state not beig done. some regs will not read properly, like pc
		pr("[%08x]=%08x {", addr, instr);
		
		for (i = 0; i < 32; i++) {
			if (!(i & 7))
				pr("%u: ", i);
			pr(" %08x", cpuGetRegExternal(i));
		}
		pr("}\n");
	}
	else if (addr == 0x800D4FEC)
		mReportCy = 1;
}

void usartExtRx(uint8_t val)
{
  dz11charRx(3, val);
}

void reportInvalid(uint32_t pc, uint32_t instr)
{
	//inval used for emulation
	if (instr == 0x0000000F)
		return;
	
	//rdhwr
	if (instr == 0x7C03E83B)
		return;
	
	pr("INVAL [%08x] = %08x\n", pc, instr);
}

static void showBuf(uint_fast8_t idx, const uint8_t *buf)
{
	uint_fast16_t i;
	
	pr("BUFFER %u", idx);
	
	for (i = 0; i < 512; i++) {
		
		if (!(i & 15))
			pr("\n  %03x  ", i);
		pr(" %02x", *buf++);
	}
	pr("\n");
}

static void usartPuts(const char *s)
{
	char ch;
	
	while ((ch = *s++) != 0)
		usartTx(ch);
}

static void usartPutSmallDecimal(uint_fast8_t val)
{
	uint_fast8_t i;
	char ch[5];
	
	ch[i = sizeof(ch) - 1] = 0;
	do {
		uint_fast8_t div10 = (6554U * val) >> 16;
		
		ch[--i] = '0'+ val - 10 * div10;
		val = div10;
		
	} while (val);
	
	usartPuts(ch + i);
}

void dz11rxSpaceNowAvail(uint_fast8_t line)
{
	(void)line;
}

int main(void)
{
	uint8_t eachChipSz, numChips, chipWidth;
	volatile uint32_t t = 0;
	uint16_t cy = 0;

	initHwSuperEarly();

	usartInit();

	usbhid_init();

	// This must be called before sd_get_drive_prefix:
        sd_init_driver(); 

	pSD0 = sd_get_by_num(0);
	char const *drive_prefix = sd_get_drive_prefix(pSD0);
	FRESULT fr = f_mount(&pSD0->state.fatfs, drive_prefix, 1);
	if (FR_OK != fr) {
	  hwError(2);
	  pr("SD mount error: %s (%d)\n\r", FRESULT_str(fr), fr);
	} else {
	  pr("SD mount OK!\n\r");
	}

#if 0
        fr  = f_open(&gDiskFile, "ultrix.textmode", FA_READ | FA_WRITE);
	if(fr != FR_OK){
	  pr("Failed to open %s\n", "ultrix.textmode");
		return -1;
	}
	else pr("Opened %s\n", "ultrix.textmode");
#endif

#if 1
        fr  = f_open(&gDiskFile, "ultrix.gui", FA_READ | FA_WRITE);
	if(fr != FR_OK){
	  pr("Failed to open %s\n", "ultrix.gui");
		return -1;
	}
	else pr("Opened %s\n", "ultrix.gui");
#endif

#if 0
        fr  = f_open(&gDiskFile, "linux.wheezy", FA_READ | FA_WRITE);
	if(fr != FR_OK){
	  pr("Failed to open %s\n", "linux.wheezy");
		return -1;
	}
	else pr("Opened %s\n", "linux.wheezy");
#endif


#if 0
	//	while(1) {
	for(int i = 0; i<3; i++) {
	  pr("hello\n");
	  sleep_ms(1000);
	}


	uint32_t buff;
	massStorageAccess(MASS_STORE_OP_GET_SZ,  0, (void *)&buff);
	pr("size: %d\n", buff);

	pr("size: %llu\n", f_size(&gDiskFile));
	pr("size mb: %llu\n", f_size(&gDiskFile)/(1024*1024));
	pr("size gb: %llu\n", f_size(&gDiskFile)/(1024*1024*1024));


	pr("seek to end: %d\n", f_lseek(&gDiskFile, (uint64_t)buff * (uint64_t) BLK_DEV_BLK_SZ));
	pr("seek to beginning: %d\n", ff_fseek(&gDiskFile, 0, SEEK_SET));
	pr("seek to end: %d\n", ff_fseek(&gDiskFile, 0, SEEK_END));

	pr("tell: %d\n", ff_ftell(&gDiskFile)/BLK_DEV_BLK_SZ);


	while(1) {}
#endif
	//return 0;

	initHw();
	asm volatile("cpsie i");
	timebaseInit();
	//usartInit();
	
	pr("ready, time is 0x%016llx\n", getTime());
	pr("ready, time is 0x%016llx\n", getTime());

#if 0
	if (!sdCardInit(mDiskBuf)) {
		
		hwError(2);
		pr("SD card init fail\n");
	}
#else 
	if (0) {
	  
	}
#endif
	else if (!spiRamInit(&eachChipSz, &numChips, &chipWidth)) {
	        hwError(3);
		pr("SPI ram init issue\n");
	}
#if 1
	else if (!graphicsInit()) {
	  pr("Graphics init issue\n");
	}
#endif
	else {
	  	uint32_t ramAmt = spiRamGetAmt();
		pr("ram: %d\n", ramAmt/(1024*1024));

		cpuInit(ramAmt);

		uint_fast8_t i;
		
		//divvy up the RAM
		mSiiRamBase = ramAmt -= SII_BUFFER_SIZE;
		mFbBase = ramAmt -= SCREEN_BYTES;
		mPaletteBase = ramAmt -= SCREEN_PALETTE_BYTES;
		mCursorBase = ramAmt -= SCREEN_CURSOR_BYTES;
		// Set screen display/palette/cursor start appropriately
		graphicsSetStart(mFbBase, mPaletteBase, mCursorBase);
		//round usable ram to page size
		mRamTop = ramAmt = (ramAmt >> 12) << 12;
		pr("ramtop: %d\n", mRamTop/(1024*1024));

		pr("ram:         0x%08x - 0x%08x\n", 0x0, mRamTop - 1);
		pr("palette:     0x%08x - 0x%08x\n",
		   mPaletteBase, mPaletteBase + SCREEN_PALETTE_BYTES - 1);
		pr("framebuffer: 0x%08x - 0x%08x\n",
		   mFbBase, mFbBase + SCREEN_BYTES - 1);
		
#if 0
		extern uint32_t *mCpu;
#define OFST_MEMLIMIT			0x3c
#define NUM_REGS				32
#define OFST_PART2				(NUM_REGS * 4)

		pr("cpuAsm mtop: 0x%08x\n", mCpu[OFST_PART2 + OFST_MEMLIMIT]);
#endif

		if (!memRegionAdd(RAM_BASE, ramAmt, accessRam))
			pr("failed to init %s\n", "RAM");
		else if (!memRegionAdd(DS_ROM_BASE & 0x1FFFFFFFUL, sizeof(gRom), accessRom))
			pr("failed to init %s\n", "ROM");
		else if (!decBusInit())
			pr("failed to init %s\n", "DEC BUS");
		else if (!dz11init())
			pr("failed to init %s\n", "DZ11");
		else if (!siiInit(7))
			pr("failed to init %s\n", "SII");
		else if (!scsiDiskInit(&gDisk, 6, massStorageAccess, mDiskBuf, false))
			pr("failed to init %s\n", "SCSI disc");
		else {
			for (i = 0; i < 6; i++) {
				if (!scsiNothingInit(&gNoDisk, i)) {
					
					pr("failed to init %s\n", "SCSI dummy");
					break;
				}
			}
			
			if (i == 6) {
				
				if (!ds1287init())
					pr("failed to init %s\n", "DS1287");
				else if (!lanceInit())
					pr("failed to init %s\n", "LANCE");
				else {
					
					char x[16];
					usartPuts("uMIPS v2.2.0 (BL ver ");
					usartPutSmallDecimal(*(volatile uint8_t*)8 - 0x10);

					usartPuts(")\r\n will run with ");
#if 0
					usartPutSmallDecimal(numChips);
					usartTx('x');
					usartPutSmallDecimal(eachChipSz);
					usartPuts("MB x");
					usartPutSmallDecimal(chipWidth);
					usartPuts(" RAMs, \r\n");
#else
					usartPutSmallDecimal(EMULATOR_RAM_MB);
					usartPuts("MB\r\n");
#endif					
					printRegions();

					cpuInit(ramAmt);
					while(1) {
					  cy++;
					  cpuCycle(ramAmt);
					}
					while(1);
				}
			}	
		}
		hwError(4);
	}

	while(1);
}

void siiPrvBufferWrite(uint_fast16_t wordIdx, uint_fast16_t val)
{
	uint32_t addr = mSiiRamBase + wordIdx * 2;
	uint16_t v = val;
	
	spiRamWrite(addr, &v, 2);
}

uint_fast16_t siiPrvBufferRead(uint_fast16_t wordIdx)
{
	uint16_t ret;
	
	spiRamRead(mSiiRamBase + wordIdx * 2, &ret, 2);
	
	return ret;
}


void __attribute__((used)) report_hard_fault(uint32_t* regs, uint32_t ret_lr, uint32_t *user_sp)
{
	#ifdef SUPPORT_DEBUG_PRINTF
		uint32_t *push = (ret_lr == 0xFFFFFFFD) ? user_sp : (regs + 8), *sp = push + 8;
		unsigned i;
		
		prRaw("============ HARD FAULT ============\n");
		prRaw("R0  = 0x%08X    R8  = 0x%08X\n", (unsigned)push[0], (unsigned)regs[0]);
		prRaw("R1  = 0x%08X    R9  = 0x%08X\n", (unsigned)push[1], (unsigned)regs[1]);
		prRaw("R2  = 0x%08X    R10 = 0x%08X\n", (unsigned)push[2], (unsigned)regs[2]);
		prRaw("R3  = 0x%08X    R11 = 0x%08X\n", (unsigned)push[3], (unsigned)regs[3]);
		prRaw("R4  = 0x%08X    R12 = 0x%08X\n", (unsigned)regs[4], (unsigned)push[4]);
		prRaw("R5  = 0x%08X    SP  = 0x%08X\n", (unsigned)regs[5], (unsigned)sp);
		prRaw("R6  = 0x%08X    LR  = 0x%08X\n", (unsigned)regs[6], (unsigned)push[5]);
		prRaw("R7  = 0x%08X    PC  = 0x%08X\n", (unsigned)regs[7], (unsigned)push[6]);
		prRaw("RA  = 0x%08X    SR  = 0x%08X\n", (unsigned)ret_lr,  (unsigned)push[7]);
		//		prRaw("SHCSR = 0x%08X\n", SCB->SHCSR);
		#if defined(CPU_CM7)
	    	prRaw("CFSR  = 0x%08X    HFSR  = 0x%08X\n", SCB->CFSR, SCB->HFSR);
	    	prRaw("MMFAR = 0x%08X    BFAR  = 0x%08X\n", SCB->MMFAR, SCB->BFAR);
		#endif
	    
		prRaw("WORDS @ SP: \n");
		
		for (i = 0; i < 32; i++)
			prRaw("[sp, #0x%03x = 0x%08x] = 0x%08x\n", i * 4, (unsigned)&sp[i], (unsigned)sp[i]);
		
		prRaw("\n\n");
	#endif
	
	while(1);
}

#ifdef NO_EXTERNAL_HF_HANDLER
void __attribute__((noreturn, naked, noinline)) HardFault_Handler(void)
{
	asm volatile(
			"push {r4-r7}				\n\t"
			"mov  r0, r8				\n\t"
			"mov  r1, r9				\n\t"
			"mov  r2, r10				\n\t"
			"mov  r3, r11				\n\t"
			"push {r0-r3}				\n\t"
			"mov  r0, sp				\n\t"
			"mov  r1, lr				\n\t"
			"mrs  r2, PSP				\n\t"
			"bl   report_hard_fault		\n\t"
			:::"memory");
}
#endif
