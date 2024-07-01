/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_DEVICE_H_
#define _SCSI_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "scsiDevicePrivate.h"


struct ScsiDevice;

enum ScsiHlCmdResult {
	ScsiHlCmdResultGoToDataIn,
	ScsiHlCmdResultGoToDataOut,
	ScsiHlCmdResultGoToStatus,
};

struct ScsiHlFuncs {
	void (*ScsiHlSetLun)(void *userData, uint_fast8_t lun);	//return true to allow
	enum ScsiHlCmdResult (*ScsiHlCmdRxed)(void *userData, const uint8_t *cmd, uint_fast8_t len);
	enum ScsiHlCmdResult (*ScsiHlXferDone)(void *userData);
};

bool scsiDeviceInit(struct ScsiDevice *dev, uint_fast8_t scsiId, const struct ScsiHlFuncs *funcs, void *userData);

void scsiDeviceSetDataToTx(struct ScsiDevice *dev, const void *data, uint32_t len);
void scsiDeviceSetRxDataBuffer(struct ScsiDevice *dev, void *data, uint32_t len);

#endif
