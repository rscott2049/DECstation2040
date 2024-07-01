/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "decPointingDevice.h"


static DecPointingDeviceTxF mTxF;
static DecPointingDeviceCanTxBytesF mSpaceFreeF;
static uint8_t mCurBtnState = 0;	//in bit order we send
static bool mAutoMode;

#define DEC_MOUSE_MAX_MOVE			0x7f

void decPointingDeviceInit(DecPointingDeviceTxF txF, DecPointingDeviceCanTxBytesF spaceFreeF)
{
	mTxF = txF;
	mSpaceFreeF = spaceFreeF;
}

void decPointingDeviceRxByte(uint8_t byte)
{
	if (byte == 'T' ) {
		
		fprintf(stderr, "mouse self test\r\n");
		mTxF(0);
		mTxF(2);	//abs tablet
		mTxF(0);
		mTxF(0);
	}
	else if (byte == 'R') {
		
		mAutoMode = true;
	}
	else if (byte == 'P') {
		
		mAutoMode = false;
		if (mSpaceFreeF() >= 3) {
		
			mTxF(0x80 + mCurBtnState);	//no support for this mode - fuck it
			mTxF(0);
			mTxF(0);
		}
	}
	else 
		fprintf(stderr, "mouse RXed 0x%02x\r\n", byte);
}

void decPointingDeviceMove(int32_t dx, int32_t dy)
{
	uint_fast8_t byte = 0x80 + mCurBtnState;
	
	if (dx < 0)
		dx = -dx;
	else
		byte += 0x10;
	
	if (dy < 0) {
		
		dy = -dy;
		byte += 0x08;
	}
		
	
	if (dx > DEC_MOUSE_MAX_MOVE)
		dx = DEC_MOUSE_MAX_MOVE;
	
	if (dy > DEC_MOUSE_MAX_MOVE)
		dy = DEC_MOUSE_MAX_MOVE;
	
	if (mAutoMode) {
	
		if (mSpaceFreeF() >= 3) {
		
			mTxF(byte);
			mTxF(dx);
			mTxF(dy);
		}
	}
}

void decPointingDeviceButton(enum PointingDeviceButton btn, bool pressed)
{
	uint8_t bit;
	
	switch (btn) {
		case MouseButtonLeft:
			bit = 0x04;
			break;
		
		case MouseButtonMiddle:
			bit = 0x02;
			break;
		
		case MouseButtonRight:
			bit = 0x01;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	if (pressed)
		mCurBtnState |= bit;
	else
		mCurBtnState &=~ bit;
	
	decPointingDeviceMove(0, 0);
}

bool decPointingDeviceIsAbsolute(void)
{
	return true;
}