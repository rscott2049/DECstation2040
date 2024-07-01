/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../hypercall.h"
#include "inputSDL.h"
#include "scsiNothing.h"
#include "scsiDisk.h"
#include "graphics.h"
#include "decBus.h"
#include "ds1287.h"
#include "printf.h"
#include "lance.h"
#include "dz11.h"
#include "soc.h"
#include "cpu.h"
#include "mem.h"
#include "sii.h"




static uint16_t mSiiBuffer[SII_BUFFER_SIZE / sizeof(uint16_t)];
static MassStorageF gDiskF;
static uint8_t gRam[RAM_AMOUNT];
static uint8_t gRom[256*1024];
static uint8_t gScsiBuf[512];
static struct ScsiDisk gDisk;
static struct ScsiNothing gNoDisk;



static bool accessRamRom(uint32_t pa, uint_fast8_t size, bool write, void* buf, void* isRam)
{
	//XXX: endianness
	uint8_t *mem;

	if (isRam) {
		pa -= RAM_BASE;
		mem = gRam;
	}
	else{
		pa -= (DS_ROM_BASE & 0x1FFFFFFFUL);
		mem = gRom;
	}

	if (write) {
		if (size == 4)
			*(uint32_t*)(mem + pa) = *(uint32_t*)buf;
		else if (size == 1)
			mem[pa] = *(uint8_t*)buf;
		else if (size == 2)
			*(uint16_t*)(mem + pa) = *(uint16_t*)buf;
		else if (size == 8)
			memcpy(mem + pa, buf, size);
	}
	else {
		if (size == 4)
			*(uint32_t*)buf = *(uint32_t*)(mem + pa);
		else if (size == 1)
			*(uint8_t*)buf = mem[pa];
		else if (size == 2)
			*(uint16_t*)buf = *(uint16_t*)(mem + pa);
		else
			memcpy(buf, mem + pa, size);
	}
	
	return true;
}

bool cpuExtHypercall(void)	//call type in $at, params in $a0..$a3, return in $v0, if any
{
	uint32_t hyperNum = cpuGetRegExternal(MIPS_REG_AT), t;
	uint32_t blk, pa;
	uint8_t chr;
	bool ret;

	switch (hyperNum) {
		case H_GET_MEM_MAP:
			//a0 is byte index index if >= 2, [0] is nBits, [1] is eachBitSz
			switch (cpuGetRegExternal(MIPS_REG_A0)) {
				case 0:
					pa = 1;
					break;
				
				case 1:
					pa = RAM_AMOUNT;
					break;
				
				case 2:
					pa = 0x01;	//that one bit :D
					break;
				
				default:
					pa = 0;
					break;
			}
			cpuSetRegExternal(MIPS_REG_V0, pa);
			break;
		
		case H_CONSOLE_WRITE:
			chr = cpuGetRegExternal(MIPS_REG_A0);
			if (chr == '\n')
				fputc('\r', stderr);
			fputc(chr, stderr);
			break;
		
		case H_STOR_GET_SZ:
			if (!gDiskF(MASS_STORE_OP_GET_SZ, 0, &t))
				return false;
			cpuSetRegExternal(MIPS_REG_V0, t);
			break;
		
		case H_STOR_READ:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			ret = pa < RAM_AMOUNT && RAM_AMOUNT - pa >= 512 && gDiskF(MASS_STORE_OP_READ, blk, gRam + pa);
			cpuSetRegExternal(MIPS_REG_V0, ret);
	//		fprintf(stderr, " rd_block(%u, 0x%08x) -> %d\r\n", blk, pa, ret);
		
			break;
		
		case H_STOR_WRITE:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			ret = pa < RAM_AMOUNT && RAM_AMOUNT - pa >= 512 && gDiskF(MASS_STORE_OP_WRITE, blk, gRam + pa);
			cpuSetRegExternal(MIPS_REG_V0, ret);
	//		fprintf(stderr, " wr_block(%u, 0x%08x) -> %d\r\n", blk, pa, ret);
			break;
		
		case H_TERM:
			exit(0);
			break;
		
		default:
			err_str("hypercall %u @ 0x%08x\n", hyperNum, cpuGetRegExternal(MIPS_EXT_REG_PC));
			return false;
	}
	return true;
}

#if CDROM_SUPORTED

	static struct ScsiDisk gCDROM;
	
	static bool cdromStorageAccess(uint8_t op, uint32_t sector, void *buf)
	{
		const uint32_t blockSz = 512;
		
		static FILE *f;
		
		if (!f) {
			const char *cdpath = "../ref/ultrix/ultrix-risc-4.5-mode1.ufs";
			f = fopen(cdpath, "rb");
			if (!f) {
				fprintf(stderr, "cannot open cdrom file '%s'\n", cdpath);
				exit(-6);
			}
		}
		
		switch (op) {
			case MASS_STORE_OP_GET_SZ:
				fseeko64(f, 0, SEEK_END);
				 *(uint32_t*)buf = (off64_t)ftello64(f) / (off64_t)blockSz;
				 return true;
			case MASS_STORE_OP_READ:
				fseeko64(f, (off64_t)sector * (off64_t)blockSz, SEEK_SET);
				return fread(buf, 1, blockSz, f) == blockSz;
			case MASS_STORE_OP_WRITE:
				return false;
		}
		return false;
	}

