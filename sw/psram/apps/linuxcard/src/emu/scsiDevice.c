/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "scsiPublic.h"
#include "scsiDevice.h"
#include "printf.h"
#include "sii.h"


#pragma GCC optimize ("Os")

#define VERBOSE					0

static bool scsiDevicePrvDeviceSelected(void *userData)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	
	if (dev->state == DeviceIdle) {
		
		dev->state = DeviceSelected;
		return true;
	}
	err_str("unexpected select in idle\r\n");
	return false;
}

static void scsiDevicePrvBusResetted(void *userData)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	
	dev->state = DeviceIdle;
	dev->dataOutLen = 0;
	//xxx: more?
}

static void scsiDevicePrvSendMsgIn(struct ScsiDevice *dev)
{
	dev->state = DeviceWaitForMessageIn;
	siiDevSetState(ScsiStateMsgIn);
	siiDevSetDB(*dev->dataOutPtr);
	siiDevSetReq(true);
}

static void scsiDevicePrvAtnState(void *userData, bool hi)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	
	if (dev->state == DeviceIdle) {
		
		err_str("unexpected atn in idle\r\n");
		return;
	}
	
	if (hi) {
		
		if (dev->state == DeviceIdle) {
			
			err_str("ATN going high while idle\r\n");
			return;
		}
		
		if (VERBOSE)
			err_str("ATN went high\r\n");
		//xxx: maybe do this elsewhere os we dont ruin state while finishing last byte of status or data or message in?
		dev->state = DeviceWaitForMessageOut;
		siiDevSetState(ScsiStateMsgOut);
		siiDevSetReq(true);
	}
	else {
		
		if (VERBOSE)
			err_str("scsi atn went low\r\r");
	}
	dev->curAtn = hi;
}

static void scsiDevicePrvHandleNextStep(struct ScsiDevice *dev, enum ScsiHlCmdResult step)
{
	uint_fast8_t ret;
	
	if (VERBOSE)
		err_str(" %% command next step %u\r\n", step);
		
	switch (step) {
		case ScsiHlCmdResultGoToDataIn:
			if (VERBOSE)
				err_str(" $$ data in\r\n");
			
			ret = *dev->dataOutPtr;
				
			dev->state = DeviceTxDataIn;
			if (VERBOSE)
				err_str(" $$ read providing first byte 0x%02x\r\n", ret);
			dev->dataOutIndex = 0;
			siiDevSetDB(ret);
			siiDevSetState(ScsiStateDataIn);
			siiDevSetReq(true);
			break;
			
		case ScsiHlCmdResultGoToDataOut:
			if (VERBOSE)
				err_str(" $$ data out\r\n");
			dev->state = DeviceRxDataOut;
			siiDevSetState(ScsiStateDataOut);
			siiDevSetReq(true);
			break;
		
		case ScsiHlCmdResultGoToStatus:
			if (dev->dataOutLen != 1)
				err_str("wrong length status data provided but going to status phase\r\n");
			else {
				
				ret = *dev->dataOutPtr;
				
				dev->state = DeviceTxStatus;
				if (VERBOSE)
					err_str(" $$ read providing first byte 0x%02x\r\n", ret);
				dev->dataOutIndex = 0;
				siiDevSetDB(ret);
				siiDevSetState(ScsiStateStatus);
				siiDevSetReq(true);
			}
			break;
	}
}

static void scsiDevicePrvDataByteOut(struct ScsiDevice *dev, uint8_t val)
{
	if (!dev->dataInPtr) {
		
		err_str(" %%%% data in without a buffer\r\n");
		while(1);
	}
	
	*(dev->dataInPtr)++ = val;
	dev->dataInIndex++;
	if (dev->dataInIndex != dev->dataInLen)
		siiDevSetReq(true);
	else {
		dev->dataInLen = 0;
		dev->dataInPtr = NULL;
		
		if (dev->curAtn) {	//unambiguously go to message out
			
			//currently atn high handler already moves us to right mode and raises req, so no more to do
		}
		else {
			
			scsiDevicePrvHandleNextStep(dev, dev->hlFuncs->ScsiHlXferDone(dev->hlUserData));
		}
	}
}

static uint_fast8_t scsiDevicePrvCmdLen(uint_fast8_t cmd)
{
	switch (cmd >> 5) {
		case 0:
			return 6;
		
		case 1:
		case 2:
			return 10;
		
		case 5:
			return 12;
		
		default:
			err_str("%% command byte 0x%02x unrecognized. no length info!\r\n", cmd);
			return 0;
	}
}

