/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "printf.h"
#include "mem.h"
#include "soc.h"
#include "sii.h"


#pragma GCC optimize ("Os")

#define VERY_VERBOSE			0	//log every regiseter access
#define VERBOSE					0

#define REG_ADDR_SDB				0x00
#define REG_ADDR_SC1				0x04
	#define REG_SC1_InO				0x0001
	#define REG_SC1_CnD				0x0002
	#define REG_SC1_MSG				0x0004
	#define REG_SC1_ATN				0x0008
	#define REG_SC1_REQ				0x0010
	#define REG_SC1_ACK				0x0020
	#define REG_SC1_RST				0x0040
	#define REG_SC1_SEL				0x0080
	#define REG_SC1_BSY				0x0100
	
#define REG_ADDR_SC2				0x08
#define REG_ADDR_CSR				0x0C
	#define REG_CSR_IE				0x0001
	#define REG_CSR_PCE				0x0002
	#define REG_CSR_SLE				0x0004
	#define REG_CSR_RSE				0x0008
	#define REG_CSR_HPM				0x0010
#define REG_ADDR_ID					0x10
	#define REG_ID_IO				0x8000
	#define REG_ID_IDmask			0x0007
	#define REG_ID_IDshift			0
#define REG_ADDR_SLCSR				0x14
	#define REG_SLCSR_BUSIDmask		0x0007
	#define REG_SLCSR_BUSIDshift	0
#define REG_ADDR_DESTAT				0x18
#define REG_ADDR_DSTMO				0x1c
#define REG_ADDR_DATA				0x20
#define REG_ADDR_DMCTRL				0x24
	#define REG_DMCTRL_RAOmask		0x0003
	#define REG_DMCTRL_RAOshift		0
#define REG_ADDR_DMLOTC				0x28
	#define REG_DMLOTC_COUNTmask	0x3fff	//i know docs say 0x1fff, but ultrix writs 0x2000 here
	#define REG_DMLOTC_COUNTshift	0
#define REG_ADDR_DMADDRL			0x2c
#define REG_ADDR_DMADDRH			0x30
#define REG_ADDR_DMABYTE			0x34
#define REG_ADDR_STLP				0x38
#define REG_ADDR_LTLP				0x3c
#define REG_ADDR_ILP				0x40
#define REG_ADDR_DSCTRL				0x44
#define REG_ADDR_CSTAT				0x48
	#define REG_CSTAT_CI			0x8000
	#define REG_CSTAT_DI			0x4000
	#define REG_CSTAT_RST			0x2000
	#define REG_CSTAT_BER			0x1000
	#define REG_CSTAT_SCH			0x0080
	#define REG_CSTAT_CON			0x0040
	#define REG_CSTAT_DST			0x0020
	#define REG_CSTAT_TGT			0x0010
	#define REG_CSTAT_SWA			0x0008
	#define REG_CSTAT_SIP			0x0004
	#define REG_CSTAT_LST			0x0002
#define REG_ADDR_DSTAT				0x4c
	#define REG_DSTAT_CI			0x8000
	#define REG_DSTAT_DI			0x4000
	#define REG_DSTAT_DNE			0x2000
	#define REG_DSTAT_TCZ			0x1000
	#define REG_DSTAT_TBE			0x0800
	#define REG_DSTAT_IBF			0x0400
	#define REG_DSTAT_IPE			0x0200
	#define REG_DSTAT_OBB			0x0100
	#define REG_DSTAT_MIS			0x0010
	#define REG_DSTAT_ATN			0x0008
	#define REG_DSTAT_MSG			0x0004
	#define REG_DSTAT_CnD			0x0002
	#define REG_DSTAT_InO			0x0001
#define REG_ADDR_COMM				0x50
	#define REG_COMM_DMA			0x8000
	#define REG_COMM_RST			0x4000
	#define REG_COMM_RSL			0x1000
	#define REG_COMM_CMDmask		0x0f80
	#define REG_COMM_CMDshift		7
	#define REG_COMM_CON			0x0040
	#define REG_COMM_ORI			0x0020
	#define REG_COMM_TGT			0x0010
	#define REG_COMM_ATN			0x0008
	#define REG_COMM_MSG			0x0004
	#define REG_COMM_CnD			0x0002
	#define REG_COMM_InO			0x0001
#define REG_ADDR_DICTRL				0x54
	#define REG_DICTRL_PRE			0x0004