#endif

static bool accessRam(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	return accessRamRom(pa, size, write, buf, (void*)1);
}

static bool accessRom(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
	return accessRamRom(pa, size, write, buf, (void*)0);
}

bool socInit(MassStorageF diskF)
{
	uint_fast8_t i;
	
	gDiskF = diskF;
	
	if (!memRegionAdd(RAM_BASE, sizeof(gRam), accessRam))
		return false;
	
	if (!memRegionAdd(DS_ROM_BASE & 0x1FFFFFFFUL, sizeof(gRom), accessRom))
		return false;
	
	if (!decBusInit())
		return false;
	
	if (!dz11init())
		return false;
	
	if (!siiInit(7))
		return false;
	
	if (!graphicsInit())
		return false;
	
	if (!scsiDiskInit(&gDisk, 6, gDiskF, gScsiBuf, false))
		return false;
	
	#if CDROM_SUPORTED
		if (!scsiDiskInit(&gCDROM, 0, cdromStorageAccess, gScsiBuf, true))
			return false;
		i = 1;
	#else
		i = 0;
	#endif
	
	for (;i < 6; i++) {
		
		if (!scsiNothingInit(&gNoDisk, i))
			return false;
	}
	
	if (!lanceInit())
		return false;
	
	if (!ds1287init())
		return false;
	
	cpuInit(RAM_AMOUNT);
	
	return true;
}

#ifdef GDB_SUPPORT
	static void gdbCmdWait(unsigned gdbPort, bool* ss);
#endif

static bool singleStep = false;
	
void socStop(void)
{
	singleStep = true;
}

void socRun(int gdbPort)
{
	uint16_t cy = 0;
	
	(void)gdbPort;
	
	while(true) {
		cy++;
		
		#ifdef GDB_SUPPORT
			gdbCmdWait(gdbPort, &singleStep);
		#endif
		
		cpuCycle(RAM_AMOUNT);
		
		if (!(cy & 0x0fff))
			ds1287step(1);
		
		if (!(cy & 0x1fff))
			socInputCheck();
		
		if (!(cy & 0xfff))
			sdlInputPoll();
			
		if (!(cy & 0xfffff))
			graphicsPeriodic();
	}
}

void siiPrvBufferWrite(uint_fast16_t wordIdx, uint_fast16_t val)
{
	mSiiBuffer[wordIdx] = val;
}

uint_fast16_t siiPrvBufferRead(uint_fast16_t wordIdx)
{
	return mSiiBuffer[wordIdx];
}



















//this code is a very big mess, it also is not guaranteed to work or pass any sort of test, it is here to assist in debugging of in-emulator code. no more no less