static void scsiDevicePrvCmdHandle(struct ScsiDevice *dev, uint32_t len)
{
	//interp command
	if (VERBOSE)
		err_str("%% complete command RXed\r\n");
	scsiDevicePrvHandleNextStep(dev, dev->hlFuncs->ScsiHlCmdRxed(dev->hlUserData, dev->cmd, len));
	
	dev->cmdLen = 0;
}

static void scsiDevicePrvCmdByteOut(struct ScsiDevice *dev, uint8_t val)
{
	if (!dev->cmdLen) {
		
		dev->cmd[0] = val;
		dev->cmdIdx = 1;
		
		dev->cmdLen = scsiDevicePrvCmdLen(val);
	}
	else	
		dev->cmd[dev->cmdIdx++] = val;
	
	if (dev->cmdIdx != dev->cmdLen)
		siiDevSetReq(true);
	else
		scsiDevicePrvCmdHandle(dev, dev->cmdLen);
}

void scsiDevicePrvHandleDmaedCmd(struct ScsiDevice *dev, uint32_t len)
{
	err_str("## dmaed cmd %u b\r\n", len);
	
	//no support for partial dma partial pio for commands
	scsiDeviceSetRxDataBuffer(dev, NULL, 0);	//in case we gave too much buffer it is still pending
	
	if (!len || len != (dev->cmdLen = scsiDevicePrvCmdLen(dev->cmd[0])))
		err_str("DMAed cmd inval\r\n");
	else
		scsiDevicePrvCmdHandle(dev, len);
}

void scsiDeviceSetDataToTx(struct ScsiDevice *dev, const void *data, uint32_t len)
{
	if (dev->dataOutLen && data && VERBOSE)
		err_str("data in data provided while previous in data pending\r\n");
	
	dev->dataOutPtr = data;
	dev->dataOutLen = len;
	dev->dataOutIndex = 0;
}

void scsiDeviceSetRxDataBuffer(struct ScsiDevice *dev, void *data, uint32_t len)
{
	if (dev->dataInLen && data && VERBOSE)
		err_str("data out buffer provided while previous out data pending\r\n");
	
	dev->dataInPtr = data;
	dev->dataInLen = len;
	dev->dataInIndex = 0;
}

static void scsiDisPrvRequestCommand(struct ScsiDevice *dev)
{
	if (VERBOSE)
		err_str("$$ requesting cmd\r\n");
	dev->cmdLen = 0;
	scsiDeviceSetRxDataBuffer(dev, dev->cmd, sizeof(dev->cmd));
	dev->state = DeviceRxCommand;
	siiDevSetState(ScsiStateCmd);
	siiDevSetReq(true);
}

static void scsiDevicePrvMsgByteOut(struct ScsiDevice *dev, uint8_t val)
{
	if (dev->curExtMsgOutLen) {
		
		if (dev->curExtMsgOutLen == 0xffff) {
			
			dev->curExtMsgOutLen = val ? val : 0x100;
			dev->curExtMsgOutIdx = 0;
			siiDevSetReq(true);	//request the next msg byte
		}
		else {
			
			if (!dev->curExtMsgOutIdx) {
				
				dev->curExtMsgOutTyp = val;
				if (VERBOSE)
					err_str(" %%%% ext msg 0x%02x with len %u\r\n", val, dev->curExtMsgOutLen);
				siiDevSetReq(true);	//request the next msg byte
			}
			else {
				
				if (dev->curExtMsgOutTyp == SCSI_MSG_OFFSET_INTRLCK_DATA_XFER_REQ && dev->curExtMsgOutLen == 3) {
					
					dev->syncAgreement[dev->curExtMsgOutIdx - 1] = val;
				}
				
				if (VERBOSE)
					err_str(" %%%% ext msg 0x%02x byte %u = %02x\r\n", dev->curExtMsgOutTyp, dev->curExtMsgOutIdx, val);
			}
			dev->curExtMsgOutIdx++;
			if (dev->curExtMsgOutIdx == dev->curExtMsgOutLen) {	//msg over
				
				if (VERBOSE)
					err_str(" %%%% ext msg done\r\n");
				
				dev->curExtMsgOutLen = 0;
				
				if (!dev->curAtn && dev->curExtMsgOutTyp == SCSI_MSG_OFFSET_INTRLCK_DATA_XFER_REQ) {
					
					//try to reply
					dev->curMsgIn[0] = SCSI_MSG_MULTIBYTE_START;
					dev->curMsgIn[1] = 3;
					dev->curMsgIn[2] = SCSI_MSG_OFFSET_INTRLCK_DATA_XFER_REQ;
					dev->curMsgIn[3] = dev->syncAgreement[0];
					dev->curMsgIn[4] = dev->syncAgreement[1];
					
					dev->dataOutPtr = dev->curMsgIn;
					dev->dataOutLen = 5;
					dev->dataOutIndex = 0;
					dev->busFreeAfterMsgIn = false;
					scsiDevicePrvSendMsgIn(dev);
					return;
				}
				
			}
			else {
				
				siiDevSetReq(true);	//request the next msg byte
			}
		}
	}
	else if (val == SCSI_MSG_MULTIBYTE_START) {
		
		dev->curExtMsgOutLen = 0xffff;
	}
	else if (SCSI_MSG_IS_IDENTIFY(val)) {
		
		uint8_t lun = SCSI_IDENT_LUN(val);
		
		if (VERBOSE)
			err_str(" ** got identify for LUN %u, reconnect %ssupported\r\n", lun, SCSI_IDENT_SUPPORTS_RECONNECT(val) ? "": "not ");
		
		dev->hlFuncs->ScsiHlSetLun(dev->hlUserData, lun);
	}
	else if (val == SCSI_MSG_ABORT) {
		
		err_str(" ** got abort\r\n");
		siiDevSetState(ScsiStateFree);
	}
	else {
		
		err_str("unexpected message 0x%02x\r\n", val);
	}
	
	if (!dev->curAtn)
		scsiDisPrvRequestCommand(dev);
}

