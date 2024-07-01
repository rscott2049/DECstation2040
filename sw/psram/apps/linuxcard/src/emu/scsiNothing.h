/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_NOTHING_H_
#define _SCSI_NOTHING_H_

#include <stdbool.h>
#include <stdint.h>
#include "scsiDiskPrivate.h"
#include "soc.h"


//having an existing device with no LUNs makes ultrix boot a lot faster than having it time out probing

struct ScsiNothing {		//same instance can be attached to multiple scsi IDs, more or less
	
	struct ScsiDevice scsiDevice;
	uint8_t nextStatusOut;
};


bool scsiNothingInit(struct ScsiNothing *nothing, uint_fast8_t scsiId);



#endif