struct ScsiDeviceStruct {
	const struct ScsiDeviceFuncs *funcs;
	void *userData;
};

struct Sii {
	uint32_t id				: 3;
	uint32_t csr			: 5;
	uint32_t portEn			: 1;
	uint32_t slcsr			: 3;
	uint32_t scsiState		: 4;
		
	uint32_t dmctrl			: 2;
	
	//from outside
	uint32_t haveReq		: 1;
	
	//cstat & dstat summaries
	uint32_t ci				: 1;
	uint32_t di				: 1;
	
	//cstat
	uint32_t csRst			: 1;
//our bus is error-free - no BER bit
	uint32_t csSch			: 1;
	uint32_t csCon			: 1;
	uint32_t csTgt			: 1;		//may not need this, if my assumptions are right and doc is vague
	uint32_t csSwa			: 1;
	uint32_t csSip			: 1;
//we never lose arbitrarion so no need for LST bit

	//dstat
	uint32_t dsDne			: 1;
//TCZ is readable at dmaLotc
	uint32_t dsTbe			: 1;		//cleared when ....p.30
	uint32_t dsIbf			: 1;		//cleared when ....p.30
//we have no parity errors, so no IPE ever
	uint32_t dsObb			: 1;
	uint32_t dsMis			: 1;		//clered by modifying COMM, p.30
//we do not implement target mode, so ATN is not needed
	
	uint16_t comm;
	
	uint8_t dmaByte;
	uint16_t dmaLotc;
	uint32_t dmaAddr;
	
	//for PIO
	uint8_t txByte;
	uint8_t rxByte;
	uint8_t busByte;	//we fake this rather poorly
	
	struct ScsiDeviceStruct *curDev;
	//must be last
	struct ScsiDeviceStruct devs[NUM_SCSI_DEVICES];
};

static struct Sii mSii;


//see page 23 in spec




static void siiPrvRecalcIrqs(void)
{
	cpuIrq(SOC_IRQNO_SCSI, (mSii.csr & REG_CSR_IE) && (mSii.ci || mSii.di));
}

static void siiPrvRecalcCi(void)	//CSTAT changed
{
	mSii.ci = mSii.csRst || mSii.csSch;
	siiPrvRecalcIrqs();
}

static void siiPrvRecalcDi(void)	//DSTAT changed
{
	mSii.di = mSii.dsDne || mSii.dsTbe || mSii.dsIbf || mSii.dsMis;
	siiPrvRecalcIrqs();
}

static void siiPrvRecalcDsMisAndTbe(void)
{
	bool tbe = false, ibf = false, mis = false;
	
	//what they would be if we had data
	tbe = !(mSii.comm & REG_COMM_InO);
	ibf = !!(mSii.comm & REG_COMM_InO);
	
	if (!mSii.haveReq) {
		
		mis = false;
		tbe = false;
		ibf = false;
	}
	else if (((mSii.comm & (REG_COMM_MSG | REG_COMM_CnD | REG_COMM_InO)) | 0b1000) != mSii.scsiState) {
		
		mis = true;
		//tbe an dibf already set
	}
	else {	//actal data request!
		
		mis = false;
		//tbe and ibf already set
		
		
		//XXX: if we have DMA enabled, we do not need to set TBE/IBF...
	}
	
	mSii.dsMis = mis;
	mSii.dsTbe = tbe;
	mSii.dsIbf = ibf;
}

static void siiPrvBusReset(void)
{
	uint_fast8_t i;
	
	
	for (i = 0; i < NUM_SCSI_DEVICES; i++) {
		
		if (mSii.devs[i].funcs && mSii.devs[i].funcs->ScsiBusResetted)
			mSii.devs[i].funcs->ScsiBusResetted(mSii.devs[i].userData);
	}
	
	mSii.scsiState = ScsiStateFree;
	mSii.csSip = 0;	//selection not in progress
	mSii.csSch = 1;
	mSii.csRst = 1;
//	mSii.portEn = 0;
	mSii.curDev = NULL;
	
	siiPrvRecalcCi();
}

static void siiPrvHardReset(void)
{
	memset(&mSii, 0, sizeof(mSii) - sizeof(mSii.devs));
	siiPrvRecalcIrqs();
}

