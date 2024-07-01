/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <termios.h>
#include "decPointingDevice.h"
#include "lk401.h"
#include "dz11.h"
#include "soc.h"
#include "mem.h"
#define off64_t __off64_t




static struct termios gOldTermios;

static FILE *gDiskFile;
static bool gCtlCSeen = false;





static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
{
	switch (op) {
		case MASS_STORE_OP_GET_SZ:
			fseeko64(gDiskFile, 0, SEEK_END);
			 *(uint32_t*)buf = (off64_t)ftello64(gDiskFile) / (off64_t)BLK_DEV_BLK_SZ;
			 return true;
		case MASS_STORE_OP_READ:
			fseeko64(gDiskFile, (off64_t)sector * (off64_t)BLK_DEV_BLK_SZ, SEEK_SET);
			return fread(buf, 1, BLK_DEV_BLK_SZ, gDiskFile) == BLK_DEV_BLK_SZ;
		case MASS_STORE_OP_WRITE:
			fseeko64(gDiskFile, (off64_t)sector * (off64_t)BLK_DEV_BLK_SZ, SEEK_SET);
			return fwrite(buf, 1, BLK_DEV_BLK_SZ, gDiskFile) == BLK_DEV_BLK_SZ;
	}
	return false;
}

void ctl_cHandler(int v)	//handle SIGTERM      
{
	(void)v;
	
	fclose(gDiskFile);
	tcsetattr(0, TCSANOW, &gOldTermios);
	gCtlCSeen = 1;
	exit(0);
}

static void keyboardByteRxed(uint8_t byte)
{
	dz11charRx(0, byte);
}

static void pointingDeviceByteRxed(uint8_t byte)
{
	dz11charRx(1, byte);
}

static uint_fast8_t pointingDeviceCanTxBytes(void)
{
	return dz11numBytesFreeInRxBuffer(1);
}

void dz11rxSpaceNowAvail(uint_fast8_t line)
{
	(void)line;
}

int main(int argc, char** argv)
{
	struct termios cfg, old;
	uint32_t romSz = 0;
	uint8_t tmp;
	FILE *f;
	int gdbPort = 0;

	#ifdef GDB_SUPPORT
		if (argc == 4)
			gdbPort = atoi(argv[--argc]);
	#endif


	if (argc != 3) {
		fprintf(stderr, "USAGE: %s <rom.img> <disk.img>"
		
		#ifdef GDB_SUPPORT
			" [<gdb_port]>"
		#endif
		
		"\n", argv[0]);
		return -1;
	}	
	
	gDiskFile = fopen64(argv[2], "r+b");
	if(!gDiskFile){
		fprintf(stderr,"Failed to open root device\n");
		return -1;
	}
	
	if (!socInit(massStorageAccess)) {
		fprintf(stderr," soc init fail\n");
		fclose(gDiskFile);
		return -3;
	}
	
	lk401init(keyboardByteRxed);
	decPointingDeviceInit(pointingDeviceByteRxed, pointingDeviceCanTxBytes);

	
	//load rom
	f = fopen64(argv[1], "r+b");
	if (!f) {
		fprintf(stderr, "Failed to open ROM file\n");
		fclose(gDiskFile);
		return -2;
	}
	while(fread(&tmp, 1, 1, f)) {
		if (!memAccess((ROM_BASE & 0x1FFFFFFFUL) + romSz, 1, true, &tmp)) {
			fprintf(stderr, "Failed to write rom byte %u\n", romSz);
			fclose(gDiskFile);
			return -3;
		}
		romSz++;
	}
	fclose(f);
	fprintf(stderr, "Read %u bytes of rom\n", romSz);
	
	//setup the terminal
	{
		int ret;
		
		ret = tcgetattr(0, &old);
		cfg = old;
		if(ret) perror("cannot get term attrs");
		
		#ifndef _DARWIN_
		
			cfg.c_iflag &=~ (INLCR | INPCK | ISTRIP | IUCLC | IXANY | IXOFF | IXON);
			cfg.c_oflag &=~ (OPOST | OLCUC | ONLCR | OCRNL | ONOCR | ONLRET);
			cfg.c_lflag &=~ (ECHO | ECHOE | ECHONL | ICANON | IEXTEN | XCASE);
		#else
			cfmakeraw(&cfg);
		#endif
		
		ret = tcsetattr(0, TCSANOW, &cfg);
		if(ret) perror("cannot set term attrs");
		gOldTermios = old;
	}
	
	signal(SIGINT, &ctl_cHandler);
	socRun(gdbPort);
	//does not return

	return 0;
}

void dz11charPut(uint_fast8_t line, uint_fast8_t chr)
{
	if (line == 3) {
		char ch = chr;
		
		while (1 != write(1, &ch, sizeof(ch)));
	}
	else {
		
		#ifdef MOUSE_AND_KBD
		
			if (line == 0) {		//keyboard
				
				lk401RxByte(chr);
				return;
			}
			else if (line == 1) {	//mouse
				
				decPointingDeviceRxByte(chr);
				return;
			}
			
		#else
		
			fprintf(stderr, "[%u%c]", line, chr);
		#endif
	}
}

void socInputCheck(void)
{
	struct timeval limit = {};
	fd_set set;
	
	FD_ZERO(&set);
	FD_SET(0, &set);
	
    if (1 == select(1, &set, NULL, NULL, & limit)) {
    	
    	char ch;
    	
    	if (1 == read(0, &ch, 1))
    		dz11charRx(3, (uint8_t)ch);
    }
}

