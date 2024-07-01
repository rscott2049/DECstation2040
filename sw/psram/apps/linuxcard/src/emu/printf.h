/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _PRINTF_H_
#define _PRINTF_H_

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#ifdef SUPPORT_DEBUG_PRINTF

	#define prv			prvRaw
	#define pr			prRaw

#else

	#define prv(...) do {} while(0)
	#define pr(...) do {} while(0)

#endif

//provided
void prvRaw(const char* fmtStr, va_list vl);
void prRaw(const char* fmtStr, ...);


//external
void prPutchar(char chr);

#endif

