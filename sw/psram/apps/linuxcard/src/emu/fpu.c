/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdbool.h>
#include <math.h>
#include "fpu.h"



///XXX
	//#define FPU_LOG
	
	#ifdef FPU_LOG
		#include <stdio.h>
		#include "cpu.h"
	
	#endif
	
	#ifdef FPU_LOG
			
		#define LOG(...) 																			\
			do { 																					\
				fprintf(stderr, "[%08x] = %08xx: ", cpuGetRegExternal(MIPS_EXT_REG_PC), instr);		\
				fprintf(stderr, __VA_ARGS__);														\
			}while(0)
	
	#else
		#define LOG(...)
	#endif
///XXX

#if defined(FPU_SUPPORT_FULL)
	#define FPU_REVISION		0x320
#elif defined(FPU_SUPPORT_MINIMAL)
	#define FPU_REVISION		0
#else
	#error "building fpu wit no fpu on"
#endif

#define SUPPORT_FPU_R4000



//we just do not use these...
#define FCR_C_SHIFT			23
#define FCR_C				(1 << FCR_C_SHIFT)
#define FCR_UNIMPL			0x00020000
#define FCR_INVAL_OP		0x10
#define FCR_CEF_DIV0		0x08
#define FCR_CEF_OVERFLOW	0x04
#define FCR_CEF_UDERFLOW	0x02
#define FCR_CEF_INEXACT		0x01

#define FCR_SHIFT_FLAGS		2
#define FCR_SHIFT_ENABLES	7
#define FCR_SHIFT_CAUSE		12

#define FCR_PEROP_FLAGS		(((FCR_INVAL_OP | FCR_CEF_DIV0 | FCR_CEF_OVERFLOW | FCR_CEF_UDERFLOW | FCR_CEF_INEXACT) << FCR_SHIFT_CAUSE) | FCR_UNIMPL)

static inline uint_fast8_t fpuPrvGetFpRegNumT(uint32_t instr)
{
	return (instr >> 16) & 0x1f;
}

static inline uint_fast8_t fpuPrvGetFpRegNumD(uint32_t instr)
{
	return (instr >> 6) & 0x1f;
}

static inline uint_fast8_t fpuPrvGetFpRegNumS(uint32_t instr)
{
	return (instr >> 11) & 0x1f;
}

static inline uint_fast8_t cpuGetRegNumT(uint32_t instr)
{
	return (instr >> 16) & 0x1f;
}

static inline uint32_t cpuGetRegT(uint32_t instr, uint32_t *cpuRegs)
{
	return cpuRegs[cpuGetRegNumT(instr)];
}

static inline void cpuSetRegT(uint32_t instr, uint32_t val, uint32_t *cpuRegs)
{
	uint_fast8_t rn = cpuGetRegNumT(instr);
	
	if (rn)
		cpuRegs[rn] = val;
}

enum FpuOpRet fpuOp(uint32_t instr, uint32_t *cpuRegs, struct FpuState *fpu)
{
	bool isDouble = false, isFloat = false, isFixed = false;
	uint_fast8_t op;
	
	//the minute anyone enables any trapping we cnanot easily support, we are GTFOing and letting the FPU emulator take it on...
	if (fpu->fcr & ((FCR_INVAL_OP | FCR_CEF_OVERFLOW | FCR_CEF_UDERFLOW | FCR_CEF_INEXACT) << FCR_SHIFT_ENABLES))
		goto inval;
	
	fpu->fcr &=~ FCR_PEROP_FLAGS;
	
