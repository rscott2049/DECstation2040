/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SD_HW_H_
#define _SD_HW_H_

#include <stdbool.h>
#include <stdint.h>


#define FLAG_MISC_ERR		0x80		//not used by spec
#define FLAG_PARAM_ERR		0x40
#define FLAG_ADDR_ERR		0x20
#define FLAG_ERZ_SEQ_ERR	0x10
#define FLAG_CMD_CRC_ERR	0x08
#define FLAG_ILLEGAL_CMD	0x04
#define FLAG_ERZ_RST		0x02
#define FLAG_IN_IDLE_MODE	0x01


enum SdHwWriteReply {
	SdHwWriteAccepted,
	SdHwWriteCrcErr,
	SdHwWriteError,
	SdHwCommErr,
	SdHwTimeout,
};

enum SdHwReadResult {
	SdHwReadOK,
	SdHwReadTimeout,
	SdHwReadCrcErr,
	SdHwReadFramingError,
	SdHwReadInternalError,
};

enum SdHwRespType {
	SdRespTypeNone,
	
	SdRespTypeR1,				//almost all commands. SDIO: 48 bit with crc; SPI: 1 byte. one byte always returned (SD reply converted to SPI reply)
	SdRespTypeR1withBusy,		//same
	SdRespTypeR3,				//OCR. SDIO: 48 bit with no crc; SPI: 1+4 bytes. 4 bytes always returned
	SdRespTypeR7,				//OP_CMD. SDIO: 48 bit with no crc; SPI: 1+4 bytes. 4 bytes always returned
	
	SdRespTypeSdR2,				//136 bit with crc
	SdRespTypeSdR6,				//48 bit with crc, compressed status, RCA
	
	SdRespTypeSpiR2,			//two byte. one byte returned, any errors in second byte marked as "FLAG_MISC_ERR" into first
};

enum SdHwDataDir {
	SdHwDataNone,
	SdHwDataWrite,
	SdHwDataRead,
};

enum SdHwCmdResult {
	SdHwCmdResultOK,
	SdHwCmdResultRespTimeout,
	SdCmdInvalid,				//separated out from card reply
	SdCmdInternalError,
};


#define SD_HW_FLAG_INITED				0x00000001
#define SD_HW_FLAG_SUPPORT_4BIT			0x00000002
#define SD_HW_FLAG_SDIO_IFACE			0x00000004
#define SD_HW_FLAG_SPI_DOES_CRC			0x00000008	//set if spi iface will do CRC (we'll then not turn it off)

uint32_t sdHwInit(void);		//should set speed to 400khz
void sdHwGiveInitClocks(void);

void sdHwRxRawBytes(void *dst /* can be NULL*/, uint_fast16_t numBytes);


void sdHwSetSpeed(uint32_t maxHz);

enum SdHwCmdResult sdHwCmd(uint_fast8_t cmd, uint32_t param, bool cmdCrcRequired, enum SdHwRespType respTyp, void *respBufOut, enum SdHwDataDir dataDir, uint_fast16_t blockSz, uint32_t numBlocks);

void sdHwSetTimeouts(uint_fast16_t timeoutBytes, uint32_t rdTimeoutTicks, uint32_t wrTimeoutTicks);

bool sdHwSetBusWidth(bool useFourWide);

void sdHwNotifyRCA(uint_fast16_t rca);	//sometimes hw needs to know this

void sdHwChipDeselect(void);
bool sdHwDataWait(uint_fast16_t timeoutBytes, uint32_t timeoutTicks);

bool sdHwReadData(uint8_t *data, uint_fast16_t sz);
enum SdHwWriteReply sdHwWriteData(const uint8_t *data, uint_fast16_t sz, bool isMultiblock);
bool sdHwPrgBusyWait(void);

bool sdHwMultiBlockWriteSignalEnd(void);
bool sdHwMultiBlockReadSignalEnd(void);


#endif
