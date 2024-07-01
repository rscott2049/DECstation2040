/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "scsiPublic.h"
#include "scsiDevice.h"
#include "scsiDisk.h"
#include "printf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#pragma GCC optimize ("Os")

#define VERBOSE		0

static bool diskPrvSignalInvalidLunIfNeeded(struct ScsiDisk *disk)	//return true if that was the case
{
	if (!disk->curLun)
		return false;
		
	disk->senseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
	disk->ASC = SCSI_ASC_Q_LUN_NOT_SUPPORTED;
	disk->nextStatusOut = SCSI_STATUS_CHECK_CONDITION;
		
	return true;
}

static void diskPrvScsiHlSetLun(void *userData, uint_fast8_t lun)
{
	struct ScsiDisk *disk = (struct ScsiDisk*)userData;
	
	disk->curLun = lun;
}

static bool diskPrvHandleInquiry(struct ScsiDisk *disk, bool vitalData, uint_fast8_t pageCode, uint_fast16_t allocLen)
{
	//this MUST be done even for invalid LUNs
	if (pageCode == 0 && !vitalData) {
		
		uint_fast16_t myInquiryDataLen = 5;
	
		//SCSI-II p 142, SCSI-I p 60
		if (disk->curLun) {	//no disk here
			
			disk->buffer[0] = 0x7f;
		}
#if CDROM_SUPORTED
		else if (disk->isCDROM) {
			
			disk->buffer[0] = 0x05;		//a CDROM is attached
			disk->buffer[1] = 0x80;		//removable
			disk->buffer[2] = 0x09;		//SCSI compatible
			disk->buffer[3] = 0x03;		//ATAPI ver
			disk->buffer[4] = 0x00;		//no additional length
		}
#endif
		else {
			
			disk->buffer[0] = 0x00;		//a random access disk that is attached
			disk->buffer[1] = 0x00;		//not removable
			disk->buffer[2] = 0x09;		//SCSI compatible
			disk->buffer[3] = 0x00;		//no features
			disk->buffer[4] = 0x00;		//no additional length
		}
		
		if (myInquiryDataLen > allocLen)
			myInquiryDataLen = allocLen;
		
		scsiDeviceSetDataToTx(&disk->scsiDevice, disk->buffer, myInquiryDataLen);
		return true;
	}
	
	if (diskPrvSignalInvalidLunIfNeeded(disk))
		return false;
	
	//invalid request to a valid LUN
	disk->senseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
	disk->ASC = SCSI_ASC_Q_INVALID_FIELD_IN_CDB;
	//disk->nextStatusOut will be set to SCSI_STATUS_CHECK_CONDITION already
	
	return false;
}

static uint32_t diskPrvGetActualSectorSize(struct ScsiDisk *disk)
{
	(void)disk;
	
	return 512;
}

static uint32_t diskPrvGetReportedSectorSize(struct ScsiDisk *disk)
{
	(void)disk;
	
	#if CDROM_SUPORTED
		if (disk->isCDROM)
			return 2048;
	#endif
	
	return 512;
}

//These are for fixed disks only and are from SBC-r08c.pdf
static uint8_t* dispPrvAppendModeSensePageHDD(struct ScsiDisk *disk, uint8_t *dst, uint_fast8_t pageCode)
{	
	uint_fast8_t pgLen;
	
	switch (pageCode) {
		case 1:		//read-write error recovery page
			memset(dst + 2, 0x00, pgLen = 10);
			break;
		
		case 3:		//format device page
			memset(dst + 2, 0x00, pgLen = 19);
			dst[10] = disk->geometry.secPerTrack >> 8;
			dst[11] = disk->geometry.secPerTrack & 0xff;
			dst[12] = diskPrvGetReportedSectorSize(disk) >> 8;
			dst[13] = diskPrvGetReportedSectorSize(disk) & 0xff;
			dst[20] = 0x40;	//hard sectors (reported for "default", 0x00 should be reported for "changeable")
			break;
		
		case 4:		//Rigid disk device geometry page
			memset(dst + 2, 0x00, pgLen = 7);	//we only need to provide 4 but ultrix has an off-by-one bug and demands to see at least 5 bytes, we cannot break mid-field so we have to do 7
			dst[2] = disk->geometry.numCyl >> 16;
			dst[3] = disk->geometry.numCyl >> 8;
			dst[4] = disk->geometry.numCyl & 0xff;
			dst[5] = disk->geometry.numHeads;
			//starting cylinder write precompensation
			dst[6] = disk->geometry.numCyl >> 16;
			dst[7] = disk->geometry.numCyl >> 8;
			dst[8] = disk->geometry.numCyl & 0xff;
			
			break;
		
		default:
			
			return NULL;
	}
	
	dst[0] = pageCode;
	dst[1] = pgLen;
	
	return dst + pgLen + 2;
}