	switch ((instr >> 21) & 0x1f) {
		
		case 0:	//MFC
			cpuSetRegT(instr, fpu->i[fpuPrvGetFpRegNumS(instr)], cpuRegs);
			LOG("MFC f%02u -> r%02u (%08x)\r\n", fpuPrvGetFpRegNumS(instr), cpuGetRegNumT(instr), fpu->i[fpuPrvGetFpRegNumS(instr)]);
			return FpuRetInstrDone;
			
		case 2: //CFC
			switch (fpuPrvGetFpRegNumS(instr)) {
				case 0:
					cpuSetRegT(instr, FPU_REVISION, cpuRegs);	//R3000 FPU
					LOG("CFC c%02u -> r%02u (%08x)\r\n", fpuPrvGetFpRegNumS(instr), cpuGetRegNumT(instr), FPU_REVISION);
					return FpuRetInstrDone;
				
				case 30:	//EIR (implementation revision register)
					cpuSetRegT(instr, 0, cpuRegs);
					LOG("CFC c%02u -> r%02u (%08x)\r\n", fpuPrvGetFpRegNumS(instr), cpuGetRegNumT(instr), FPU_REVISION & 0xff);
					return FpuRetInstrDone;
				
				case 31:	//FCR
					cpuSetRegT(instr, fpu->fcr, cpuRegs);
					LOG("CFC c%02u -> r%02u (%08x)\r\n", fpuPrvGetFpRegNumS(instr), cpuGetRegNumT(instr), fpu->fcr);
					return FpuRetInstrDone;
				
				default:
					goto inval;
			}
			break;
		
		case 4:	//MTC
			LOG("MTC r%02u -> f%02u (%08x)\r\n", cpuGetRegNumT(instr), fpuPrvGetFpRegNumS(instr), cpuGetRegT(instr, cpuRegs));
			fpu->i[fpuPrvGetFpRegNumS(instr)] = cpuGetRegT(instr, cpuRegs);
			return FpuRetInstrDone;
		
		case 6:	//CTC
			LOG("CTC r%02u -> c%02u (%08x)\r\n", cpuGetRegNumT(instr), fpuPrvGetFpRegNumS(instr), cpuGetRegT(instr, cpuRegs));
			switch (fpuPrvGetFpRegNumS(instr)) {
				case 0:		//writing reg0 sets LEDs on the FPU board (which we do not have)
					return FpuRetInstrDone;
				
				case 30:	//ultrix likes to write this one too
					return FpuRetInstrDone;
				
				case 31:	//FCR
					fpu->fcr = cpuGetRegT(instr, cpuRegs);
					return FpuRetInstrDone;
				
				default:
					goto inval;
			}
			break;
		
		
		case 8:	//BC1
			LOG("condbr%s on %c, cur is %c\r\n", (instr & 0x00020000) ? "(likely)" : "", ((instr >> 16) & 1) ? 'T' : 'F', (fpu->fcr & FCR_C) ? 'T' : 'F');
			if (((fpu->fcr >> FCR_C_SHIFT) & 1) == ((instr >> 16) & 1))
				return FpuBranchTaken;
			else if (instr & 0x00020000)	//it was a "likely" branch
				return FpuLikelyBranchNotTaken;
			else
				return FpuRetInstrDone;
			
		case 16:
			isFloat = true;
			break;
		
		case 17:
			isDouble = true;
			break;
		
		case 20:
			isFixed = true;
			break;
		
		default:
			goto inval;
	}

	#ifdef FPU_SUPPORT_MINIMAL
		return FpuCoprocUseException;	
	#endif

