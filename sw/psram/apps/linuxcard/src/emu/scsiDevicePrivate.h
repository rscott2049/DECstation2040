/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_DEVICE_PRIVATE_H_
#define _SCSI_DEVICE_PRIVATE_H_

//no peeking!

struct ScsiHlFuncs;

enum DiskState {
	DeviceIdle,
	DeviceSelected,
	DeviceWaitForMessageOut,
	DeviceWaitForMessageIn,
	DeviceRxCommand,
	DeviceTxStatus,
	DeviceTxDataIn,
	DeviceRxDataOut,
};


struct ScsiDevice {
	
	enum DiskState state;
	uint8_t syncAgreement[2];
	uint8_t curAtn				: 1;	//current state of ATN line
	uint8_t busFreeAfterMsgIn	: 1;
	
	//ext msg tx
	uint8_t curMsgIn[8];
	
	//ext msg rx
	uint16_t curExtMsgOutLen, curExtMsgOutIdx;
	uint8_t curExtMsgOutTyp;
	
	//cmd in
	uint8_t cmd[12], cmdLen, cmdIdx;
	
	//data/status out
	const uint8_t *dataOutPtr;
	uint32_t dataOutLen, dataOutIndex;
	
	//data in
	uint8_t *dataInPtr;
	uint32_t dataInLen, dataInIndex;
	
	//HL
	const struct ScsiHlFuncs *hlFuncs;
	void *hlUserData;
};


#endif