#if CDROM_SUPORTED

	//from MMC-2 spec
	static uint8_t* dispPrvAppendModeSensePageCDROM(struct ScsiDisk *disk, uint8_t *dst, uint_fast8_t pageCode)
	{
		uint_fast8_t pgLen;
	
		(void)disk;
	
		switch (pageCode) {
			case 1:		//read-write error recovery page
				memset(dst + 2, 0x00, pgLen = 10);
				break;
			
			case 0x18:		//feature set
				memset(dst + 2, 0x00, pgLen = 22);
				break;
			
			case 0x2a:		//capabilities and mechanical status
				memset(dst + 2, 0x00, pgLen = 24);
				dst[2] = 0x3f;	//we read all
				dst[3] = 0x00;	//we write none
				dst[4] = 0x70;	//more things we support
				dst[5] = 0x00;	//not this stuff
				dst[6] = 0x20;	//tray-loading
				break;
			
			default:
				
				return NULL;
		}
		
		dst[0] = pageCode;
		dst[1] = pgLen;
		
		return dst + pgLen + 2;
	}

#endif


static uint8_t* dispPrvAppendModeSensePage(struct ScsiDisk *disk, uint8_t *dst, uint_fast8_t pageCode)
{
#if CDROM_SUPORTED
	if (disk->isCDROM)
		return dispPrvAppendModeSensePageCDROM(disk, dst, pageCode);
#endif
	return dispPrvAppendModeSensePageHDD(disk, dst, pageCode);
}

static bool diskPrvHandleModeSense(struct ScsiDisk *disk, bool dbd, uint_fast8_t pc, uint_fast8_t pageCode, uint_fast16_t allocLen)
{
	uint_fast16_t ourReplyLen;
	uint8_t *dst = disk->buffer, *t;
	uint_fast8_t i;
	
	
	dst++;			//skip total length - write it later
	*dst++ = 0x00;	//medium type is 0
	*dst++ = 0x00;	//device specific parameter: not write protected
	if (dbd) {
		
		*dst++ = 0x00;	//no block descriptor and thus their size is zero
	}
	else {				//one descriptor (ultrix expects precisely one)
		
		uint32_t diskBlockLength = diskPrvGetReportedSectorSize(disk);
		
		*dst++ = 0x08;	//one block descriptor's worth of bytes
		
		*dst++ = 0x00;	//default density of medium
		*dst++ = 0x00;	//all remaining blcoks are in this zone
		*dst++ = 0x00;
		*dst++ = 0x00;
		*dst++ = 0x00;	//RFU
		*dst++ = diskBlockLength >> 16;
		*dst++ = diskBlockLength >> 8;
		*dst++ = diskBlockLength & 0xff;
	}
	
	if (pc || pageCode) {	//SCSI-I compat
	
		if (pageCode != 0x3f) {
			
			t = dispPrvAppendModeSensePage(disk, dst, pageCode);
			if (!t) {	//no such page
				
				disk->senseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
				disk->ASC = SCSI_ASC_Q_INVALID_FIELD_IN_CDB;
				return false;
			}
			dst = t;
		}
		else {		//all pages
			
			for (i = 0; i < 0x3f; i++) {
				uint_fast8_t pageNo = (i == 0x3f) ? 0 : i;	//page 0 is always last
				
				t = dispPrvAppendModeSensePage(disk, dst, pageNo);		//this loop *assumes* that our total sense data is never more than 512 bytes :)
				if (t)
					dst = t;
			}
		}
	}
	ourReplyLen = dst - disk->buffer;
	disk->buffer[0] = ourReplyLen - 1;
	
	if (ourReplyLen > allocLen)
		ourReplyLen = allocLen;
	
	scsiDeviceSetDataToTx(&disk->scsiDevice, disk->buffer, ourReplyLen);
	return true;
}