	switch (op = (instr & 0x3f)) {
	
		case 0b000000:	//ADD.fmt
			if (isDouble) {
				double ret;
				
				ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2] + fpu->d[fpuPrvGetFpRegNumT(instr) / 2];
				
				LOG("d%02u (%f) + d%02u (%f) -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumT(instr) / 2, fpu->d[fpuPrvGetFpRegNumT(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)] + fpu->f[fpuPrvGetFpRegNumT(instr)];
				
				LOG("f%02u (%f) + f%02u (%f) -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumT(instr), fpu->f[fpuPrvGetFpRegNumT(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				return FpuRetInstrInval;
			break;
		
		case 0b000001:	//SUB.fmt
			if (isDouble) {
				double ret;
				
				ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2] - fpu->d[fpuPrvGetFpRegNumT(instr) / 2];
				
				LOG("d%02u (%f) - d%02u (%f) -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumT(instr) / 2, fpu->d[fpuPrvGetFpRegNumT(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)] - fpu->f[fpuPrvGetFpRegNumT(instr)];
				
				LOG("f%02u (%f) + f%02u (%f) -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumT(instr), fpu->f[fpuPrvGetFpRegNumT(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				return FpuRetInstrInval;
			break;
		
		case 0b000010:	//MUL.fmt
			if (isDouble) {
				double ret;
				
				ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2] * fpu->d[fpuPrvGetFpRegNumT(instr) / 2];
				
				LOG("d%02u (%f) * d%02u (%f) -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumT(instr) / 2, fpu->d[fpuPrvGetFpRegNumT(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)] * fpu->f[fpuPrvGetFpRegNumT(instr)];
				
				LOG("f%02u (%f) * f%02u (%f) -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumT(instr), fpu->f[fpuPrvGetFpRegNumT(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				return FpuRetInstrInval;
			break;
		
		case 0b000011:	//DIV.fmt
			if (isDouble) {
				
				double denom = fpu->d[fpuPrvGetFpRegNumT(instr) / 2];
				double ret;
				
				if (!denom) {
					fpu->fcr |= ((FCR_CEF_DIV0 << FCR_SHIFT_CAUSE) | (FCR_CEF_DIV0 << FCR_SHIFT_FLAGS));
					if (fpu->fcr & (FCR_CEF_DIV0 << FCR_SHIFT_ENABLES))
						return FpuRetExcTaken;
				}
				
				ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2] / denom;
				LOG("d%02u (%f) / d%02u (%f) -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumT(instr) / 2, fpu->d[fpuPrvGetFpRegNumT(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float denom = fpu->f[fpuPrvGetFpRegNumT(instr)];
				float ret;
				
				if (!denom) {
					fpu->fcr |= ((FCR_CEF_DIV0 << FCR_SHIFT_CAUSE) | (FCR_CEF_DIV0 << FCR_SHIFT_FLAGS));
					if (fpu->fcr & (FCR_CEF_DIV0 << FCR_SHIFT_ENABLES))
						return FpuRetExcTaken;
				}
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)] / denom;
				LOG("f%02u (%f) / f%02u (%f) -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumT(instr), fpu->f[fpuPrvGetFpRegNumT(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b000101:	//ABS.fmt
			if (isDouble) {
				
				double ret;
				
				ret = fabs(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
				
				LOG("d%02u (%f) ABS -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = fabsf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
				LOG("f%02u (%f) ABS -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b000110:	//MOV.fmt
			if (isDouble) {
				
				double ret;
				
				ret =  fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
				
				LOG("d%02u (%f) MOV -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)];
				LOG("f%02u (%f) MOV -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		
		case 0b000111:	//NEG.fmt
			if (isDouble) {
				
				double ret;
				
				ret = -fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
				
				LOG("d%02u (%f) NEG -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFloat) {
				
				float ret;
				
				ret = -fpu->f[fpuPrvGetFpRegNumS(instr)];
				LOG("f%02u (%f) NEG -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
	
	#ifdef SUPPORT_FPU_R4000
		
		case 0b001100:	//round.W.fmt
			if (isDouble) {
				
				int32_t ret;
				
				ret = round(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
				LOG("d%02u (%f) ROUND -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFloat) {
				
				int32_t ret;
				
				ret = roundf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
				LOG("f%02u (%f) ROUND -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b001101:	//trunc.W.fmt
			if (isDouble) {
				
				int32_t ret;
				
				ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
				LOG("d%02u (%f) TRUNC -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFloat) {
				
				int32_t ret;
				
				ret = fpu->f[fpuPrvGetFpRegNumS(instr)];
				LOG("f%02u (%f) TRUNC -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b001110:	//ceil.W.fmt
			if (isDouble) {
				
				int32_t ret;
				
				ret = ceil(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
				LOG("d%02u (%f) CEIL -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFloat) {
				
				int32_t ret;
				
				ret = ceilf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
				LOG("f%02u (%f) CEIL -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b001111:	//floor.W.fmt
			if (isDouble) {
				
				int32_t ret;
				
				ret = floor(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
				LOG("d%02u (%f) FLOOR -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFloat) {
				
				int32_t ret;
				
				ret = floorf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
				LOG("f%02u (%f) FLOOR -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;	
	
	#endif
	
		case 0b100000:	//CVT.S.fmt
			if (isDouble) {
				
				float ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
				
				LOG("d%02u (%f) CVT.S.D -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFixed) {
				
				float ret = (int32_t)fpu->i[fpuPrvGetFpRegNumS(instr)];
				
				LOG("f%02u (%d) CVT.S.W -> f%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), (int32_t)fpu->i[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->f[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b100001:	//CVT.D.fmt
			if (isFloat) {
				double ret = fpu->f[fpuPrvGetFpRegNumS(instr)];
				
				LOG("f%02u (%f) CVT.D.S -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr) / 2, ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else if (isFixed) {
				double ret = (int32_t)fpu->i[fpuPrvGetFpRegNumS(instr)];
				
				LOG("f%02u (%d) CVT.D.W -> d%02u (%f)\r\n",
					fpuPrvGetFpRegNumS(instr), (int32_t)fpu->i[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->d[fpuPrvGetFpRegNumD(instr) / 2] = ret;
			}
			else
				goto inval;
			break;
		
		case 0b100100:	//CVT.W.fmt
			if (isDouble) {
				
				int32_t ret;
				
				switch (fpu->fcr & 3) {
					case 0:	//round to nearest
						ret = round(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
						break;
					
					case 1:	//round to zero
						ret = fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
						break;
					
					case 2:	//round to +inf
						ret = ceil(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
						break;
					
					case 3:	//round to -inf
						ret = floor(fpu->d[fpuPrvGetFpRegNumS(instr) / 2]);
						break;
					
					default:
						__builtin_unreachable();
						break;
				}
				
				LOG("d%02u (%f) CVT.W.D -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else if (isFloat) {
				
				int32_t ret;
				
				switch (fpu->fcr & 3) {
					case 0:	//round to nearest
						ret = roundf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
						break;
					
					case 1:	//round to zero
						ret = fpu->f[fpuPrvGetFpRegNumS(instr)];
						break;
					
					case 2:	//round to +inf
						ret = ceilf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
						break;
					
					case 3:	//round to -inf
						ret = floorf(fpu->f[fpuPrvGetFpRegNumS(instr)]);
						break;
					
					default:
						__builtin_unreachable();
						break;
				}
				
				LOG("f%02u (%f) CVT.W.S -> f%02u (%d)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					fpuPrvGetFpRegNumD(instr), ret);
				fpu->i[fpuPrvGetFpRegNumD(instr)] = ret;
			}
			else
				goto inval;
			break;
	
		//compares (specializing these might gain speed)
		//see page 670 of r4000 doc!!!
		case 0b110000:	//C.f.fmt
		case 0b110001:	//C.un.fmt
		case 0b110010:	//C.eq.fmt
		case 0b110011:	//C.ueq.fmt
		case 0b110100:	//C.olt.fmt
		case 0b110101:	//C.ult.fmt
		case 0b110110:	//C.ole.fmt
		case 0b110111:	//C.ule.fmt
		case 0b111000:	//C.sf.fmt
		case 0b111001:	//C.ngle.fmt
		case 0b111010:	//C.seq.fmt
		case 0b111011:	//C.ngl.fmt
		case 0b111100:	//C.lt.fmt
		case 0b111101:	//C.nge.fmt
		case 0b111110:	//C.le.fmt
		case 0b111111:	//C.ngt.fmt
		{
			static const char *names[] = {"f", "un", "eq", "ueq", "olt", "ult",  "ole", "ule", "sf", "ngle", "seq", "ngl", "lt", "nge", "le", "ngt", };
			uint_fast8_t cond = 0;
			
			(void)names;
			
			if (isDouble) {
				
				double s = fpu->d[fpuPrvGetFpRegNumS(instr) / 2];
				double t = fpu->d[fpuPrvGetFpRegNumT(instr) / 2];
				
				if (isnan(s) || isnan(t))
					cond += 1;
				else {
					
					if (s == t)
						cond += 2;
					if (s < t)
						cond += 4;
				}
				
				LOG("d%02u (%f) %s d%02u (%f) -> (signal: %u, ret: %u)\r\n",
					fpuPrvGetFpRegNumS(instr) / 2, fpu->d[fpuPrvGetFpRegNumS(instr) / 2],
					names[op & 15],
					fpuPrvGetFpRegNumT(instr) / 2, fpu->d[fpuPrvGetFpRegNumT(instr) / 2],
					((cond & 1) && (op & 0b1000)),
					!!(op & cond));
			}
			else if (isFloat) {
				
				float s = fpu->f[fpuPrvGetFpRegNumS(instr)];
				float t = fpu->f[fpuPrvGetFpRegNumT(instr)];
				
				if (isnan(s) || isnan(t))
					cond += 1;
				else {
					
					if (s == t)
						cond += 2;
					if (s < t)
						cond += 4;
				}
				
				LOG("f%02u (%f) %s f%02u (%f) -> (signal: %u, ret: %u)\r\n",
					fpuPrvGetFpRegNumS(instr), fpu->f[fpuPrvGetFpRegNumS(instr)],
					names[op & 15],
					fpuPrvGetFpRegNumT(instr), fpu->f[fpuPrvGetFpRegNumT(instr)],
					((cond & 1) && (op & 0b1000)),
					!!(op & cond));
			}
			else
				goto inval;
			
			if ((cond & 1) && (op & 0b1000)) {
				
				fpu->fcr |= ((FCR_INVAL_OP << FCR_SHIFT_CAUSE) | (FCR_INVAL_OP << FCR_SHIFT_FLAGS));
				return FpuRetExcTaken;
			}
			
			if (op & cond)
				fpu->fcr |= FCR_C;
			else
				fpu->fcr &=~ FCR_C;			
			break;
		}
		
		default:
			goto inval;
	}
	return FpuRetInstrDone;

inval:
	fpu->fcr |= FCR_UNIMPL;
	return FpuRetExcTaken;
}