static void scsiDevicePrvDataInDone(struct ScsiDevice *dev)
{
	dev->dataOutLen = 0;
	dev->dataOutPtr = NULL;
	
	if (dev->curAtn) {	//unambiguously go to message out
		
		//currently atn high handler already moves us to right mode and raises req, so no more to do
	}
	else {
	
		switch (dev->state) {
			
			case DeviceTxStatus:
				if (VERBOSE)
					err_str(" $$ sending message IN as command is done\r\n");
				dev->curMsgIn[0] = SCSI_MSG_CMD_COMPLETED;
				
				dev->dataOutPtr = dev->curMsgIn;
				dev->dataOutLen = 1;
				dev->dataOutIndex = 0;
				dev->busFreeAfterMsgIn = true;
				scsiDevicePrvSendMsgIn(dev);
				break;
			
			case DeviceWaitForMessageIn:
				if (dev->busFreeAfterMsgIn) {
					if (VERBOSE)
						err_str(" $$ going bus free as message in is done\r\n");
					
					dev->state = DeviceIdle;
					siiDevSetState(ScsiStateFree);
				}
				else {
				
					if (VERBOSE)
						err_str(" $$ requesting command as message in is done\r\n");
					scsiDisPrvRequestCommand(dev);
				}
				break;
			
			case DeviceTxDataIn:
				scsiDevicePrvHandleNextStep(dev, dev->hlFuncs->ScsiHlXferDone(dev->hlUserData));
				break;
				
			default:
				err_str(" $$ what to do now in state %u ???\r\n", dev->state);
				break;
		}
	}
}

static uint32_t scsiDevicePrvDmaIn(void *userData, uint_fast16_t dmaWordIdx, uint8_t *extraByteP, bool haveExtraByte, uint32_t maxBytes)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	uint32_t lenAvail = dev->dataOutLen - dev->dataOutIndex;
	
	siiDevSetReq(false);
	
	if (VERBOSE)
		err_str(" %%%% DMA IN REQ for %u, have %u\r\n", maxBytes, lenAvail);
	
	if (lenAvail) {
		
		uint32_t numDone;
		
		if (maxBytes > lenAvail)
			maxBytes = lenAvail;
		
		numDone = maxBytes;
		
		if (haveExtraByte) {
			
			siiPrvBufferWrite(dmaWordIdx++, (*extraByteP) + (((uint_fast16_t)*dev->dataOutPtr) << 8));
			dev->dataOutPtr++;
			dev->dataOutIndex++;
			maxBytes--;
			lenAvail--;
		}
		while (maxBytes >= 2) {
			
			siiPrvBufferWrite(dmaWordIdx++, (((uint_fast16_t)dev->dataOutPtr[1]) << 8) + dev->dataOutPtr[0]);
			dev->dataOutPtr += 2;
			dev->dataOutIndex += 2;
			maxBytes -= 2;
			lenAvail -= 2;
		}
		
		if (maxBytes) {	//need to provide extra byte out to dma
			
			*extraByteP = *dev->dataOutPtr;
			dev->dataOutPtr++;
			dev->dataOutIndex++;
			maxBytes--;
			lenAvail--;
		}
		
		if (lenAvail) {
		
			siiDevSetDB(*dev->dataOutPtr);
			siiDevSetReq(true);
		}
		else{
		
			scsiDevicePrvDataInDone(dev);
		}
		
		return numDone;
	}
	else {	//done
	
		//we do not expect to get here with no bytes avail, but if we do, say we did zero bytes
		
		err_str("DMA IN with no bytes left!\r\n");
		while(1);
		
		return 0;
	}
}