static bool diskPrvHandleRequestSense(struct ScsiDisk *disk, uint_fast16_t allocLen)
{
	uint_fast16_t ourReplyLen = 14;
	
	memset(disk->buffer, 0, allocLen);
	
	disk->buffer[0] = 0xf0;
	disk->buffer[7] = ourReplyLen - 8;
	disk->buffer[12] = disk->ASC >> 8;
	disk->buffer[13] = disk->ASC & 0xff;
	
	if (ourReplyLen > allocLen)
		ourReplyLen = allocLen;
	
	scsiDeviceSetDataToTx(&disk->scsiDevice, disk->buffer, ourReplyLen);
	return true;
}

static bool diskPrvContinueMultiblockOp(struct ScsiDisk *disk)		//called BEFORE xferring each read sector to initiator, and AFTER xferring each write sector from target
{
	if (disk->multiblockState == MultiblockIdle) {
		
		err_str(" ### unexpected multi continue\r\n");
		while(1);
	}
	
	if (VERBOSE)
		err_str(" ### %sing disk sector %u, %u more left\r\n",
			disk->multiblockState == MultiblockRead ? "read" : "writ",
				disk->nextLba, disk->numLbasLeft - ((disk->multiblockState == MultiblockRead) ? 0 : 1));
	
	if (!disk->diskF(disk->multiblockState == MultiblockRead ? MASS_STORE_OP_READ : MASS_STORE_OP_WRITE, disk->nextLba++, disk->buffer)) {
		
		disk->multiblockState = MultiblockIdle;
		disk->senseKey = SCSI_SENSE_KEY_MEDIUM_ERROR;
		disk->ASC = SCSI_ASC_Q_INTERNAL_TARGET_ERROR;
		
		return false;
	}
	
	disk->numLbasLeft--;
	
	if (disk->multiblockState == MultiblockRead) {
		
		scsiDeviceSetDataToTx(&disk->scsiDevice, disk->buffer, diskPrvGetActualSectorSize(disk));
		if (!disk->numLbasLeft)
			disk->multiblockState = MultiblockIdle;
	}
	else if (disk->numLbasLeft) {
		
		scsiDeviceSetRxDataBuffer(&disk->scsiDevice, disk->buffer, diskPrvGetActualSectorSize(disk));

	}
	else {

		disk->multiblockState = MultiblockIdle;
		return false;	//will go to idle
	}
	
	return true;
}

static bool diskPrvStatWrite(struct ScsiDisk *disk, uint32_t lba, uint_fast16_t nBlocks)
{
	if (diskPrvSignalInvalidLunIfNeeded(disk))
		return false;
	
	disk->multiblockState = MultiblockWrite;
	disk->nextLba = lba;
	disk->numLbasLeft = nBlocks;
	
	scsiDeviceSetRxDataBuffer(&disk->scsiDevice, disk->buffer, diskPrvGetActualSectorSize(disk));

	return true;
}
	
static bool diskPrvRead(struct ScsiDisk *disk, uint32_t lba, uint_fast16_t nBlocks)
{
	if (diskPrvSignalInvalidLunIfNeeded(disk))
		return false;
	
	//for CDROM
	nBlocks *= diskPrvGetReportedSectorSize(disk) / diskPrvGetActualSectorSize(disk);
	
	disk->multiblockState = MultiblockRead;
	disk->nextLba = lba;
	disk->numLbasLeft = nBlocks;
	
	return diskPrvContinueMultiblockOp(disk);
}

static bool diskPrvReadCapacity(struct ScsiDisk *disk)
{
	uint32_t t;
	
	if (diskPrvSignalInvalidLunIfNeeded(disk))
		return false;
	
	if (!disk->diskF(MASS_STORE_OP_GET_SZ, 0, &t) || !t) {
		
		disk->senseKey = SCSI_SENSE_KEY_MEDIUM_ERROR;
		disk->ASC = SCSI_ASC_Q_INCOMPAT_MEDIUM_INSTALLED;
		
		return false;
	}
	else {
		
		//index of last valid block
		t--;
		disk->buffer[0] = t >> 24;
		disk->buffer[1] = t >> 16;
		disk->buffer[2] = t >> 8;
		disk->buffer[3] = t >> 0;
		
		//block size
		t = diskPrvGetReportedSectorSize(disk);
		disk->buffer[4] = t >> 24;
		disk->buffer[5] = t >> 16;
		disk->buffer[6] = t >> 8;
		disk->buffer[7] = t >> 0;
	
		scsiDeviceSetDataToTx(&disk->scsiDevice, disk->buffer, 8);
		return true;
	}
}

