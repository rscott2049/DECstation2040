/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_DISK_H_
#define _SCSI_DISK_H_

#include <stdbool.h>
#include <stdint.h>
#include "scsiDiskPrivate.h"
#include "soc.h"

struct ScsiDisk;


bool scsiDiskInit(struct ScsiDisk *disk, uint_fast8_t scsiId, MassStorageF diskF, void *buf512, bool isCDROM);



#endif