static uint32_t scsiDevicePrvDmaOut(void *userData, uint_fast16_t dmaWordIdx, uint8_t *extraByteP, bool haveExtraByte, uint32_t numBytes)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	uint32_t spaceAvail = dev->dataInLen - dev->dataInIndex;
	
	siiDevSetReq(false);
	
	if (VERBOSE)
		err_str(" %%%% DMA OUT REQ for %u, have %u bytes of space\r\n", numBytes, spaceAvail);
	
	if (spaceAvail) {
		
		uint32_t numDone;
		
		if (numBytes > spaceAvail)
			numBytes = spaceAvail;
		
		numDone = numBytes;
		
		if (haveExtraByte) {
			
			*dev->dataInPtr = *extraByteP;
			dev->dataInPtr++;
			dev->dataInIndex++;
			spaceAvail--;
			numBytes--;
		}
		while (numBytes >= 2) {
			
			uint_fast16_t v = siiPrvBufferRead(dmaWordIdx++);
			
			dev->dataInPtr[0] = v;
			dev->dataInPtr[1] = v >> 8;
			dev->dataInPtr += 2;
			dev->dataInIndex += 2;
			spaceAvail -= 2;
			numBytes -= 2;
		}
		if (numBytes) {		//space byte
			
			uint_fast16_t v = siiPrvBufferRead(dmaWordIdx++);
			
			dev->dataInPtr[0] = v;
			*extraByteP = v >> 8;
			dev->dataInPtr++;
			dev->dataInIndex++;
			spaceAvail--;
			numBytes--;
		}
		
		if (dev->state == DeviceRxCommand) {
			
			scsiDevicePrvHandleDmaedCmd(dev, dev->dataInIndex);
		}
		else if (spaceAvail) {
		
			siiDevSetReq(true);
		}
		else{
			
			dev->dataInLen = 0;
			dev->dataInPtr = NULL;
			
			scsiDevicePrvHandleNextStep(dev, dev->hlFuncs->ScsiHlXferDone(dev->hlUserData));
		}
		
		return numDone;
	}
	else {	//done
	
		//we do not expect to get here with no bytes avail, but if we do, say we did zero bytes
		
		err_str("DMA OUT with no space left!\r\n");
		while(1);
		
		return 0;
	}
}

static void scsiDevicePrvByteInConsumed(void *userData)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	
	siiDevSetReq(false);
	
	dev->dataOutIndex++;
	
	//if we have more to say, say it
	if (dev->dataOutLen != dev->dataOutIndex) {
		
		uint8_t ret = *++(dev->dataOutPtr);
		siiDevSetDB(ret);
		siiDevSetReq(true);
		if (VERBOSE)
			err_str(" $$ read providing next byte 0x%02x\r\n", ret);
	}
	else {	//done
	
		scsiDevicePrvDataInDone(dev);
	}
}


static void scsiDevicePrvByteOut(void *userData, uint8_t val)
{
	struct ScsiDevice *dev = (struct ScsiDevice*)userData;
	
	siiDevSetReq(false);
	switch (dev->state) {
		case DeviceWaitForMessageOut:
			scsiDevicePrvMsgByteOut(dev, val);
			break;
		
		case DeviceRxCommand:
			scsiDevicePrvCmdByteOut(dev, val);
			break;
		
		case DeviceRxDataOut:
			scsiDevicePrvDataByteOut(dev, val);
			break;
		
		default:
			err_str(" %% unexpected byte out 0x%02x\n", val);
			break;
	}
}	

bool scsiDeviceInit(struct ScsiDevice *dev, uint_fast8_t scsiId, const struct ScsiHlFuncs *funcs, void *userData)
{
	static const struct ScsiDeviceFuncs diskDev = {
		.ScsiBusResetted = scsiDevicePrvBusResetted,
		.ScsiDeviceSelected = scsiDevicePrvDeviceSelected,
		.ScsiDeviceAtnState = scsiDevicePrvAtnState,
		.ScsiDeviceByteOut = scsiDevicePrvByteOut,
		.ScsiDeviceByteInConsumed = scsiDevicePrvByteInConsumed,
		.ScsiDeviceDmaIn = scsiDevicePrvDmaIn,
		.ScsiDeviceDmaOut = scsiDevicePrvDmaOut,
	};
	
	dev->hlFuncs = funcs;
	dev->hlUserData = userData;
	
	return siiDeviceAdd(scsiId, &diskDev, dev);
}


//XXX: SII DMA address is not incremented as DMA happens


//http://web.mit.edu/~linux/redhat/redhat-4.0.0/i386/doc/HOWTO/SCSI-Programming-HOWTO

