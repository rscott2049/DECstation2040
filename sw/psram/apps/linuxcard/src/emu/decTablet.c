/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "decPointingDevice.h"
#include "graphics.h"


static DecPointingDeviceTxF mTxF;
static DecPointingDeviceCanTxBytesF mSpaceFreeF;
static uint8_t mCurBtnState = 0;	//in bit order we send
static uint16_t mLastX, mLastY;
static bool mAutoMode;


void decPointingDeviceInit(DecPointingDeviceTxF txF, DecPointingDeviceCanTxBytesF spaceFreeF)
{
	mTxF = txF;
	mSpaceFreeF = spaceFreeF;
}


static void tabletPrvSendReport(bool proximity)
{
	uint_fast8_t byte = 0xc0 + mCurBtnState + (proximity ? 1 : 0);
	
	if (mSpaceFreeF() >= 5) {
	
		mTxF(byte);
		mTxF(mLastX & 0x3f);
		mTxF(mLastX >> 6);
		mTxF(mLastY & 0x3f);
		mTxF(mLastY >> 6);
	}
}

void decPointingDeviceRxByte(uint8_t byte)
{	
	switch (byte) {
		case 'K':	//55 reports per sec please
		case 'L':	//72 reports per sec please
		case 'M':	//120 reports per sec please
		case 'B':	//go to higher baudrate please
			break;
		
		case 'P':	//please send a report (and go to request mode
			tabletPrvSendReport(true);
			//fallthrough
		
		case 'D':	//request mode please
			mAutoMode = false;
			break;
		
		case 'R':	//auto-stream mode please
			mAutoMode = true;
			break;
		
		case 'T':	//test
			mTxF(0xa0);
			mTxF(0x04);
			mTxF(0x11);	//stylus is connected
			mTxF(0x00);
			break;
		
		default:
			fprintf(stderr, "tablet RXed 0x%02x\r\n", byte);
			break;
	}
}

void decPointingDeviceMove(int32_t x, int32_t y)
{
	const uint32_t tabletMaxDeflection = 2200;	//ask dec
	
	//invert y
	y = SCREEN_HEIGHT - 1 - y;
	
	//input is screen coords, we need to convert to tablet coords
	x = x * tabletMaxDeflection / SCREEN_WIDTH;
	y = y * tabletMaxDeflection / SCREEN_HEIGHT;
	
	//now record it
	mLastX = x;
	mLastY = y;
	
	if (mAutoMode)
		tabletPrvSendReport(true);
}

void decPointingDeviceButton(enum PointingDeviceButton btn, bool pressed)
{
	uint8_t bit;
	
	switch (btn) {
		case MouseButtonLeft:
			bit = 0x02;
			break;
		
		case MouseButtonMiddle:
			bit = 0; //not reported
			break;
		
		case MouseButtonRight:
			bit = 0x04;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	if (pressed)
		mCurBtnState |= bit;
	else
		mCurBtnState &=~ bit;
	
	if (mAutoMode)
		tabletPrvSendReport(true);
}

bool decPointingDeviceIsAbsolute(void)
{
	return true;
}