/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _USBDEV_H_
#define _USBDEV_H_

#include <stdbool.h>
#include <stdint.h>

//very simple usb driver for ATSAMD, no dual anything, no fancy-schmancy.

#define USB_MAX_EP_IDX				4
#define EP0_SZ						8



struct UsbSetup {
	uint8_t bmRequestType;
	uint8_t bRequest;
	union {
		uint16_t wValue;
		struct {
			uint8_t wValueL;
			uint8_t wValueH;
		};
	};
	union {
		uint16_t wIndex;
		struct {
			uint8_t wIndexL;
			uint8_t wIndexH;
		};
	};
	union {
		uint16_t wLength;
		struct {
			uint8_t wLengthL;
			uint8_t wLengthH;
		};
	};
};

enum UsbState {
	UsbDisconnected,
	UsbWaitForReset,
	UsbWaitForEnum,
	UsbAssigningAddress,
	UsbWaitingForConfig,
	UsbRunning,
};




bool usbInit(void);					//assumes fuse-based calibration has been done, DFLL48M is up and configured, gpios are configured
bool usbAttach(bool attached);

bool usbEpUnconfigIn(uint_fast8_t epNo);
bool usbEpUnconfigOut(uint_fast8_t epNo);

bool usbEpIntrCfgIn(uint_fast8_t epNo, uint_fast16_t epSz);
bool usbEpIntrCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr);

bool usbEpBulkCfgIn(uint_fast8_t epNo, uint_fast16_t epSz);
bool usbEpBulkCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr);

bool usbEpStallIn(uint_fast8_t epNo, bool stall);
bool usbEpStallOut(uint_fast8_t epNo, bool stall);

bool usbEpIsStalledIn(uint_fast8_t epNo);
bool usbEpIsStalledOut(uint_fast8_t epNo);

bool usbEpCanTx(uint_fast8_t epNo);
bool usbEpTx(uint_fast8_t epNo, const void *data, uint_fast16_t len, bool autoZlp);
bool usbRxRelease(uint_fast8_t epNo);

uint_fast8_t usbEp0dataRx(uint8_t *data);

enum UsbState usbGetState(void);

//externally provided
bool usbExtSetIface(uint_fast16_t iface, uint_fast16_t setting);
bool usbExtEp0handler(const struct UsbSetup *setup, void **dataOutP, int16_t *lenOutP);	//true if handled. set len to negative to stall, non-negative to send reply. on entry dataOutP will point to a place where up to 4 bytes may be stashed
void usbExtEpDataArrivalNotif(uint8_t epNo, uint_fast8_t len);	//data valid till released, no more rx till released. see usbRxRelease()
void usbExtConfigSelected(uint8_t cfgIdx);
void usbExtStateChangedNotif(enum UsbState nowState);
const void* usbExtGetGescriptor(uint_fast8_t descrType, uint_fast8_t descrIdx, uint_fast16_t langIdx, uint16_t *lenP);	//should be in ram, usb device fails to dma from rom

#endif
