/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _ESAR_H_
#define _ESAR_H_

#include <stdbool.h>
#include <stdint.h>


bool esarMemAccess(uint32_t paOfst, uint_fast8_t size, bool write, void* buf);



#endif

