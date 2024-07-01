/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SII_H_
#define _SII_H_


#include <stdbool.h>

#define SII_BUFFER_SIZE			0x20000

#define NUM_SCSI_DEVICES		8



enum ScsiState {
	ScsiStateFree,			//BSY is 0, SEL is 0
	
	ScsiStateArb,			//initiator present own ID on DB, set BSY to 1, if arbitration won, set SEL to 1, release BSY. If arb lost, release all
	ScsiStateBusAlloced,	//Arb is temporary, ends here - "bus is allocated". BSY=0, SEL=1
	
	ScsiStateSel,			//initiator presents target and own Id on DB, set SEL to 1	and InO to 0. IF target exists, it set BSY to 1. initialitor sees BSY = 1, SEL = 1, know selection worked. seleases SEL. if no devs, two ways to release. see SCSI-1 p25
	ScsiStateResel,			//target presents target and own Id on DB, set SEL to 1	and InO to 1. wait 2xDD, release BSY. initiator who is reselected sets BSY = 1, target sees BSY, also sets BSY=1, wait 2xDD, set SEL=0. reselected initiator sees BSY=1, SEL=0, releses BSY
	ScsiStateConnected,		//Sel/Resel is temporary and ends here - "target & initiator connecterd". BSY=1, SEL=0
	
	//all these driven by target. master can requets a mesage out phase by setting ATN=1
	//values set such that low 3 bits are MSG|CnD|InO, code relies on this
	ScsiStateDataOut = 0b1000,	//MSG=0, CnD=0, InO=0, driven by target
	ScsiStateDataIn = 0b1001,	//MSG=0, CnD=0, InO=1, driven by target
	ScsiStateCmd = 0b1010,		//MSG=0, CnD=1, InO=0, driven by target	(async xfer only)
	ScsiStateStatus = 0b1011,	//MSG=0, CnD=1, InO=1, driven by target	(async xfer only)
	ScsiStateMsgOut = 0b1110,	//MSG=1, CnD=1, InO=0, driven by target
	ScsiStateMsgIn = 0b1111,	//MSG=1, CnD=1, InO=1, driven by target
	
	//all of these work the same.
	
	//async xfer IN:
	//	1. target present these signals, wait 1xDD, 
	//	2. target present data on DB, wait 1xDD + 1xCSD, set REQ = 1
	//	3. initiator RX signals and dat on DB, set ACK=1
	//	4. target detects ACK, releases DB and REQ
	//	5. initiator sees REQ=0, releases ACK
	//	6. if more data to send, goto 2
	//	7. devices are still connected to each other
	
	//async xfer OUT:
	//	1. target present these signals, wait 1xBSD
	//	2. target set REQ=1
	//	3. initiator sees REQ, reads signals, presents data on DB, wait 1xDD + 1xCSD, set ACK=1
	//	4. target sees ACK=1, reads data, acknowledges recept by releasing REQ
	//	5. initiator sees REQ=0 and releases ACK to 0
	//	6. target sees ACK=0, if more data is expected, goto 2
	//	7. devices are still connected to each other
	
	//for synx xfers, OFFSET NTERDLOCK DATA XFER REQUEST required to have been done
	
	//xync xfer IN
	//	1. target present these signals, wait 1xBSD
	//	2. target: for every byte t obe sent: present data in DB, wait 1xDD + 1xCSD, set REQ, wait 1xDD + 1xHTD, clear REQ
	//  3. initiator: for every byte to be RXed: see REQ pulse, read data, pulse ACK for each byte
	//	4. devices are still connected to each other
	
	//xync xfer OUT
	//	same as in, exxept target pulses REQ to request a byte, and initiator presents a byte and ACK at the same time with a pulse
	
	
	
	//for message XFER, initiator sets ATN, target may read message at its leisure. iitiator shall clear ATN befor setting ACK on last message byte xfer
	// there is a proto for resending message son parity errors ,but virtual SCSI busses are error free :D
	// target indicated successful message xfer by going to a phase that is not "message out"
	
	//reset condition forces all devices to bus free mode
};

struct ScsiDeviceFuncs {	//provided by subscribed clients
	
	void (*ScsiBusResetted)(void *userData);			//optional
	bool (*ScsiDeviceSelected)(void *userData);		//return true if device accepts being selected
	void (*ScsiDeviceAtnState)(void *userData, bool hi);
	void (*ScsiDeviceByteOut)(void *userData, uint8_t val);
	void (*ScsiDeviceByteInConsumed)(void *userData);
	
	uint32_t (*ScsiDeviceDmaIn)(void *userData, uint_fast16_t dmaWordIdx, uint8_t *extraByteP, bool haveExtraByte, uint32_t maxBytes);
	uint32_t (*ScsiDeviceDmaOut)(void *userData, uint_fast16_t dmaWordIdx, uint8_t *extraByteP, bool haveExtraByte, uint32_t bytesAvail);
};


bool siiDeviceAdd(uint_fast8_t devId, const struct ScsiDeviceFuncs *devFuncs, void *userData);

bool siiInit(uint_fast8_t ownDevId);


//for devices
void siiDevSetState(enum ScsiState desiredState);
void siiDevSetDB(uint8_t val);
void siiDevSetReq(bool set);

//externally provided
void siiPrvBufferWrite(uint_fast16_t wordIdx, uint_fast16_t val);
uint_fast16_t siiPrvBufferRead(uint_fast16_t wordIdx);


#endif
