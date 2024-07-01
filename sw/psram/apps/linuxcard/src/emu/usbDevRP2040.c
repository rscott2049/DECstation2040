#include <stdint.h>
#include "usbDev.h"

bool usbInit(void) {
}

bool usbAttach(bool attached) {
}

bool usbEpUnconfigIn(uint_fast8_t epNo) {
}

bool usbEpUnconfigOut(uint_fast8_t epNo) {
}

bool usbEpIntrCfgIn(uint_fast8_t epNo, uint_fast16_t epSz) {
}

bool usbEpIntrCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr) {
}

bool usbEpBulkCfgIn(uint_fast8_t epNo, uint_fast16_t epSz) {
}

bool usbEpBulkCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr) {
}

bool usbEpStallIn(uint_fast8_t epNo, bool stall) {
}

bool usbEpStallOut(uint_fast8_t epNo, bool stall) {
}

bool usbEpIsStalledIn(uint_fast8_t epNo) {
}

bool usbEpIsStalledOut(uint_fast8_t epNo) {
}

bool usbEpCanTx(uint_fast8_t epNo) {
}

bool usbEpTx(uint_fast8_t epNo, const void *data, uint_fast16_t len, bool autoZlp) {
}

bool usbRxRelease(uint_fast8_t epNo) {
}

uint_fast8_t usbEp0dataRx(uint8_t *data) {
}

enum UsbState usbGetState(void) {
}

//externally provided
bool usbExtSetIface(uint_fast16_t iface, uint_fast16_t setting) {
}

//true if handled. set len to negative to stall, non-negative to send reply. on entry dataOutP will point to a place where up to 4 bytes may be stashed
bool usbExtEp0handler(const struct UsbSetup *setup, void **dataOutP, int16_t *lenOutP) {
}

//data valid till released, no more rx till released. see usbRxRelease()
void usbExtEpDataArrivalNotif(uint8_t epNo, uint_fast8_t len) {
}

void usbExtConfigSelected(uint8_t cfgIdx) {
}

void usbExtStateChangedNotif(enum UsbState nowState) {
}

//should be in ram, usb device fails to dma from rom
const void* usbExtGetGescriptor(uint_fast8_t descrType, uint_fast8_t descrIdx, uint_fast16_t langIdx, uint16_t *lenP) {
}
