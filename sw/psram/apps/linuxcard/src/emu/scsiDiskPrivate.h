/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_DISK_PRIVATE_H_
#define _SCSI_DISK_PRIVATE_H_


#include "scsiDevice.h"
#include <stdint.h>
#include "soc.h"


enum MultiblockState {
	MultiblockIdle,
	MultiblockRead,
	MultiblockWrite,
};

struct ScsiDisk {
	
	struct ScsiDevice scsiDevice;
	MassStorageF diskF;
	uint8_t *buffer;
	
	//geometry (sigh...)
	struct {
		uint32_t numCyl			: 24;
		uint32_t numHeads		: 8;
		uint16_t secPerTrack;
	} geometry;
	
	//read/write progress
	uint32_t nextLba;
	uint32_t numLbasLeft;
	enum MultiblockState multiblockState;
	
	//err state
	uint8_t curLun;
	uint16_t ASC;
	uint8_t nextStatusOut, senseKey;

#if CDROM_SUPORTED
	//identity
	bool isCDROM;
#endif
};





#endif