static void siiPrvSelect(void)
{
	if (mSii.scsiState != ScsiStateFree) {
	
		err_str("attempt to select while not in bus free\r\n");
	}
	else if (!mSii.devs[mSii.slcsr].funcs) {
	
		mSii.curDev = NULL;
		mSii.csSip = 1;	//selection stays in progress forever
		err_str("attempt to select noexistent device %u\r\n", mSii.slcsr);
	}
	else if (!mSii.devs[mSii.slcsr].funcs->ScsiDeviceSelected(mSii.devs[mSii.slcsr].userData)) {
		
		mSii.curDev = NULL;
		mSii.csSip = 1;	//selection stays in progress forever
		err_str("device %u refused to be selected\r\n", mSii.slcsr);
	}
	else {
		
		mSii.scsiState = ScsiStateConnected;
		mSii.csSip = 0;	//not in progress
		mSii.csSch = 1;
		mSii.csCon = 1;
		mSii.curDev = mSii.devs + mSii.slcsr;
		if (VERBOSE)
			err_str("selected device %u\r\n", mSii.slcsr);
	}
	siiPrvRecalcCi();
}

static bool siiPrvInfoXfer(void)
{
	if (mSii.scsiState < 0b1000) {
		
		err_str("xfer attempt in bad state\r\n");
	}
	else if (!mSii.haveReq) {
		
		err_str("out xfer without req\r\n");
		return true;
	}
	else if (mSii.scsiState & 1) {	//in
		
		if (VERBOSE)
			err_str("XFER IN (%s) in state %u\r\n", (mSii.comm & REG_COMM_DMA) ? "DMA" : "PIO", mSii.scsiState);
		
		if (mSii.scsiState == ScsiStateStatus || mSii.scsiState == ScsiStateMsgIn ||  mSii.scsiState == ScsiStateDataIn) {
			
			if (mSii.comm & REG_COMM_DMA) {
				
				uint32_t numBytesDone = 0, numBytesWanted;
				uint_fast8_t curPhase, haveSpareByte = 0;
				uint_fast16_t dmaWordIdx;
				
				mSii.dsObb = 0;	//cleared by starting DMA
				
				if (mSii.dmaAddr >= SII_BUFFER_SIZE || SII_BUFFER_SIZE - mSii.dmaAddr < mSii.dmaLotc) {
					
					err_str("DMA misprogrammed\r\n");
					return false;
				}
				dmaWordIdx = mSii.dmaAddr / 2;
				numBytesWanted = mSii.dmaLotc;
				
				curPhase = mSii.scsiState;
				if (mSii.dmaAddr & 1)	//need to incorporate the odd byte
					haveSpareByte = 1;
				while (numBytesWanted != numBytesDone && curPhase == mSii.scsiState) {
					
					uint32_t now;
					
					if (VERBOSE)
						err_str(" ^^^ trying to DMA %u in bytes\r\n", numBytesWanted - numBytesDone);
					now = mSii.curDev->funcs->ScsiDeviceDmaIn(mSii.curDev->userData, dmaWordIdx, &mSii.dmaByte, haveSpareByte, numBytesWanted - numBytesDone);
					numBytesDone += now;
					
					dmaWordIdx += (now + haveSpareByte) / 2;
					haveSpareByte = (now + haveSpareByte) % 2;
					
					if (VERBOSE)
						err_str(" ^^^ did %u / %u bytes of DMA\r\n", numBytesDone, numBytesWanted);
				}
				
				if (haveSpareByte) {	//odd byte goes to DMABYTE
					
					mSii.dsObb = 1;
					//i checked - if dma ends with an odd number of bytes,
					// the last byte will be read out of DMABYTE, but the word
					// of buffer that would contain it will also be read (but ignored)
					
					//mSii.dmaByte is already set
					if (VERBOSE)
						err_str(" ^^^ odd byte exported\r\n");
				}
				
				mSii.dmaLotc -= numBytesDone;
				mSii.dsDne = true;
				siiPrvRecalcDi();
				
				return true;
			}
			else {	//PIO
			
				mSii.rxByte = mSii.busByte;
				
				//XXX: dne? clear command
				mSii.dsDne = true;
				siiPrvRecalcDi();
				
				mSii.curDev->funcs->ScsiDeviceByteInConsumed(mSii.curDev->userData);
				
				return true;
			}
		}
		else {
			err_str("XXX in xfer in this state unsupported\r\n");
		}
	}
	else {							//out
		
		if (VERBOSE)
			err_str("XFER OUT (%s) in state %u\r\n", (mSii.comm & REG_COMM_DMA) ? "DMA" : "PIO", mSii.scsiState);
		
		if (mSii.scsiState == ScsiStateMsgOut || mSii.scsiState == ScsiStateCmd || mSii.scsiState == ScsiStateDataOut) {
			
			if (mSii.comm & REG_COMM_DMA) {
				
				uint32_t numBytesDone = 0, numBytesWanted;
				uint_fast8_t curPhase, haveSpareByte = 0;
				uint_fast16_t dmaWordIdx;
				
				mSii.dsObb = 0;	//cleared by starting DMA
				
				if (mSii.dmaAddr >= SII_BUFFER_SIZE || SII_BUFFER_SIZE - mSii.dmaAddr < mSii.dmaLotc) {
					
					err_str("DMA misprogrammed\r\n");
					return false;
				}
				dmaWordIdx = (mSii.dmaAddr + 1) / 2;	//presumably in this case it points to byte we need to send, already in mSii.dmaByte
				numBytesWanted = mSii.dmaLotc;
				
				curPhase = mSii.scsiState;
				if (mSii.dmaAddr & 1) {	//need to incorporate the odd byte
					
					if (VERBOSE)
						err_str(" ^^^ odd byte unexpected on OUT xfer start\r\n");
					
					haveSpareByte = 1;
				}
				while (numBytesWanted != numBytesDone && curPhase == mSii.scsiState) {
					
					uint32_t now;
					
					if (VERBOSE)
						err_str(" ^^^ trying to DMA out %u bytes\r\n", numBytesWanted - numBytesDone);
					now = mSii.curDev->funcs->ScsiDeviceDmaOut(mSii.curDev->userData, dmaWordIdx, &mSii.dmaByte, haveSpareByte, numBytesWanted - numBytesDone);
					numBytesDone += now;
					
					dmaWordIdx += (now + haveSpareByte) / 2;
					haveSpareByte = (now + haveSpareByte) % 2;
				
					if (VERBOSE)
						err_str(" ^^^ did %u / %u bytes of DMA\r\n", numBytesDone, numBytesWanted);
				}
				
				if (haveSpareByte) {	//odd byte goes to DMABYTE and we act like we XFERRED one more
					
					if (VERBOSE)
						err_str(" ^^^ odd byte unexpected on OUT xfer end\r\n");
					
					mSii.dsObb = 1;
					//mSii.dmaByte is already set
				}
				
				mSii.dmaLotc -= numBytesDone;
				mSii.dsDne = true;
				siiPrvRecalcDi();
				
				return true;
				
			}
			else {	//PIO
			
				mSii.curDev->funcs->ScsiDeviceByteOut(mSii.curDev->userData, mSii.txByte);
				
				//XXX: dne? clear command
				mSii.dsDne = true;
				mSii.comm &=~ REG_COMM_CMDmask;
				siiPrvRecalcDi();
				
				return true;
			}
		}
		else {
			
			err_str("XXX out xfer in this stat eunsupported\r\n");
		}
	}
	
	return false;
}

