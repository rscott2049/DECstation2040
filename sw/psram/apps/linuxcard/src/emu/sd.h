/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SD_H_
#define _SD_H_

#include <stdbool.h>
#include <stdint.h>

#define SD_BLOCK_SIZE		512

union SdFlags {
	struct {
		uint8_t inited			: 1;
		uint8_t v2				: 1;
		uint8_t HC				: 1;
		uint8_t SD				: 1;
		uint8_t RO				: 1;
		uint8_t cmd23supported	: 1;
		uint8_t hasDiscard		: 1;
		uint8_t sdioIface		: 1;
	};
	uint8_t value;
};


bool sdCardInit(uint8_t buf[static 64]);
uint32_t sdGetNumSecs(void);

bool sdSecRead(uint32_t sec, uint8_t *dst);
bool sdSecWrite(uint32_t sec, const uint8_t *src);

bool sdReadStart(uint32_t sec, uint32_t numSec);	//if numSec is nonzero, card will be advised to expect that many blocks
bool sdReadNext(uint8_t *dst);
bool sdReadStop(void);

bool sdWriteStart(uint32_t sec, uint32_t numSec);	//if numSec is nonzero, card will be advised to expect that many blocks
bool sdWriteNext(const uint8_t *src);
bool sdWriteStop(void);

void sdGetInfo(uint8_t *midP, uint16_t *oidP, uint32_t *snumP);
uint8_t sdGetFlags(void);

void sdReportLastError(void);

#endif
