/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _CPU_H_
#define _CPU_H_

struct Cpu;

#include <stdbool.h>
#include <stdint.h>



#define MIPS_REG_ZERO	0	//always zero
#define MIPS_REG_AT	1	//assembler use (caller saved)
#define MIPS_REG_V0	2	//return val 0 (caller saved)
#define MIPS_REG_V1	3	//return val 1 (caller saved)
#define MIPS_REG_A0	4	//arg 0 (callee saved)
#define MIPS_REG_A1	5	//arg 1 (callee saved)
#define MIPS_REG_A2	6	//arg 2 (callee saved)
#define MIPS_REG_A3	7	//arg 3 (callee saved)
#define MIPS_REG_T0	8	//temporary 0 (caller saved)
#define MIPS_REG_T1	9	//temporary 1 (caller saved)
#define MIPS_REG_T2	10	//temporary 2 (caller saved)
#define MIPS_REG_T3	11	//temporary 3 (caller saved)
#define MIPS_REG_T4	12	//temporary 4 (caller saved)
#define MIPS_REG_T5	13	//temporary 5 (caller saved)
#define MIPS_REG_T6	14	//temporary 6 (caller saved)
#define MIPS_REG_T7	15	//temporary 7 (caller saved)
#define MIPS_REG_S0	16	//saved 0 (callee saved)
#define MIPS_REG_S1	17	//saved 1 (callee saved)
#define MIPS_REG_S2	18	//saved 2 (callee saved)
#define MIPS_REG_S3	19	//saved 3 (callee saved)
#define MIPS_REG_S4	20	//saved 4 (callee saved)
#define MIPS_REG_S5	21	//saved 5 (callee saved)
#define MIPS_REG_S6	22	//saved 6 (callee saved)
#define MIPS_REG_S7	23	//saved 7 (callee saved)
#define MIPS_REG_T8	24	//temporary 8 (caller saved)
#define MIPS_REG_T9	25	//temporary 9 (caller saved)
#define MIPS_REG_K0	26	//kernel use 0 (??? saved)
#define MIPS_REG_K1	27	//kernel use 0 (??? saved)
#define MIPS_REG_GP	28	//globals pointer (??? saved)
#define MIPS_REG_SP	29	//stack pointer (??? saved)
#define MIPS_REG_FP	30	//frame pointer (??? saved)
#define MIPS_REG_RA	31	//return address (??? saved)
#define MIPS_NUM_REGS	32	//must be power of 2
#define MIPS_EXT_REG_PC		(MIPS_NUM_REGS + 0)	//ONLY FOR external use
#define MIPS_EXT_REG_HI		(MIPS_NUM_REGS + 1)	//ONLY FOR external use
#define MIPS_EXT_REG_LO		(MIPS_NUM_REGS + 2)	//ONLY FOR external use
#define MIPS_EXT_REG_VADDR	(MIPS_NUM_REGS + 3)	//ONLY FOR external use
#define MIPS_EXT_REG_CAUSE	(MIPS_NUM_REGS + 4)	//ONLY FOR external use
#define MIPS_EXT_REG_STATUS	(MIPS_NUM_REGS + 5)	//ONLY FOR external use
#define MIPS_EXT_REG_NTRYLO	(MIPS_NUM_REGS + 6)	//ONLY FOR external use
#define MIPS_EXT_REG_NTRYHI	(MIPS_NUM_REGS + 7)	//ONLY FOR external use




#define MIPS_HYPERCALL	0x4f646776




void cpuInit(uint32_t ramAmount);			//ram amount is advisory
void cpuCycle(uint32_t ramAmount);			//may not return (in embedded case it executes forever)
void cpuIrq(uint_fast8_t idx, bool raise);	//unraise when acknowledged

//for debugging
enum CpuMemAccessType {
	CpuAccessAsKernel,
	CpuAccessAsUser,
	CpuAccessAsCurrent,	//one of the above, picked based on current state
};

uint32_t cpuGetRegExternal(uint8_t reg);
void cpuSetRegExternal(uint8_t reg, uint32_t val);
bool cpuMemAccessExternal(void *buf, uint32_t va, uint_fast8_t sz, bool write, enum CpuMemAccessType type);

uint32_t cpuGetCyCnt(void);

//provided externally
bool cpuExtHypercall(void);

void prTLB(void);
#endif