static bool siiPrvMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	uint16_t *vP = (uint16_t*)buf;
	uint_fast16_t v;
	bool ret = false;
		
	//buffer ram starts at offset 0x01000000
	
	pa &= 0x01ffffff;
	if (pa == 0 && size == 4 && !write) {
		//netBSD probes for device this way
		*(uint32_t*)buf = 0;
		return true;
	}
	
	if ((pa & 3) || size != 2) {
		
		err_str("invalid SII access mode: %u @ 0x%04x\n", size, pa);
		return false;
	}
	v = *vP;
	
	if (pa & 0x01000000) {
		
		pa &= 0x00ffffff;
		pa /= 2;
		
		if (pa >= SII_BUFFER_SIZE)
			return false;
		
		if (write) {
			
			//assume host is LE - i am lazy, so sue me
			siiPrvBufferWrite(pa / sizeof(uint16_t), v);
			if (VERY_VERBOSE)
				err_str("SIIRAM[0x%04x] <- 0x%04x\r\n", pa / 2, *vP);
		}
		else {
			
			*vP = siiPrvBufferRead(pa / sizeof(uint16_t));
			if (VERY_VERBOSE)
				err_str("SIIRAM[0x%04x] -> 0x%04x\r\n", pa / 2, *vP);
		}
		
		return true;
	}
	else {
		
		pa /= 4;
		
		switch (pa) {
			
			case REG_ADDR_SDB / 4:
				if (!write) {
					*vP = mSii.busByte;
					ret = true;
				}
				break;
			
			case REG_ADDR_SC1 / 4:
				if (write)
					break;
				ret = true;
				switch (mSii.scsiState) {
					case ScsiStateFree:
						v = 0;
						break;
					
					case ScsiStateArb:
						v = REG_SC1_BSY;
						break;
					
					case ScsiStateBusAlloced:
						v = REG_SC1_SEL;	//we always win arbitration
						break;
					
					case ScsiStateSel:
						v = REG_SC1_SEL;
						break;
					
					case ScsiStateResel:
						v = REG_SC1_SEL | REG_SC1_InO;
						break;
					
					case ScsiStateConnected:
						v = REG_SC1_BSY;
						break;
					
					case ScsiStateDataOut:
						v = REG_SC1_BSY;
						break;
					
					case ScsiStateDataIn:
						v = REG_SC1_BSY | REG_SC1_InO;
						break;
					
					case ScsiStateCmd:
						v = REG_SC1_BSY | REG_SC1_CnD;
						break;
					
					case ScsiStateStatus:
						v = REG_SC1_BSY | REG_SC1_CnD | REG_SC1_InO;
						break;
					
					case ScsiStateMsgOut:
						v = REG_SC1_BSY | REG_SC1_MSG | REG_SC1_CnD;
						break;
					
					case ScsiStateMsgIn:
						v = REG_SC1_BSY | REG_SC1_MSG | REG_SC1_CnD | REG_SC1_InO;
						break;
				}
				if (mSii.comm & REG_COMM_ATN)
					v |= REG_SC1_ATN;
				if (mSii.haveReq)
					v |= REG_SC1_REQ;
				*vP = v;
				break;
				
			case REG_ADDR_CSR / 4:
				if (write) {
					
					mSii.csr = v;
					siiPrvRecalcIrqs();
				}
				else
					*vP = mSii.csr;
				ret = true;
				break;
			
			case REG_ADDR_ID / 4:
				if (write) {
					if (v & REG_ID_IO)
						mSii.id = (v & REG_ID_IDmask) >> REG_ID_IDshift;
				}
				else
					*vP = (((uint_fast16_t)mSii.id) << REG_ID_IDshift) & REG_ID_IDmask;
				ret = true;
				break;
		
			case REG_ADDR_SLCSR / 4:
				if (write)
					mSii.slcsr = (v & REG_SLCSR_BUSIDmask) >> REG_SLCSR_BUSIDshift;
				else
					*vP = (((uint_fast16_t)mSii.slcsr) << REG_SLCSR_BUSIDshift) & REG_SLCSR_BUSIDmask;
				ret = true;
				break;
					
			case REG_ADDR_DESTAT / 4:
				break;
	
			case REG_ADDR_DATA / 4:
				if (write)
					mSii.busByte = mSii.txByte = v;
				else
					*vP = mSii.rxByte;
				ret = true;
				break;
					
			case REG_ADDR_DMCTRL / 4:
				if (write)
					mSii.dmctrl = (v & REG_DMCTRL_RAOmask) >> REG_DMCTRL_RAOshift;
				else
					*vP = (((uint_fast16_t)mSii.dmctrl) << REG_DMCTRL_RAOshift) & REG_DMCTRL_RAOmask;
				ret = true;
				break;
					
			case REG_ADDR_DMLOTC / 4:
				if (write)
					mSii.dmaLotc = (v & REG_DMLOTC_COUNTmask) >> REG_DMLOTC_COUNTshift;
				else
					*vP = (((uint_fast16_t)mSii.dmaLotc) << REG_DMLOTC_COUNTshift) & REG_DMLOTC_COUNTmask;
				ret = true;
				break;
				
			case REG_ADDR_DMADDRL / 4:
				if (write)
					mSii.dmaAddr = (mSii.dmaAddr &~ 0xffff) | v;
				else
					*vP = mSii.dmaAddr;
				ret = true;
				break;
					
			case REG_ADDR_DMADDRH / 4:
				if (write)
					mSii.dmaAddr = (mSii.dmaAddr & 0xffff) | (((uint32_t)v) << 16);
				else
					*vP = mSii.dmaAddr >> 16;
				ret = true;
				break;
					
			case REG_ADDR_DMABYTE / 4:
				if (write)
					mSii.dmaByte = v;
				else
					*vP =mSii.dmaByte;
				ret = true;
				break;
	
			case REG_ADDR_CSTAT / 4:
				if (write) {
					if (v & REG_CSTAT_RST)
						mSii.csRst = 0;
					if (v & REG_CSTAT_SCH)
						mSii.csSch = 0;
					siiPrvRecalcCi();
				}
				else {
					
					v = 0;
					if (mSii.ci)
						v += REG_CSTAT_CI;
					if (mSii.di)
						v += REG_CSTAT_DI;
					if (mSii.csRst)
						v += REG_CSTAT_RST;
					if (mSii.csSch)
						v += REG_CSTAT_SCH;
					if (mSii.csCon)
						v += REG_CSTAT_CON;
					if (mSii.csTgt)
						v += REG_CSTAT_TGT;
					if (mSii.csSwa)
						v += REG_CSTAT_SWA;
					if (mSii.csSip)
						v += REG_CSTAT_SIP;
					*vP = v;
				}
				ret = true;
				break;
				
			case REG_ADDR_DSTAT / 4:
				if (write) {
					if (v & REG_DSTAT_DNE)
						mSii.dsDne = 0;
					siiPrvRecalcDi();
				}
				else {
					v = 0;
					if (mSii.ci)
						v += REG_DSTAT_CI;
					if (mSii.di)
						v += REG_DSTAT_DI;
					if (mSii.dsDne)
						v += REG_DSTAT_DNE;
					if (!mSii.dmaLotc)
						v += REG_DSTAT_TCZ;
					if (mSii.dsTbe)
						v += REG_DSTAT_TBE;
					if (mSii.dsIbf)
						v += REG_DSTAT_IBF;
					if (mSii.dsObb)
						v += REG_DSTAT_OBB;
					if (mSii.dsMis)
						v += REG_DSTAT_MIS;
					
					if (mSii.scsiState >= 0b1000)
						v += (mSii.scsiState & (REG_DSTAT_MSG | REG_DSTAT_CnD | REG_DSTAT_InO));
					*vP = v;
				}
				ret = true;
				break;
					
			case REG_ADDR_COMM / 4:		///can set/clear dsMis...
				if (write) {
					
					bool atnChanged = !!((mSii.comm ^ v) & REG_COMM_ATN);
					
					mSii.comm = v;
					
					if (v & REG_COMM_RST) {
						err_str(" * SCSI reset\r\n");
						siiPrvBusReset();
						ret = true;
					}

					if (mSii.portEn) {		
						
						uint_fast8_t cmd = (v & REG_COMM_CMDmask) >> REG_COMM_CMDshift;
						
						if (VERBOSE)
							err_str(" comm cmd 0x%02x\r\n", (unsigned)cmd);
						
						if (cmd & 1) {	//chip reset
							
							siiPrvHardReset();
							cmd &=~ 1;
						}
						
						if (cmd & 2) {	//disconnect
							mSii.scsiState = ScsiStateFree;
							mSii.csSip = 0;	//selection not in progress
							mSii.csSch = 1;
							mSii.csCon = 0;
							mSii.curDev = NULL;
							siiPrvRecalcCi();
							cmd &=~ 2;
						}
						
						if (cmd & 8) {	//select
							siiPrvSelect();
							cmd &=~ 8;
						}
					
						if (atnChanged && mSii.curDev) {
							
							mSii.curDev->funcs->ScsiDeviceAtnState(mSii.curDev->userData, !!(v & REG_COMM_ATN));
							mSii.curDev->funcs->ScsiDeviceAtnState(mSii.curDev->userData, false);
						}
						
						if (cmd & 16) {	//information transfer
							(void)siiPrvInfoXfer();
							cmd &=~ 16;
						}
						
						if (cmd)
							err_str("unhandled command bit(s) 0x%x in cmd 0x%x\n", cmd, (unsigned)((v & REG_COMM_CMDmask) >> REG_COMM_CMDshift));
						else
							ret = true;
						
						siiPrvRecalcDsMisAndTbe();
					}
					else
						ret = true;
				}
				else {
					
					//reading com in initiator mode MUST replace lower 3 bits with current bus state
					*vP = (mSii.comm &~ (REG_COMM_MSG | REG_COMM_CnD | REG_COMM_InO)) | (mSii.scsiState >= 0b1000 ? mSii.scsiState - 0b1000 : 0);
					ret = true;
				}
				break;
				
			case REG_ADDR_DICTRL / 4:
				if (write) {
					if (v &~ REG_DICTRL_PRE)
						err_str("unknown bits set in DICTRL\n");
					mSii.portEn = !!(v & REG_DICTRL_PRE);
				}
				else
					*vP = mSii.portEn ? REG_DICTRL_PRE : 0;
				ret = true;
				break;
			
			case REG_ADDR_SC2 / 4:
				//diagnostic regs that shouldn't be needed
				break;
				
			case REG_ADDR_DSCTRL / 4:
			case REG_ADDR_DSTMO / 4:
			case REG_ADDR_STLP / 4:
			case REG_ADDR_LTLP / 4:
			case REG_ADDR_ILP / 4:
				//these regs documented as unusupported
				break;
			
			default:
				break;
		}
		
		if (VERY_VERBOSE) {
				
			static const char *mRegnames[] = {
				"SDB", "SC1", "SC2", "CSR",
				"ID", "SLCSR", "DESTAT", "DSTMO",
				"DATA", "DMCTRL", "DMLOTC", "DMADDRL",
				"DMADDRH", "DMABYTE", "STLP", "LTLP",
				"ILP", "DSCTRL", "CSTAT", "DSTAT",
				"COMM", "DICTRL",
			};
			
			if (write)
				err_str("%s SII reg W: [0x%04x, %7s] <- 0x%04x\r\n", ret ? "GOOD" : "BAD ", pa * 4, mRegnames[pa], (unsigned)v);
			else if (!ret)
				err_str("%s SII reg R: [0x%04x, %7s] -> ??????\r\n", "BAD ", pa * 4, mRegnames[pa]);
			else {
				static uint32_t prevAddr = 0, prevVal = 0;
				static bool anySkipped = false;
				
				if (pa != prevAddr || *(uint16_t*)vP != prevVal) {
					anySkipped = false;
					err_str("%s SII reg R: [0x%04x, %7s] -> 0x%04x\r\n", "GOOD", pa * 4, mRegnames[pa], *(uint16_t*)vP);
				}
				else if (!anySkipped) {
					anySkipped = true;
					err_str("***\r\n");
				}
				prevAddr = pa;
				prevVal = *(uint16_t*)vP;
			}
		}
	}
		
	return ret;
}