static enum ScsiHlCmdResult diskPrvScsiHlCmdRxed(void *userData, const uint8_t *cmd, uint_fast8_t len)
{
	struct ScsiDisk *disk = (struct ScsiDisk*)userData;
	enum ScsiHlCmdResult ret;
		
	if (VERBOSE) {
		
		uint_fast8_t i;
	
		err_str("DISK RXed %u byte cmd:", len);
		for (i = 0; i < len; i++)
			err_str(" %02x", cmd[i]);
		err_str("\r\n");
	}
	
	disk->nextStatusOut = SCSI_STATUS_CHECK_CONDITION;
	ret = ScsiHlCmdResultGoToStatus;
	
	if (cmd[len - 1] & 3) {	//handle control byte's FLAG and LINK fields
		
		err_str(" ## flag or link bits seen\r\n");
		disk->senseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
		disk->ASC = 0;
	}
	else {
	
		uint_fast16_t len;
		uint32_t lba;
	
		switch (cmd[0]) {
			
			case SCSI_CMD_TEST_UNIT_READY:
				disk->nextStatusOut = SCSI_STATUS_GOOD;
				ret = ScsiHlCmdResultGoToStatus;
				break;
			
			case SCSI_CMD_READ:
				lba = (((uint32_t)(cmd[1] & 0x1f)) << 16) + (((uint32_t)cmd[2]) << 8) + cmd[3];
				len = cmd[4];
				if (!len)
					len = 256;
				if (diskPrvRead(disk, lba, len)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			case SCSI_CMD_READ_EXTENDED:
				lba = (((uint32_t)cmd[2]) << 24) + (((uint32_t)cmd[3]) << 16) + (((uint32_t)cmd[4]) << 8) + cmd[5];
				len = (((uint32_t)cmd[7]) << 8) + cmd[8];
				if (!len)	//valid
					ret = ScsiHlCmdResultGoToStatus;
				if (diskPrvRead(disk, lba, len)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			case SCSI_CMD_WRITE:
				lba = (((uint32_t)(cmd[1] & 0x1f)) << 16) + (((uint32_t)cmd[2]) << 8) + cmd[3];
				len = cmd[4];
				if (!len)
					len = 256;
				if (diskPrvStatWrite(disk, lba, len)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataOut;
				}
				break;
			
			case SCSI_CMD_WRITE_EXTENDED:
				lba = (((uint32_t)cmd[2]) << 24) + (((uint32_t)cmd[3]) << 16) + (((uint32_t)cmd[4]) << 8) + cmd[5];
				len = (((uint32_t)cmd[7]) << 8) + cmd[8];
				if (!len)	//valid
					ret = ScsiHlCmdResultGoToStatus;
				else if (diskPrvStatWrite(disk, lba, len)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataOut;
				}
				break;
			
			case SCSI_CMD_INQUIRY:
				if (diskPrvHandleInquiry(disk, cmd[1] & 1, cmd[2], cmd[4] ? cmd[4] : 256)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			case SCSI_CMD_REQUEST_SENSE:
				if (diskPrvHandleRequestSense(disk, cmd[4] ? cmd[4] : 256)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			case SCSI_CMD_MODE_SENSE:
				if (diskPrvHandleModeSense(disk, !!(cmd[1] & 0x08), cmd[2] >> 6, cmd[2] & 0x3f, cmd[4] ? cmd[4] : 256)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			case SCSI_CMD_READ_CAPACITY:
				if (cmd[1] & 1) {	//relative address bit not supported as we do not do linked commands
					disk->senseKey = SCSI_SENSE_KEY_ILLEGAL_REQUEST;
					disk->ASC = SCSI_ASC_Q_INVALID_FIELD_IN_CDB;
				}
				else if (diskPrvReadCapacity(disk)) {
					disk->nextStatusOut = SCSI_STATUS_GOOD;
					ret = ScsiHlCmdResultGoToDataIn;
				}
				break;
			
			default:
				err_str(" ## disk not handling command 0x%02x\r\n", cmd[0]);
				while(1);
				break;
		}
	}
	
	if (ret == ScsiHlCmdResultGoToStatus) {
		
		scsiDeviceSetDataToTx(&disk->scsiDevice, &disk->nextStatusOut, 1);
	}
	if (VERBOSE)
		err_str(" ## disk replying with %u\r\n", ret);
	return ret;
}

static enum ScsiHlCmdResult diskPrvScsiHlXferDone(void *userData)
{
	struct ScsiDisk *disk = (struct ScsiDisk*)userData;
	
	switch (disk->multiblockState) {
		case MultiblockIdle:
			break;
		
		case MultiblockRead:
			if (diskPrvContinueMultiblockOp(disk))
				return ScsiHlCmdResultGoToDataIn;
			break;
			
		case MultiblockWrite:
			if (diskPrvContinueMultiblockOp(disk))
				return ScsiHlCmdResultGoToDataOut;
			break;
	}
	
	scsiDeviceSetDataToTx(&disk->scsiDevice, &disk->nextStatusOut, 1);
	return ScsiHlCmdResultGoToStatus;
}

bool scsiDiskInit(struct ScsiDisk *disk, uint_fast8_t scsiId, MassStorageF diskF, void *buf512, bool isCDROM)
{
	uint32_t diskSz, numCyl = 1, numHeads = 1, numSecPerTrack = 1, diskSzOrig;
	
	static const struct ScsiHlFuncs diskFuncs = {
		.ScsiHlSetLun = diskPrvScsiHlSetLun,
		.ScsiHlCmdRxed = diskPrvScsiHlCmdRxed,
		.ScsiHlXferDone = diskPrvScsiHlXferDone,
	};

	disk->diskF = diskF;
	disk->buffer = buf512;
	
	if (!disk->diskF(MASS_STORE_OP_GET_SZ, 0, &diskSzOrig))
		return false;

#if CDROM_SUPORTED
	disk->isCDROM = isCDROM;
#endif
	err_str("isCDROM: %d\n", (int)isCDROM);
	
	//CAP = sector_sz * sectors_per_track * num_heads * num_cylinders
	// sector_sz = 512
	// sectors_per_track is 16 bits
	// num_heads is 8 bits
	// num_cylinders is 24 bits
	//not optimal, but good enough
	// BIG RED NOTICE: ultrix passes "num cylinders" around as a u16 !!!!
	numCyl = diskSzOrig;
	while (numCyl > 2048) {		//minimize cylinders, but to a sane limit, ultrix does not like weird disks...
		
		static const uint8_t primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, };
		uint_fast8_t i, factor;
		
		for (i = 0; i < sizeof(primes); i++) {
			
			factor = primes[i];
			if (numCyl % factor)
				continue;
			
			//allocate to whichever is less far along its way
			if (15 - __builtin_clz(numSecPerTrack) < 2 * (8 - __builtin_clz(numHeads))) {
			
				if (!(numSecPerTrack * factor / 0x10000))
					numSecPerTrack *= factor;
				else if (!(numHeads * factor / 0x100))
					numHeads *= factor;
				else
					continue;	//not likely to help, but makes code simpler
			}
			else {
				
				if (!(numHeads * factor / 0x100))
					numHeads *= factor;
				else if (!(numSecPerTrack * factor / 0x10000))
					numSecPerTrack *= factor;
				else
					continue;	//not likely to help, but makes code simpler
			}
			
			numCyl /= factor;
			
			break;
		}
		if (i == sizeof(primes))
			break;
	}
	
	if (numCyl >> 16) {
		
		err_str("geometry optimal config failed - going for simple\r\n");
		numCyl = diskSzOrig;
		numHeads = 1;
		numSecPerTrack = 1;
		while (numCyl >> 16) {
			numCyl >>= 1;
			numSecPerTrack <<= 1;
		}
	}
	
	diskSz = numCyl * numHeads * numSecPerTrack;
	if (diskSz != diskSzOrig)
		err_str("Disk geometry limits dictate a size of %u sectors, losing %u sectors\r\n", diskSz, diskSzOrig - diskSz);
	else
		err_str("Disk geometry: %u cyl of %u heads of %u-sector tracks for %u LBAs total\r\n", numCyl, numHeads, numSecPerTrack, diskSz);
	
	disk->geometry.numCyl = numCyl;
	disk->geometry.numHeads = numHeads;
	disk->geometry.secPerTrack = numSecPerTrack;
	
	return scsiDeviceInit(&disk->scsiDevice, scsiId, &diskFuncs, disk);
}
