/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _FPU_H_
#define _FPU_H_

#include <stdint.h>




struct FpuState {
	
	union {
		double d[16];
		float f[32];
		uint32_t i[32];
	};
	uint32_t fcr;
};

enum FpuOpRet {	//must match asm code in cpuAsm.S
	FpuRetInstrInval,
	FpuRetExcTaken,
	FpuBranchTaken,
	FpuLikelyBranchNotTaken,
	FpuCoprocUseException,
	FpuRetInstrDone,
};


enum FpuOpRet fpuOp(uint32_t instr, uint32_t *cpuRegs, struct FpuState *fpu);



#endif
