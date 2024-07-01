/*
	(c) 2024 Rob Scott

*/

#ifndef _USBHID_H_
#define _USBHID_H_

uint32_t usbhid_init(void);

void decMouseTx(uint8_t chr);
void decKeyboardTx(uint8_t chr);

#endif // _USBHID_H_
