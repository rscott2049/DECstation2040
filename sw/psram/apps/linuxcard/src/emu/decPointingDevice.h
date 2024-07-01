/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _DEC_POINTING_DEVICE_H_
#define _DEC_POINTING_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>

typedef void (*DecPointingDeviceTxF)(uint8_t chr);
typedef uint_fast8_t (*DecPointingDeviceCanTxBytesF)(void);

void decPointingDeviceInit(DecPointingDeviceTxF txF, DecPointingDeviceCanTxBytesF spaceFreeF);
void decPointingDeviceRxByte(uint8_t byte);

enum PointingDeviceButton {
	MouseButtonLeft,
	MouseButtonMiddle,
	MouseButtonRight,
};

bool decPointingDeviceIsAbsolute(void);
void decPointingDeviceMove(int32_t x, int32_t y);	//absolute for tablet, relative for mouse
void decPointingDeviceButton(enum PointingDeviceButton btn, bool pressed);


#endif


