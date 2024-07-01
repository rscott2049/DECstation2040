/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _UC_HW_H_
#define _UC_HW_H_


void initHwSuperEarly(void);			//before safety delay - bette rbe careful

void initHw(void);

//fatal errors only
void hwError(uint_fast8_t err);

uint8_t hwGetUidLen(void);	//in bytes
uint8_t hwGetUidByte(uint_fast8_t idx);

#endif

