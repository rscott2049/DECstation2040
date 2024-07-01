/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/


#pragma GCC optimize ("Os")


#include <string.h>
#include <stdarg.h>
#include "printf.h"



static uint_fast8_t prvDiv10(uint64_t *valP)
{
	uint64_t val = *valP;
	uint32_t retHi, retMid, retLo, retChar;
	
	retHi = val >> 32;
	retMid = retHi % 10;
	retHi /= 10;
	
	retMid = (retMid << 16) + (uint16_t)(val >> 16);
	retLo = retMid % 10;
	retMid /= 10;
	
	retLo = (retLo << 16) + (uint16_t)val;
	retChar = retLo % 10;
	retLo /= 10;
	
	val = retHi;
	val <<= 16;
	val += retMid;
	val <<= 16;
	val += retLo;
	
	*valP = val;
	
	return retChar;
}

static void StrPrvPrintfEx_number(uint64_t number, bool baseTen, bool zeroExtend, bool isSigned, uint32_t padToLength)
{
	char buf[64];
	uint32_t idx = sizeof(buf) - 1;
	uint32_t numPrinted = 0;
	bool neg = false;
	uint32_t chr, i;
	
	
	if (padToLength > 63)
		padToLength = 63;
	
	buf[idx--] = 0;	//terminate
	
	if (isSigned) {
		
		if (((int64_t)number) < 0) {
			
			neg = true;
			number = -number;
		}
	}
	
	do {
		
		if (baseTen)
			chr = prvDiv10(&number);
		else {
			
			chr = number & 0x0f;
			number = number >> 4;
		}
		buf[idx--] = (chr >= 10)?(chr + 'A' - 10):(chr + '0');
		
		numPrinted++;
		
	} while (number);
	
	if (neg) {
	
		buf[idx--] = '-';
		numPrinted++;
	}
	
	if (padToLength > numPrinted)
		padToLength -= numPrinted;
	else
		padToLength = 0;
	
	while (padToLength--) {
		
		buf[idx--] = zeroExtend ? '0' : ' ';
		numPrinted++;
	}
	
	idx++;
	for (i = 0; i < numPrinted; i++)
		prPutchar((buf + idx)[i]);
}

static uint32_t StrVPrintf_StrLen_withMax(const char* s, uint32_t max)
{
	uint32_t len = 0;
	
	while ((*s++) && (len < max))
		len++;
	
	return len;
}

void prvRaw(const char* fmtStr, va_list vl)
{	
	uint64_t val64;
	char c, t;
	
	while((c = *fmtStr++) != 0){
		
		if (c == '%') {
			
			bool zeroExtend = false, useLong = false, useVeryLong = false, isSigned = false, baseTen = false;
			uint32_t padToLength = 0, len, i;
			const char* str;
			
more_fmt:
			switch (c = *fmtStr++) {
				
				case '%':
				default:	
					prPutchar(c);
					break;
				
				case 'c':
					
					t = va_arg(vl,unsigned int);
					prPutchar(t);
					break;
				
				case 's':
					
					str = va_arg(vl,char*);
					if (!str)
						str = "(null)";
					if (padToLength)
						len = StrVPrintf_StrLen_withMax(str, padToLength);
					else
						padToLength = len = strlen(str);
					
					for (i = len; i < padToLength; i++)
						prPutchar(' ');
					for (i = 0; i < len; i++)
						prPutchar(*str++);
					break;
				
				case '0':
					
					if (!zeroExtend && !padToLength) {
						
						zeroExtend = true;
						goto more_fmt;
					}
				
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					
					padToLength = (padToLength * 10) + c - '0';
					goto more_fmt;
				
				case 'd':
					isSigned = true;
					//fallthrough
					
				case 'u':
					baseTen = true;
					//fallthrough
					
				case 'x':
				case 'X':
					val64 = useVeryLong ? va_arg(vl,uint64_t) : va_arg(vl,uint32_t);
					if (isSigned && !useVeryLong)
						val64 = (int64_t)(int32_t)val64;
					StrPrvPrintfEx_number(val64, baseTen, zeroExtend, isSigned, padToLength);
					break;
					
				case 'l':
					if(useLong)
						useVeryLong = true;
					useLong = true;
					goto more_fmt;

			}
		}
		else
			prPutchar(c);
	}
}

void prRaw(const char *format, ...)
{
	va_list vl;
	
	va_start(vl, format);
	prv(format, vl);
	va_end(vl);
}