#ifdef GDB_SUPPORT


	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/select.h>
	#include <unistd.h>
	#include <errno.h>
	#include <stdlib.h>
	#include <netinet/in.h>
	#include <string.h>
	#include <stdio.h>
	
	#define MAX_BKPT	16
	
	static uint32_t gBkpts[MAX_BKPT];
	static uint32_t gNumBkpts = 0;
	
	
	
	static bool socdBkptDel(uint32_t addr, uint8_t sz){
		
		uint8_t i;
		
		(void)sz;
		for(i = 0; i < gNumBkpts; i++){
			
			if(gBkpts[i] == addr){
				
				gNumBkpts--;
				gBkpts[i] = gBkpts[gNumBkpts];
				i--;
			}	
		}
		
		return true;
	}
	
	
	static bool socdBkptAdd(uint32_t addr, uint8_t sz){	//bool
		
		socdBkptDel(addr, sz);
		
		if(gNumBkpts == MAX_BKPT) return false;
		
		gBkpts[gNumBkpts++] = addr;
		
		return true;
	}
	
	static uint32_t htoi(const char** cP){
		
		uint32_t i = 0;
		const char* in = *cP;
		char c;
		
		while((c = *in) != 0){
			
			if(c >= '0' && c <= '9') i = (i * 16) + (c - '0');
			else if(c >= 'a' && c <= 'f') i = (i * 16) + (c + 10 - 'a');
			else if(c >= 'A' && c <= 'F') i = (i * 16) + (c + 10 - 'A');
			else break;
			in++;
		}
		
		*cP = in;
		
		return i;
	}
	
	static bool gdb_memAccess(uint32_t va, uint8_t* buf, bool write){
		
		return cpuMemAccessExternal(buf, va, 1, write, CpuAccessAsCurrent);
	}
	
	static bool gdbRegGet(uint8_t which, uint32_t *dst)
	{
		if (which < 32)
			*dst = cpuGetRegExternal(which);
		else switch (which - 32) {
			case 0:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_STATUS);
				break;
			case 1:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_LO);
				break;
			case 2:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_HI);
				break;
			case 3:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_VADDR);
				break;
			case 4:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_CAUSE);
				break;
			case 5:
				*dst = cpuGetRegExternal(MIPS_EXT_REG_PC);
				break;
			default:
				return false;
		}
		return true;
	}
	
	static bool gdbRegSet(uint8_t which, uint32_t val)
	{
		if (which < 32)
			cpuSetRegExternal(which, val);
		else switch (which - 32) {
			
			case 0:
				cpuSetRegExternal(MIPS_EXT_REG_STATUS, val);
				break;
			case 1:
				cpuSetRegExternal(MIPS_EXT_REG_LO, val);
				break;
			case 2:
				cpuSetRegExternal(MIPS_EXT_REG_HI, val);
				break;
			case 3:
				cpuSetRegExternal(MIPS_EXT_REG_VADDR, val);
				break;
			case 4:
				cpuSetRegExternal(MIPS_EXT_REG_CAUSE, val);
				break;
			case 5:
				cpuSetRegExternal(MIPS_EXT_REG_PC, val);
				break;
			
			default:
				return false;
		}
		return true;
	}
	
	static bool addRegToStr(char* str, int reg){
		
		uint32_t val;
		
		if (!gdbRegGet(reg, &val))
			return false;
			
		sprintf(str + strlen(str), "%08x", __builtin_bswap32(val));
		
		return true;
	}
	
	static int interpPacket(const char* in, char* out, bool* ss){	//return 0 if we failed to interp a command, 1 is all ok, -1 to send no reply and run. pi or other irrational numbers may be returned in reply to unreasonable (or irrational) requests :)
		
		unsigned char c;
		unsigned addr, len;
		int i;
		int ret = 1;
		
		
		if(strcmp(in, "qSupported") == 0){
			
			strcpy(out, "PacketSize=99");	
		}
		else if(strcmp(in, "vCont?") == 0){
			
			out[0] = 0;
		}
		else if(strcmp(in, "s") == 0){		//single step
			
			*ss = true;
			return -1;
		}
		else if(strcmp(in, "c") == 0 || in[0] == 'C'){		//continue [with signal, which we ignore]
			
			return -1;
		}
		else if(in[0] == 'Z' || in[0] == 'z'){
			
			char op = in[0];
			char type = in[1];
			bool (*f)(uint32_t addr, uint8_t sz) = NULL;
			
			in += 3;
			addr = htoi(&in);
			if(*in++ != ',') goto fail;	//no comma?
			len = htoi(&in);
			if(*in) goto fail;		//string not over?
			
			if(type == '0' || type == '1'){	//bkpt
				
				f = (op == 'Z') ? socdBkptAdd : socdBkptDel;
			}

			strcpy(out, f && f(addr, len) ? "OK" : "e00");
		}
		else if(in[0] == 'H' && (in[1] == 'c' || in[1] == 'g')){
			strcpy(out, "OK");	
		}
		else if(in[0] == 'q'){
			
			if(in[1] == 'C'){
				
				strcpy(out, "");	
			}
			else if(strcmp(in  +1, "Offsets") == 0){
				
				strcpy(out, "Text=0;Data=0;Bss=0");
			}
			else goto fail;
		}
		else if(in[0] == 'p'){	//read register
			
			in++;
			i = htoi(&in);
			if(*in) goto fail;	//string not over?
			
			out[0] = 0;
			if(!addRegToStr(out, i)) goto fail;
		}
		else if(strcmp(in, "g") == 0){	//read all registers
			
			out[0] = 0;
			for(i = 0; i < 38; i++) if(!addRegToStr(out, i)) goto fail;
		}
		else if(in[0] == 'P'){	//write register
			
			in++;
			i = htoi(&in);
			if(*in++ != '=') goto fail;	//string not over?
			addr = htoi(&in);		//get val
			
			strcpy(out, gdbRegSet(i, addr) ? "OK" : "e00");
		}
		else if(in[0] == 'm'){	//read memory
			
			in++;
			addr = htoi(&in);
			if(*in++ != ',') goto fail;
			len = htoi(&in);
			if(*in) goto fail;
			out[0] = 0;
			while(len--){
				
				if(!gdb_memAccess(addr++, &c, false)) break;
				sprintf(out + strlen(out), "%02x", c);	
			}
			if (!strlen(out))
				strcpy(out, "e00");
		}
		else if(strcmp(in, "?") == 0){
			
			strcpy(out,"S05");	
		}
		else goto fail;
		
	send_pkt:
	
		return ret;
		
	fail:
		out[0] = 0;
		ret = 0;
		goto send_pkt;
	}
	
	static void sendpacket(int sock, char* packet, int withAck){
		
		unsigned int c;
		unsigned i;
				
		c = 0;
		for(i = 0; i < strlen(packet); i++) c += packet[i];
		memmove(packet + (withAck ? 2 : 1), packet, strlen(packet) + 1);
		if(withAck){
			packet[0] = '+';
			packet[1] = '$';
		}
		else{
			packet[0] = '$';
		}
		sprintf(packet + strlen(packet), "#%02x", c & 0xFF);
		
		//printf("sending packet <<%s>>\n", packet);
		send(sock, packet, strlen(packet), 0);	
	}
	
	static inline void gdbCmdWait(unsigned gdbPort, bool* ss)
	{
		
		static int running = 0;
		static int sock = -1;
		char packet[4096];
		struct timeval tv = {0};
		fd_set set;
		int ret;
		
		if(*ss && running){
			
			strcpy(packet,"S05");
			sendpacket(sock, packet, 0);
			running = 0;	//perform single step
		}
		*ss = false;
		
		if(running){	//check for breakpoints
			
			uint8_t i;
			
			for(i = 0; i < gNumBkpts; i++){
				
				if(cpuGetRegExternal(MIPS_EXT_REG_PC) == gBkpts[i]){
					
				//	printf("bkpt hit: pc=0x%08lX bk=0x%08lX i=%d\n", soc->cpu.regs[15], gBkpts[i], i);
					strcpy(packet,"S05");
					sendpacket(sock, packet, 0);
					running = 0;	//perform breakpoint hit
					break;
				}
			}
		}
		
		if(gdbPort){
			
			if(sock == -1){	//no socket yet - make one
				
				struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(gdbPort)};
				socklen_t sl = sizeof(sa);
				
				inet_aton("127.0.0.1", &sa.sin_addr);
				
				sock = socket(PF_INET, SOCK_STREAM, 0);
				if(sock == -1){
					err_str("gdb socket creation fails: %d", errno);
				}
				
				ret = bind(sock, (struct sockaddr*)&sa, sizeof(sa));
				if(ret){
					err_str("gdb socket bind fails: %d", errno);
				}
				
				fprintf(stderr, "gdb stub listening for connection on port %d\n", gdbPort);
				ret = listen(sock, 1);
				if(ret){
					err_str("gdb socket listen fails: : %d", errno);
				}
				
				ret = accept(sock, (struct sockaddr*)&sa, &sl);
				if(ret == -1){
					err_str("gdb socket accept fails: : %d", errno);
				}
				close(sock);
				sock = ret;
				
				gNumBkpts = 0;
			}
		}
		if(gdbPort){
				
			do{
		
				FD_ZERO(&set);
				FD_SET(sock, &set);
				tv.tv_sec = running ? 0 : 0x00f00000UL;
				do{
					ret = select(sock + 1, &set, NULL, NULL, &tv);
				}while(!ret && !running);
				if(ret < 0){
					err_str("select fails: : %d", errno);
				}
				if(ret > 0){
					char c;
					char* p;
					int len = 0, esc = 0, end = 0;
					
					ret = recv(sock, &c, 1, 0);
					if(ret != 1){
						err_str("failed to receive byte (1)\n");
						exit(0);
					}
					
					if(c == 3){
						strcpy(packet,"S11");
						sendpacket(sock, packet, 0);
						running = 0;	//perform breakpoint hit
					}
					else if(c != '$'){
						//printf("unknown packet header '%c'\n", c);
					}
					else{
						do{
							if(esc){
								c = c ^ 0x20;
								esc = 0;
							}
							else if(c == 0x7d){
								esc = 1;
							}
							
							if(!esc){	//we cannot be here if we're being escaped
								
								packet[len++] = c;
								if(end == 0 && c == '#') end = 2;
								else if(end){
									
									end--;
									if(!end) break;
								}
								
								ret = recv(sock, &c, 1, 0);
								if(ret != 1) err_str("failed to receive byte (2)\n");
							}
						}while(1);
						packet[len] = 0;
						
						memmove(packet, packet + 1, len);
						len -= 4;
						packet[len] = 0;
						ret = interpPacket(p = strdup(packet), packet, ss);
					//	if(ret == 0) printf("how do i respond to packet <<%s>>\n", p);
						if(ret == -1){	//ack it anyways
							char c = '+';
							send(sock, &c, 1, 0);
							running = 1;
						}
						else sendpacket(sock, packet, 1);
						
						free(p);
					}
				}
			}while(!running);
		}
	}

#endif
