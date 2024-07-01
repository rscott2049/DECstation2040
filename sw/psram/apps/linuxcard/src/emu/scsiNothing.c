/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "scsiNothing.h"
#include "scsiPublic.h"
#include "scsiDevice.h"
#include "printf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#pragma GCC optimize ("Os")

#define VERBOSE		0


static void diskPrvScsiHlSetLun(void *userData, uint_fast8_t lun)
{
	(void)userData;
	(void)lun;
}

static bool diskPrvHandleInquiry(struct ScsiNothing *nothing, bool vitalData, uint_fast8_t pageCode, uint_fast16_t allocLen)
{
	//this MUST be done even for invalid LUNs
	if (pageCode == 0 && !vitalData) {
		
		static const uint8_t inquiryReply[5] = {0x7f /* nothign here */};
		uint_fast16_t myInquiryDataLen = sizeof(inquiryReply);
	
		if (myInquiryDataLen > allocLen)
			myInquiryDataLen = allocLen;
		
		scsiDeviceSetDataToTx(&nothing->scsiDevice, inquiryReply, myInquiryDataLen);
		return true;
	}

	return false;
}

static enum ScsiHlCmdResult diskPrvScsiHlCmdRxed(void *userData, const uint8_t *cmd, uint_fast8_t len)
{
	struct ScsiNothing *nothing = (struct ScsiNothing*)userData;
	enum ScsiHlCmdResult ret;
		
	if (VERBOSE) {
		
		uint_fast8_t i;
	
		err_str("DISK RXed %u byte cmd:", len);
		for (i = 0; i < len; i++)
			err_str(" %02x", cmd[i]);
		err_str("\r\n");
	}
	
	nothing->nextStatusOut = SCSI_STATUS_CHECK_CONDITION;
	ret = ScsiHlCmdResultGoToStatus;
	
	if (cmd[len - 1] & 3) {	//handle control byte's FLAG and LINK fields
		
		err_str(" ## flag or link bits seen\r\n");
	}
	else {
	
		switch (cmd[0]) {
			
			case SCSI_CMD_TEST_UNIT_READY:
				nothing->nextStatusOut = SCSI_STATUS_GOOD;
				ret = ScsiHlCmdResultGoToStatus;
				break;
			
			case SCSI_CMD_INQUIRY:
				if (diskPrvHandleInquiry(nothing, cmd[1] & 1, cmd[2], cmd[4] ? cmd[4] : 256)) {
					nothing->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			default:
				err_str(" ## disk not handling command 0x%02x\r\n", cmd[0]);
				while(1);
				break;
		}
	}
	
	if (ret == ScsiHlCmdResultGoToStatus)
		scsiDeviceSetDataToTx(&nothing->scsiDevice, &nothing->nextStatusOut, 1);

	if (VERBOSE)
		err_str(" ## disk replying with %u\r\n", ret);
	return ret;
}

static enum ScsiHlCmdResult diskPrvScsiHlXferDone(void *userData)
{
	struct ScsiNothing *nothing = (struct ScsiNothing*)userData;
	
	scsiDeviceSetDataToTx(&nothing->scsiDevice, &nothing->nextStatusOut, 1);
	return ScsiHlCmdResultGoToStatus;
}

bool scsiNothingInit(struct ScsiNothing *nothing, uint_fast8_t scsiId)
{
	static const struct ScsiHlFuncs diskFuncs = {
		.ScsiHlSetLun = diskPrvScsiHlSetLun,
		.ScsiHlCmdRxed = diskPrvScsiHlCmdRxed,
		.ScsiHlXferDone = diskPrvScsiHlXferDone,
	};
	
	return scsiDeviceInit(&nothing->scsiDevice, scsiId, &diskFuncs, nothing);
}