void siiDevSetDB(uint8_t val)
{
	mSii.busByte = val;
}

void siiDevSetState(enum ScsiState desiredState)
{
	if ((mSii.scsiState != ScsiStateConnected && mSii.scsiState < 0b1000) || !mSii.curDev) {
	
		err_str("unexpected device driving state in SCSI state %u\r\n", mSii.scsiState);
		while(1);
	}
	mSii.scsiState = desiredState;

	if (desiredState == ScsiStateFree && mSii.curDev) {	//device released bus
		
		mSii.csSch = 1;
		mSii.csCon = 0;
	}

	siiPrvRecalcCi();
}

void siiDevSetReq(bool set)
{
	if (set && mSii.haveReq) {
		
		//harmless
		if (VERBOSE)
			err_str("@@ setting req while already set\r\n");
	}

	if (VERBOSE)
		err_str("@@ setting req to %u\r\n", set);
	mSii.haveReq = set;
	siiPrvRecalcDsMisAndTbe();
}

//ultrix likes to select the controller itself...go figure
static bool siiPrvSelfSelected(void *userData)
{
	(void)userData;
	
	return false;
}

bool siiInit(uint_fast8_t ownDevId)
{
	static const struct ScsiDeviceFuncs siiOwnDevFuncs = {
		.ScsiDeviceSelected = siiPrvSelfSelected,
	};
	
	siiPrvHardReset();
	mSii.devs[ownDevId].funcs = &siiOwnDevFuncs;
	return memRegionAdd(0x1a000000, 0x02000000, siiPrvMemAccess);
}

bool siiDeviceAdd(uint_fast8_t devId, const struct ScsiDeviceFuncs *devFuncs, void *userData)
{
	if (devId >= NUM_SCSI_DEVICES || mSii.devs[devId].funcs)
		return false;
	
	mSii.devs[devId].funcs = devFuncs;
	mSii.devs[devId].userData = userData;
	
	return true;
}
