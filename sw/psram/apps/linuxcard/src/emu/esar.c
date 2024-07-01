/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "esar.h"


#pragma GCC optimize ("Os")


static const uint8_t __attribute__((aligned(4))) mEsarEepromData[32] = {
	
	0x66, 0x44, 0x22, 0x44, 0x66, 0x22,	//address (big endian)
	0xee, 0xaa, 						//checksum for the above address
	
	//the above 8 bytes in reverse order are here:
	0xaa, 0xee, 						//checksum for the below address (ReverseD)
	0x22, 0x66, 0x44, 0x22, 0x33, 0x66,	//address (reversed and thus LE)
	
	//the first 8 bytes replicated here
	0x66, 0x44, 0x22, 0x44, 0x66, 0x22,	//address
	0xee, 0xaa, 						//checksum
	
	//check pattern
	0xff, 0x00, 0x55, 0xaa,
	0xff, 0x00, 0x55, 0xaa,
};


/*
	sum:
	
	sum = 0;
	for (int i = 0; i < 3; i++) {
		
		sum += addr[i * 2 + 0] * 256 + addr[i * 2 + 1];
		
		if (sum & 0x8000)
			sum = (sum << 1) - 0xffff;
		else
			sum <<= 1;
	}
	addr[6] * 256 + addr[7] MUST_EQUAL sum
*/


bool esarMemAccess(uint32_t paOfst, uint_fast8_t size, bool write, void* buf)
{
	if (!write && paOfst < sizeof(mEsarEepromData) * 4) {
		
		if (size == 4 && !(paOfst & 3)) {
			
			*(uint32_t*)buf = ((uint32_t)mEsarEepromData[paOfst / 4]) << 8;
			return true;
		}
		else if (size == 2 && !(paOfst & 3)) {
			
			*(uint16_t*)buf = ((uint32_t)mEsarEepromData[paOfst / 4]) << 8;
			return true;
		}
		else if (size == 1 && (paOfst & 3) == 1) {
			
			*(uint8_t*)buf = mEsarEepromData[paOfst / 4];
			return true;
		}
	}
	
	return false;
}