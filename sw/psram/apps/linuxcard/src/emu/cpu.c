/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "printf.h"
#include "fpu.h"

#define HYPERCALL			0x4f646776


//#define R4000
#define SUPPORT_LIKELY_BRANCHES	//sert to enable BxxL even on non-R4000
//#define SUPPORT_MOVCC			//set to enable MOVN/MOVZ. technically part of MIPS IV, but our busysbox seems to need them
#define SUPPORT_TRAPCC			//set to eable Tcc/Tcci instrs  even for R3000
//#define SUPPORT_MUL				//set to enable MUL. technically not even present in R4000, but....
//#define SUPPORT_MAC				//set to enable MADD/MADDU/MSUB/MSUBU, even though lacking in R4000
//#define SUPPORT_CLZ				//set to enable CLZ/CLO, even though lacking in R4000
//#define SUPPORT_BITFIELD_OPS	//set to enable INS/EXT, even though lacking in R4000
//#define SUPPORT_EXTEND_OPS		//set to enable SEH/SEB, even though lacking in R4000
//#define SUPPORT_BYTESWAP		//set to enable WSBH, even though lacking in R4000
#define SUPPORT_LL_SC


#include "cpu.h"
#include "mem.h"
#include "decBus.h"

#define NUM_TLB_ENTRIES			64
#define NUM_WIRED_TLB_ENTRIES	8
#define NUM_IRQS				8		//lower 2 are sw irqs

#define TLB_HASH_ENTRIES		128
#define TLB_HASH(x)				((((x) >> 24) ^ ((x) >> 12)) % (TLB_HASH_ENTRIES))

#ifdef R4000
	#define PRID_VALUE				0x0400	//R4000
#else
	#define PRID_VALUE				0x0220	//R3000
#endif

static struct {
	uint32_t regs[MIPS_NUM_REGS];

	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
		struct FpuState fpu;
	#endif
	
	uint32_t pc, npc;
	
	
	//assumes LE
	union{
		struct {
			uint32_t lo, hi;
		};
		uint64_t hilo64;
	};
	
	
	uint8_t inDelaySlot	: 1;
	uint8_t llbit		: 1;
	
	//this is CP0
	uint32_t randomSeed;
	uint32_t index, cause, status, epc, badva, entryHi, entryLo, context;
	
	struct {
		uint32_t va;	//top-aligned, bottom zero
		uint32_t pa;	//top-aligned, bottom zero
		uint8_t asid;
		union{			//i really am a sick person
			struct {
				
				uint8_t rfu	:4;
				uint8_t g	:1;
				uint8_t v	:1;
				uint8_t d	:1;
				uint8_t n	:1;
			};
			uint8_t flagsAsByte;
		};
		
		//hashtable
		int8_t prevIdx, nextIdx;
		
	} tlb[NUM_TLB_ENTRIES];
	
	int8_t tlbHash[TLB_HASH_ENTRIES];
	
} cpu;

#define TLB_ENTRYHI_VA_MASK		0xfffff000
#define TLB_ENTRYHI_ASID_MASK	0x00000fc0
#define TLB_ENTRYHI_ASID_SHIFT	6

#define TLB_ENTRYLO_PA_MASK		0xfffff000
#define TLB_ENTRYLO_N			0x00000800
#define TLB_ENTRYLO_D			0x00000400
#define TLB_ENTRYLO_V			0x00000200
#define TLB_ENTRYLO_G			0x00000100
#define TLB_ENTRYLO_FLAGS_MASK	0x00000ff0	//for flagsAsByte
#define TLB_ENTRYLO_FLAGS_SHIFT	4

#ifdef R4000
	#define CP0_CTX_PTEBASE_MASK	0xff800000
	#define CP0_CTX_PTEBASE_SHIFT	23
	#define CP0_CTX_BADVPN2_MASK	0x007ffffc	//which bits go where ain't clear here...docs conflict
	#define CP0_CTX_BADVPN2_SHIFT	2
#else
	#define CP0_CTX_PTEBASE_MASK	0xffe00000
	#define CP0_CTX_PTEBASE_SHIFT	21
	#define CP0_CTX_BADVPN2_MASK	0x001ffffc
	#define CP0_CTX_BADVPN2_SHIFT	2
#endif





#ifdef R4000

	#define EXC_OFST_KU_TLB_REFILL		0x000
	#define EXC_OFST_NON_KU_TLB_REFILL	0x000
	#define EXC_OFST_EXL				0x180
	#define EXC_OFST_GENERAL			0x180
	#define EXC_OFST_IRQ				0x200
	
#else

	#define EXC_OFST_KU_TLB_REFILL		0x000
	#define EXC_OFST_NON_KU_TLB_REFILL	0x080
	#define EXC_OFST_GENERAL			0x080
	#define EXC_OFST_IRQ				0x080
	
#endif




//MD00090-2B-MIPS32PRA-AFP-06.02.pdf page 189
#define CP0_STATUS_CU(x)		(0x10000000 << (x))
#define CP0_STATUS_CU_MASK		0xf0000000
#define CP0_STATUS_CU_SHIFT		28
#define CP0_STATUS_RE			0x02000000
#define CP0_STATUS_BEV			0x00400000
#define CP0_STATUS_TS			0x00200000
#define CP0_STATUS_PE			0x00100000
#define CP0_STATUS_CM			0x00080000
#define CP0_STATUS_PZ			0x00040000
#define CP0_STATUS_SWC			0x00020000
#define CP0_STATUS_ISC			0x00010000
#define CP0_STATUS_IM(x)		(0x00000100 << (x))
#define CP0_STATUS_IM_MASK		0x0000ff00
#define CP0_STATUS_IM_BITLEN	8
#define CP0_STATUS_IM_SHIFT		8
#ifdef R4000
	#define CP0_STATUS_UM		0x00000020	//set for user mode
	#define CP0_STATUS_ERL		0x00000004
	#define CP0_STATUS_EXL		0x00000002
#else
	#define CP0_STATUS_KUO		0x00000020
	#define CP0_STATUS_IEO		0x00000010
	#define CP0_STATUS_KUP		0x00000008
	#define CP0_STATUS_IEP		0x00000004
	#define CP0_STATUS_KUC		0x00000002	//set when in userspace
#endif
#define CP0_STATUS_IE_SHIFT		0
#define CP0_STATUS_IE			(1 << CP0_STATUS_IE_SHIFT)

//MD00090-2B-MIPS32PRA-AFP-06.02.pdf page 209
#define CP0_CAUSE_BD			0x80000000
#define CP0_CAUSE_BD_SHIFT		31
#define CP0_CAUSE_CE_MASK		0x30000000
#define CP0_CAUSE_CE_SHIFT		28
#define CP0_CAUSE_IV			0x00800000
#define CP0_CAUSE_WP			0x00400000
#define CP0_CAUSE_FDCI			0x00200000
#define CP0_CAUSE_IP(x)			(0x00000100 << (x))	//botton 2 bits are SW RW, others SW RO
#define CP0_CAUSE_IP_MASK		0x0000ff00
#define CP0_CAUSE_IP_SHIFT		8
#define CP0_CAUSE_EXC_COD_MASK	0x0000007c
#define CP0_CAUSE_EXC_COD_SHIFT	2

#define CP0_EXC_COD_IRQ			0	//IRQ happened
#define CP0_EXC_COD_MOD			1	//TLB modified:	store to a valid entry with D bit clear
#define CP0_EXC_COD_TLBL		2	//TLB exception on load: no matching entry found, it was a load or an instruction fetch
#define CP0_EXC_COD_TLBS		3	//TLB exception on store: no matching entry found, it was a store
#define CP0_EXC_COD_ADEL		4	//unaligned access or user access to kernel map, it was a load or an instruction fetch
#define CP0_EXC_COD_ADES		5	//unaligned access or user access to kernel map, it was a store
#define CP0_EXC_COD_IBE			6	//bus error on instruction fetch
#define CP0_EXC_COD_DBE			7	//bus error on data access
#define CP0_EXC_COD_SYS			8	//syscall
#define CP0_EXC_COD_BP			9	//BREAK instr
#define CP0_EXC_COD_RI			10	//invalid instr
#define CP0_EXC_COD_CPU			11	//coprocessor unusable
#define CP0_EXC_COD_OV			12	//arith overflow
#define CP0_EXC_COD_TR			13	//TRAP
#define CP0_EXC_COD_MSAFPE		14	//MSA Floating-Point exception
#define CP0_EXC_COD_FPE			15	//Floating-Point exception
#define CP0_EXC_COD_TLBRI		19	//read inhibit caught a read
#define CP0_EXC_COD_TLBXI		20	//execution inhibit caught an attempt
#define CP0_EXC_COD_MSADIS		21	//MSA Disabled exception
#define CP0_EXC_COD_WATCH		23	//watchpoint hit

#define CP0_EXC_COD_REFILL_REQ	0x20	//not sent to CP0, used to tell apart tlb refill from tlb invalid exceptions in cpuPrvTakeException()
#define CP0_EXC_COD_KU			0x40	//same use as below

/*
	exceptions and how we take them  (vectors given are for r4000, r3000 uses 0x000 for tlb refill on kuseg and 80 for all else)

	address error:
		unaligned access
		user access to kernel map
		Cause Register ExcCode Value
			AdEL: Reference was a load or an instruction fetch
			AdES: Reference was a store
		BadVA failing addr
		vector 0x180
	
	tlb refill:
		no matching entry found
		Cause Register ExcCode Value
			TLBL: Reference was a load or an instruction fetch
			TLBS: Reference was a store
		BadVA failing addr
		EntryHi.VPN2 set
		vector 0x000
	
	tlb invalid:
		a matching entry exists but V bit is clear
		Cause Register ExcCode Value
			TLBL: Reference was a load or an instruction fetch
			TLBS: Reference was a store
		BadVA failing addr
		EntryHi.VPN2 set
		vector 0x180
	
	tlb modified:
		store to a valid entry with D bit clear
		Cause Register ExcCode Value
			Mod
		BadVA failing addr
		EntryHi.VPN2 set
		vector 0x180
	
	bus error:
		access to invalid PA
		Cause Register ExcCode Value
			IBE: Error on an instruction reference
			DBE: Error on a data reference
		vector 0x180
	
	integer overflow:
		as promised
		Cause Register ExcCode Value
			Ov
		vector 0x180
	
	trap:
		as promised
		Cause Register ExcCode Value
			Tr 
		vector 0x180
	
	syscall:
		SYSCALL instr
		Cause Register ExcCode Value
			Sys 
		vector 0x180
	
	breakpoint:
		BREAK instr
		Cause Register ExcCode Value
			Bp 
		vector 0x180
	
	reserved instr exception:
		invalid instr
		Cause Register ExcCode Value
			RI 
		vector 0x180
	
	coprocessor unusable instr:
		cop0 from userspace
		copX when disabled
		Cause Register ExcCode Value
			CpU
			CE = unit number of coproc
		vector 0x180
	
	flotnig point exc (r400 only)
		CauseRegister ExcCode Value
			FPE
		vector 0x180
		
	IRQ:
		Register ExcCode Value
			Int
			IP = pending irqs
		vector
			General exception vector (offset 0x180) if the IV bit in the Cause register is zero.
			Interrupt vector (offset 0x200) if the IV bit in the Cause register is one
*/


static void cpuPrvIcacheFlushEntire(void);
static void cpuPrvIcacheFlushPage(uint32_t va);




static bool cpuPrvIrqsPending(void)
{
	return !!(((cpu.status & CP0_STATUS_IM_MASK) >> CP0_STATUS_IM_SHIFT) & ((cpu.cause & CP0_CAUSE_IP_MASK) >> CP0_CAUSE_IP_SHIFT));
}

void cpuIrq(uint_fast8_t idx, bool raise)
{
	if (idx < NUM_IRQS) {
		
		if (raise)
			cpu.cause |= CP0_CAUSE_IP(idx);
		else
			cpu.cause &=~ CP0_CAUSE_IP(idx);
	}
}

static void cpuPrvTakeException(uint_fast8_t excCode)
{
	uint32_t vector = 0x80000000;
	//	pr("take excep: %02x\n", excCode);
#ifdef R4000
	if (cpu.status & CP0_STATUS_EXL)
		vector += EXC_OFST_EXL;
	else 
#endif
	switch (excCode) {
		case CP0_EXC_COD_IRQ:
			vector += (cpu.cause & CP0_CAUSE_IV) ? EXC_OFST_IRQ : EXC_OFST_GENERAL;
			break;
		
		case CP0_EXC_COD_REFILL_REQ | CP0_EXC_COD_TLBL:
		case CP0_EXC_COD_REFILL_REQ | CP0_EXC_COD_TLBS:
			excCode &=~ CP0_EXC_COD_REFILL_REQ;
			vector += EXC_OFST_NON_KU_TLB_REFILL;
			break;
		
		case CP0_EXC_COD_REFILL_REQ | CP0_EXC_COD_KU | CP0_EXC_COD_TLBL:
		case CP0_EXC_COD_REFILL_REQ | CP0_EXC_COD_KU | CP0_EXC_COD_TLBS:
			excCode &=~ (CP0_EXC_COD_REFILL_REQ | CP0_EXC_COD_KU);
			vector += EXC_OFST_KU_TLB_REFILL;
			break;
		
		default:
			vector += EXC_OFST_GENERAL;
			break;
	}
#ifdef R4000
	if (!(cpu.status & CP0_STATUS_EXL)) {
		if (cpu.inDelaySlot) {
			
			cpu.epc = cpu.pc - 4;
			cpu.cause |= CP0_CAUSE_BD;
		}
		else {
			
			cpu.epc = cpu.pc;
			cpu.cause &=~ CP0_CAUSE_BD;
		}
		cpu.status |= CP0_STATUS_EXL;
	}
#else

	if (cpu.inDelaySlot) {
		
		cpu.epc = cpu.pc - 4;
		cpu.cause |= CP0_CAUSE_BD;
	}
	else {
		
		cpu.epc = cpu.pc;
		cpu.cause &=~ CP0_CAUSE_BD;
	}
	cpu.status =
		(cpu.status &~ (CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE)) |
		((cpu.status & (CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE)) << 2);

#endif
	
	cpu.cause = (cpu.cause &~ CP0_CAUSE_EXC_COD_MASK) | ((((uint32_t)excCode) << CP0_CAUSE_EXC_COD_SHIFT) & CP0_CAUSE_EXC_COD_MASK);
	
	cpu.inDelaySlot = false;
	cpu.pc = vector;
	cpu.npc = vector + 4;
}


static inline void cpuPrvSetBadVA(uint32_t va)
{
	cpu.badva = va;
	cpu.context = (cpu.context & CP0_CTX_PTEBASE_MASK) | (((va >> 12) << CP0_CTX_BADVPN2_SHIFT) & CP0_CTX_BADVPN2_MASK);
}

static inline void cpuPrvSetEntryHiVa(uint32_t va)
{
	cpu.entryHi = (cpu.entryHi &~ TLB_ENTRYHI_VA_MASK) | (va & TLB_ENTRYHI_VA_MASK);
}

static inline void cpuPrvTakeAddressError(uint32_t va, bool wasWrite)
{
//	err_str(" EXC: AdrErr @0x%08x\n", va);
	cpuPrvSetBadVA(va);
	cpuPrvTakeException(wasWrite ? CP0_EXC_COD_ADES : CP0_EXC_COD_ADEL);
}

static inline void cpuPrvTakeTlbRefillExc(uint32_t va, bool wasWrite)
{
	uint_fast8_t cause = wasWrite ? CP0_EXC_COD_TLBS : CP0_EXC_COD_TLBL;
	
	cause |= CP0_EXC_COD_REFILL_REQ;
	if (va < 0x80000000)
		cause |= CP0_EXC_COD_KU;
	
//	err_str(" EXC: Refill @0x%08x\r\n", va);
	cpuPrvSetBadVA(va);
	cpuPrvSetEntryHiVa(va);
	cpuPrvTakeException(cause);
}

static inline void cpuPrvTakeTlbInvalidExc(uint32_t va, bool wasWrite)
{
//	err_str(" EXC: Inval  @0x%08x\n", va);
	cpuPrvSetBadVA(va);
	cpuPrvSetEntryHiVa(va);
	cpuPrvTakeException(wasWrite ? CP0_EXC_COD_TLBS : CP0_EXC_COD_TLBL);
}

static inline void cpuPrvTakeTlbModifiedExc(uint32_t va)
{
	cpuPrvSetBadVA(va);
	cpuPrvSetEntryHiVa(va);
	cpuPrvTakeException(CP0_EXC_COD_MOD);
}

static inline void cpuPrvTakeBusError(uint32_t pa, bool wasInstrFetch)
{
	fprintf(stderr, "%cBUS error pa 0x%08x at pc 0x%08x ra 0x%08x\n", wasInstrFetch ? 'i' : 'd', pa, cpu.pc, cpu.regs[MIPS_REG_RA]);
	
	decReportBusErrorAddr(pa);
	cpuPrvTakeException(wasInstrFetch ? CP0_EXC_COD_IBE : CP0_EXC_COD_DBE);
}

static inline void cpuPrvTakeIntegerOverflowExc(void)
{
	cpuPrvTakeException(CP0_EXC_COD_OV);
}

static inline void cpuPrvTakeTrapExc(void)
{
	cpuPrvTakeException(CP0_EXC_COD_TR);
}

static inline void cpuPrvTakeSyscallExc(void)
{
	cpuPrvTakeException(CP0_EXC_COD_SYS);
}

static inline void cpuPrvTakeBreakpointExc(void)
{
	cpuPrvTakeException(CP0_EXC_COD_BP);
}

static inline void cpuPrvTakeReservedInstrExc(void)
{
	cpuPrvTakeException(CP0_EXC_COD_RI);
}

static inline void cpuPrvTakeCoprocUnusableExc(uint_fast8_t cpNo)
{
	cpu.cause = (cpu.cause &~ CP0_CAUSE_CE_MASK) | ((((uint32_t)cpNo) << CP0_CAUSE_CE_SHIFT) & CP0_CAUSE_CE_MASK);
	cpuPrvTakeException(CP0_EXC_COD_CPU);
}

static inline void cpuPrvTakeFloatingPointExc(void)
{
	#ifdef R4000
		cpuPrvTakeException(CP0_EXC_COD_FPE);
	#else
		cpuPrvTakeException(CP0_EXC_COD_RI);
	#endif
}

static inline void cpuPrvTakeIrq(void)
{
	cpuPrvTakeException(CP0_EXC_COD_IRQ);
}

uint32_t cpuGetRegExternal(uint8_t reg)
{
	if (reg < MIPS_NUM_REGS)
		return cpu.regs[reg];
	else if (reg == MIPS_EXT_REG_PC)
		return cpu.pc;
	else if (reg == MIPS_EXT_REG_HI)
		return cpu.hi;
	else if (reg == MIPS_EXT_REG_LO)
		return cpu.lo;
	else if (reg == MIPS_EXT_REG_VADDR)
		return cpu.badva;
	else if (reg == MIPS_EXT_REG_CAUSE)
		return cpu.cause;
	else if (reg == MIPS_EXT_REG_STATUS)
		return cpu.status;
	else
		err_str("Unknown reg read");
	
	return 0;
}

void cpuSetRegExternal(uint8_t reg, uint32_t val)
{
	if (!reg)
		err_str("Reg 0 external write");
	else if (reg < MIPS_NUM_REGS)
		cpu.regs[reg] = val;
	else if (reg == MIPS_EXT_REG_PC)
		cpu.pc = val;
	else if (reg == MIPS_EXT_REG_HI)
		cpu.hi = val;
	else if (reg == MIPS_EXT_REG_LO)
		cpu.lo = val;
	else if (reg == MIPS_EXT_REG_VADDR)
		cpu.badva = val;
	else if (reg == MIPS_EXT_REG_CAUSE)
		cpu.cause = val;
	else if (reg == MIPS_EXT_REG_STATUS)
		cpu.status = val;
	else
		err_str("Unknown reg set");
}

static inline uint_fast8_t cpuGetRegNumS(uint32_t instr)
{
	return (instr >> 21) & (MIPS_NUM_REGS - 1);
}

static inline uint_fast8_t cpuGetRegNumT(uint32_t instr)
{
	return (instr >> 16) & (MIPS_NUM_REGS - 1);
}

static inline uint_fast8_t cpuGetRegNumA(uint32_t instr)
{
	return (instr >> 6) & (MIPS_NUM_REGS - 1);
}

static inline uint_fast8_t cpuGetRegNumD(uint32_t instr)
{
	return (instr >> 11) & (MIPS_NUM_REGS - 1);
}

static inline uint32_t cpuGetRegS(uint32_t instr)
{
	return cpu.regs[cpuGetRegNumS(instr)];
}

static inline uint32_t cpuGetRegT(uint32_t instr)
{
	return cpu.regs[cpuGetRegNumT(instr)];
}

static inline void cpuSetRegT(uint32_t instr, uint32_t val)
{
	uint_fast8_t rn = cpuGetRegNumT(instr);
	
	if (rn)
		cpu.regs[rn] = val;
}

static inline void cpuSetRegD(uint32_t instr, uint32_t val)
{
	uint_fast8_t rn = cpuGetRegNumD(instr);
	
	if (rn)
		cpu.regs[rn] = val;
}

static inline uint32_t cpuGetUImm(uint32_t instr)
{
	return (uint16_t)instr;
}

static inline int32_t cpuGetSImm(uint32_t instr)
{
	return (int32_t)(int16_t)instr;
}

static void cpuPrvTlbHashRemove(uint_fast8_t idx)
{
	if (cpu.tlb[idx].nextIdx >= 0)
		cpu.tlb[cpu.tlb[idx].nextIdx].prevIdx = cpu.tlb[idx].prevIdx;
	
	if (cpu.tlb[idx].prevIdx >= 0)
		cpu.tlb[cpu.tlb[idx].prevIdx].nextIdx = cpu.tlb[idx].nextIdx;
	else
		cpu.tlbHash[TLB_HASH(cpu.tlb[idx].va)] = cpu.tlb[idx].nextIdx;
	
}

static void cpuPrvTlbHashAdd(uint_fast8_t idx)
{
	uint_fast8_t bucket = TLB_HASH(cpu.tlb[idx].va);
	int_fast8_t curPtr;
	
	cpu.tlb[idx].nextIdx = curPtr = cpu.tlbHash[bucket];
	cpu.tlb[idx].prevIdx = -1;
	cpu.tlbHash[bucket] = idx;
	if (curPtr >= 0)
		cpu.tlb[curPtr].prevIdx = idx;
}

static int_fast8_t cpuPrvTlbHashSearch(uint32_t pageVa)
{
	uint_fast8_t curAsid = (cpu.entryHi & TLB_ENTRYHI_ASID_MASK) >> TLB_ENTRYHI_ASID_SHIFT;
	int_fast8_t idx;
	
	for (idx = cpu.tlbHash[TLB_HASH(pageVa)]; idx >= 0; idx = cpu.tlb[idx].nextIdx) {
		
		//VA must match
		if (cpu.tlb[idx].va != pageVa)
			continue;
		
		//ASID must match or entry must be global
		if (cpu.tlb[idx].asid != curAsid && !cpu.tlb[idx].g)
			continue;
		
		return idx;
	}
	
	return -1;
}

static void cpuPrvMaybeAsidChanded(uint32_t prevVal)
{
	(void)prevVal;
	if ((prevVal ^ cpu.entryHi) & TLB_ENTRYHI_ASID_MASK) {
		
		//changed
		cpuPrvIcacheFlushEntire();
	}
}

static void cpuPrvTlbr(void)
{
	uint_fast8_t index = ((cpu.index >> 8) & 0x3f) % NUM_TLB_ENTRIES;
	uint32_t prevVal = cpu.entryHi;
	
	cpu.entryHi = cpu.tlb[index].va | (((uint32_t)cpu.tlb[index].asid) << TLB_ENTRYHI_ASID_SHIFT);
	cpu.entryLo = cpu.tlb[index].pa | (((uint32_t)cpu.tlb[index].flagsAsByte) << TLB_ENTRYLO_FLAGS_SHIFT);
	
	cpuPrvMaybeAsidChanded(prevVal);
}

static void cpuPrvTlbWrite(uint_fast8_t index)
{
//	cpuPrvIcacheFlushPage(cpu.tlb[index].va);
		
	cpuPrvTlbHashRemove(index);
	
	cpu.tlb[index].va = cpu.entryHi & TLB_ENTRYHI_VA_MASK;
	cpu.tlb[index].asid = (cpu.entryHi & TLB_ENTRYHI_ASID_MASK) >> TLB_ENTRYHI_ASID_SHIFT;
	cpu.tlb[index].pa = cpu.entryLo & TLB_ENTRYLO_PA_MASK;
	cpu.tlb[index].flagsAsByte = (cpu.entryLo & TLB_ENTRYLO_FLAGS_MASK) >> TLB_ENTRYLO_FLAGS_SHIFT;
	
	cpuPrvTlbHashAdd(index);
	
	cpuPrvIcacheFlushEntire();
	//XXX: flush old & new page should work, but does not. i am too lazy to track it down
	//cpuPrvIcacheFlushPage(cpu.tlb[index].va);
}

static void cpuPrvTlbwi(void)
{
	uint_fast8_t index = ((cpu.index >> 8) & 0x3f) % NUM_TLB_ENTRIES;
	
	cpuPrvTlbWrite(index);
}

static uint_fast8_t cpuPrvRefreshRandom(void)
{
	uint32_t rnd = cpu.randomSeed;
	rnd *= 214013;
	rnd += 2531011;
	cpu.randomSeed = rnd;
	
	rnd >>= 24;
	rnd %= (NUM_TLB_ENTRIES - NUM_WIRED_TLB_ENTRIES);
	rnd += NUM_WIRED_TLB_ENTRIES;
	
	return rnd;
}

static void cpuPrvTlbwr(void)
{
	cpuPrvTlbWrite(cpuPrvRefreshRandom());
}

static void cpuPrvTlbp(void)
{
	int_fast8_t idx = cpuPrvTlbHashSearch(cpu.entryHi & TLB_ENTRYHI_VA_MASK);
	
	if (idx < 0)
		cpu.index = 0x80000000;
	else
		cpu.index = ((uint32_t)idx) << 8;
	return;
}

static bool cpuPrvIsInKernelMode(void)
{
#ifdef R4000
	return (cpu.status & (CP0_STATUS_ERL | CP0_STATUS_EXL)) || !(cpu.status & CP0_STATUS_UM);
#else
	return !(cpu.status & CP0_STATUS_KUC);
#endif
}

static bool cpuPrvMemTranslate(uint32_t *paP, uint32_t va, bool write)
{
	int_fast8_t idx;
	
#if 0
	if (va == 0xafc36100) {
	  pr(" memtranslate hit at %08x\n", va);
	  pr(" switch val: %02d\n", va >> 29);
	  pr(" isinkernelmode: %d\n", cpuPrvIsInKernelMode());
	  pr(" paP: %08x\n", va &~ 0xe0000000);
	  cpuPrvTakeAddressError(va, false);
	  return false;
	}
#endif


	switch (va >> 29) {
		case 0:
		case 1:
		case 2:
		case 3:	//kuseg
#ifdef R4000
			if (cpu.status & CP0_STATUS_ERL) {
				*paP = va;
				return true;
			}
#endif
			break;
		
		case 4:	//kseg0
		case 5:	//kseg1
			if (!cpuPrvIsInKernelMode()) {
	//			fprintf(stderr, "addr err 1 0x%08x\n", va);
				cpuPrvTakeAddressError(va, false);
				return false;
			}
			*paP = va &~ 0xe0000000;
			return true;
		
		case 6:	//ksseg
		case 7:	//kseg3
			if (!cpuPrvIsInKernelMode()) {
	//			fprintf(stderr, "addr err 2 0x%08x\n", va);
				cpuPrvTakeAddressError(va, false);
				return false;
			}
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	//we need to consult the TLB
	idx = cpuPrvTlbHashSearch(va & TLB_ENTRYHI_VA_MASK);
	if (idx < 0) {
		cpuPrvTakeTlbRefillExc(va, write);
	//	fprintf(stderr, "refill exc 0x%08x\n", va);
	}
	else if (!cpu.tlb[idx].v){
		cpuPrvTakeTlbInvalidExc(va, write);
	//	fprintf(stderr, "inval exc 0x%08x\n", va);
	}
	else if (write && !cpu.tlb[idx].d) {
	//	fprintf(stderr, "modif exc 0x%08x\n", va);
		cpuPrvTakeTlbModifiedExc(va);
	}
	else {
		
		*paP = cpu.tlb[idx].pa | (va &~ TLB_ENTRYHI_VA_MASK);
		return true;
	}
	
	return false;
}

#define ICACHE_LINE_SZ	32	//in bytes
#define ICACHE_NUM_SETS	32
#define ICACHE_NUM_WAYS	2



struct IcacheLine {
	uint32_t addr;	//kept as LSRed by ICACHE_LINE_SIZE, so 0xfffffffe is a valid "empty "sentinel
	uint8_t icache[ICACHE_LINE_SZ];
} mIcache[ICACHE_NUM_SETS][ICACHE_NUM_WAYS];


static void __attribute__((used)) cpuPrvIcacheFlushEntire(void)
{
	memset(mIcache, 0xff, sizeof(mIcache));
}

static void __attribute__((used)) cpuPrvIcacheFlushPage(uint32_t va)
{
	struct IcacheLine *line;
	uint_fast16_t i;
		
	line = mIcache[(va / ICACHE_LINE_SZ) % ICACHE_NUM_SETS];
	
	for (i = 0; i < ICACHE_NUM_WAYS; i++, line++) {
		
		if (line->addr == va / ICACHE_LINE_SZ)
			line->addr = 0xffffffff;
	}
}

static bool __attribute__((used)) cpuPrvInstrFetchCached(uint32_t *instrP)	//if false, do nothing, all has been handled
{
	uint32_t va = cpu.pc, pa;
	struct IcacheLine *line;
	uint_fast16_t i, set;
	static unsigned rng = 1;

//pretty hard to do this, so let's not check
//	if (va & 3) {
//		
//		cpuPrvTakeAddressError(va, false);
//		return false;
//	}
	
	set = (va / ICACHE_LINE_SZ) % ICACHE_NUM_SETS;
	line = mIcache[set];
	
	for (i = 0; i < ICACHE_NUM_WAYS; i++, line++) {
		
		if (line->addr == va / ICACHE_LINE_SZ) {

			goto hit;
		}
	}
	
	//miss
	line = mIcache[set];
	
	rng *= 214013;
	rng += 2531011;
	line += rng % ICACHE_NUM_WAYS;
		
	if (!cpuPrvMemTranslate(&pa, va, false))
		return false;
	
	pa /= ICACHE_LINE_SZ;
	pa *= ICACHE_LINE_SZ;
	
	if (!memAccess(pa, ICACHE_LINE_SZ, false, line->icache)) {
		cpuPrvTakeBusError(pa, true);
		return false;
	}
	line->addr = va / ICACHE_LINE_SZ;
	
hit:
	*instrP = *(uint32_t*)(&line->icache[(va % ICACHE_LINE_SZ)]);	//god, i hope gcc optimizes this wel...
	return true;
}

static bool __attribute__((used)) cpuPrvInstrFetch(uint32_t *instrP)	//if false, do nothing, all has been handled
{
	uint32_t va = cpu.pc, pa;
	
	if (!cpuPrvMemTranslate(&pa, va, false))
		return false;
	
	if (!memAccess(pa, 4, false, instrP)) {
		
		cpuPrvTakeBusError(pa, true);
		return false;
	}
	
	return true;
}

static bool cpuPrvDataAccess(void* buf, uint32_t va, uint_fast8_t sz, bool write)
{
	uint32_t pa;
	
	if (va & (sz - 1)) {
		
		cpuPrvTakeAddressError(va, write);
		return false;
	}
	
	if (!cpuPrvMemTranslate(&pa, va, write))
		return false;


	if (cpu.status & CP0_STATUS_ISC) {
		
		//XXX: this makes cache sizing algos work...badly
		static uint32_t lastWrite;
		
		//weird mode. see r3000 doc for this, this might need adjustment for R4000
		#ifdef R4000
			#error "this might need adjustment"
		#endif
	
		if (write) {
			
			if (sz == 4)
				lastWrite = *(uint32_t*)buf;
			cpuPrvIcacheFlushEntire();
		}
		else {
			switch (sz) {
				case 1:
					*(uint8_t*)buf = lastWrite;
					break;
				case 2:
					*(uint16_t*)buf = lastWrite;
					break;
				case 4:
					*(uint32_t*)buf = lastWrite;
					break;
				case 8:
					*(uint64_t*)buf = lastWrite;
					break;
			}
		}
		return true;
	}

	if (memAccess(pa, sz, write, buf))
		return true;
	
	cpuPrvTakeBusError(pa, false);

	return false;
}

bool cpuMemAccessExternal(void *buf, uint32_t va, uint_fast8_t sz, bool write, enum CpuMemAccessType type)
{
	uint_fast8_t i, curAsid;
	uint32_t pageVa, pa;
		
	
	switch (va >> 29) {
		case 0:
		case 1:
		case 2:
		case 3:	//kuseg
#ifdef R4000
			if ((cpu.status & CP0_STATUS_ERL) && (type == CpuAccessAsKernel || type == CpuAccessAsCurrent)) {
				pa = va;
				goto resolved;
			}
#endif
			break;
		
		case 4:	//kseg0
		case 5:	//kseg1
			if (type != CpuAccessAsKernel && (!cpuPrvIsInKernelMode() || type != CpuAccessAsCurrent))
				return false;
			pa = va &~ 0xe0000000;
			goto resolved;
		
		case 6:	//ksseg
		case 7:	//kseg3
			if (type != CpuAccessAsKernel && (!cpuPrvIsInKernelMode() || type != CpuAccessAsCurrent))
				return false;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	//we need to consult the TLB
	pageVa = va & TLB_ENTRYHI_VA_MASK;
	curAsid = (cpu.entryHi & TLB_ENTRYHI_ASID_MASK) >> TLB_ENTRYHI_ASID_SHIFT;
	
	for (i = 0; i < NUM_TLB_ENTRIES; i++) {
		
		//VA must match
		if (cpu.tlb[i].va != pageVa)
			continue;
		
		//ASID must match or entry must be global
		if (cpu.tlb[i].asid != curAsid && !cpu.tlb[i].g)
			continue;
		
		//is it valid?
		if (!cpu.tlb[i].v)
			return false;
		
		//is it writeable (if needed)
		if (write && !cpu.tlb[i].d)
			return false;
		
		pa = cpu.tlb[i].pa | (va &~ TLB_ENTRYHI_VA_MASK);
		goto resolved;
	}
	return false;
	
resolved:
	return memAccess(pa, sz, write, buf);
}



static bool cpuPrvCopAccess(uint_fast8_t cpNo)
{
	if (cpNo) {
		if (cpu.status & CP0_STATUS_CU(cpNo))
			return true;
	}
	else if (cpuPrvIsInKernelMode())
		return true;
	
	cpuPrvTakeCoprocUnusableExc(cpNo);
	return false;
}

static void cpuPrvNoBranchTaken(void)
{
	cpu.pc = cpu.npc;
	cpu.npc += 4;
	cpu.inDelaySlot = false;
}

static void cpuPrvBranchTo(uint32_t to)
{
	cpu.pc = cpu.npc;
	cpu.npc = to;
	cpu.inDelaySlot = true;
}

#if defined(R4000) || defined(SUPPORT_LIKELY_BRANCHES)
	static void cpuPrvSkipNextInstr(void)
	{
		cpu.pc = cpu.npc + 4;
		cpu.npc += 8;
		cpu.inDelaySlot = false;
	}
#endif

static bool report = 0;
//static bool report = 1;

void cpuReportCy(void)
{
	report = 1 - report;
}

uint32_t whileCount = 3000;//1800;//1696 //362;
uint32_t cycleCount = 0;

void cpuCycle(uint32_t ramAmount)
{
	uint32_t i32a, i32b, i32c, i32d;
	uint32_t instr;
	uint16_t i16;
	uint8_t i8;
	
	(void)ramAmount;

	//handling interrupts while an instr in branch delay slot is executing is slow (emulation required)
	//to make life easier we do not report IRQs in the delay slot
	if (!cpu.inDelaySlot &&
		(cpu.status & CP0_STATUS_IE) &&
#ifdef R4000
		!(cpu.status & CP0_STATUS_EXL) &&
#endif
		cpuPrvIrqsPending()) {
		
		return cpuPrvTakeIrq();
	}
	
	if (!cpuPrvInstrFetchCached(&instr))
		return;
		
	if (whileCount == 0) report = 1; else report = 0;

	if (report) {
		int i;
		fprintf(stderr, "[%08X]=%08X {", cpu.pc, instr);
		for (i = 0; i < 32; i++) {
			if (!(i & 7))
				fprintf(stderr, "%u: ", i);
			fprintf(stderr, " %08X", cpu.regs[i]);
		}
		fprintf(stderr, "}\n");
	}

	//if ((whileCount--) == 0) while (1) {}
	//printf("c: %07d i: %08x sw: %d\r\n", cycleCount, instr, instr >> 26);
	cycleCount++;
	//if (instr == HYPERCALL) printf("Hypercall at: %d\r\n", cycleCount);
	
	switch (instr >> 26) {
		case 0:
			switch (instr & 0x3f) {
				case 0: //SLL
					cpuSetRegD(instr, cpuGetRegT(instr) << cpuGetRegNumA(instr));
					break;
				
				case 2:	//SRL
					cpuSetRegD(instr, cpuGetRegT(instr) >> cpuGetRegNumA(instr));
					break;
				
				case 3: //SRA
					cpuSetRegD(instr, ((int32_t)cpuGetRegT(instr)) >> cpuGetRegNumA(instr));
					break;
				
				case 4: //SLLV
					cpuSetRegD(instr, cpuGetRegT(instr) << (0x1F & cpuGetRegS(instr)));
					break;
				
				case 6:	//SRLV
					cpuSetRegD(instr, cpuGetRegT(instr) >> (0x1F & cpuGetRegS(instr)));
					break;
				
				case 7: //SRAV
					cpuSetRegD(instr, ((int32_t)cpuGetRegT(instr)) >> (0x1F & cpuGetRegS(instr)));
					break;
				
				case 8: //JR
					return cpuPrvBranchTo(cpuGetRegS(instr));
				
				case 9: //JALR
				
					i32a = cpuGetRegS(instr);
					cpuSetRegD(instr, cpu.pc + 8);
					return cpuPrvBranchTo(i32a);
	#ifdef SUPPORT_MOVCC
				case 10: //MOVZ
					if (!cpuGetRegT(instr))
						cpuSetRegD(instr, cpuGetRegS(instr));
					break;
	
				case 11: //MOVN
					if (cpuGetRegT(instr))
						cpuSetRegD(instr, cpuGetRegS(instr));
					break;			
	#endif
	
				case 12: //SYSCALL
					return cpuPrvTakeSyscallExc();
				
				case 13: //BREAK
					return cpuPrvTakeBreakpointExc();
				
				case 15: //SYNC
					//nothing to do for us
					break;
				
				case 16: //MFHI
					cpuSetRegD(instr, cpu.hi);
					break;
				
				case 17: //MTHI
					cpu.hi = cpuGetRegS(instr);
					break;
				
				case 18: //MFLO
					cpuSetRegD(instr, cpu.lo);
					break;
				
				case 19: //MTLO
					cpu.lo = cpuGetRegS(instr);
					break;
				
				case 24: //MULT
					cpu.hilo64 = (int64_t)(int32_t)cpuGetRegS(instr) * (int64_t)(int32_t)cpuGetRegT(instr);
					break;
				
				case 25: //MULTU
					cpu.hilo64 = (uint64_t)(uint32_t)cpuGetRegS(instr) * (uint64_t)(uint32_t)cpuGetRegT(instr);
					break;
				
				case 26: //DIV
					i32a = cpuGetRegT(instr);
					if (i32a) {
						int32_t num = cpuGetRegS(instr);
						cpu.lo = num / (int32_t)i32a;
						cpu.hi = num % (int32_t)i32a;
					}
					break;
		
				case 27: //DIVU
					i32a = cpuGetRegT(instr);
					if (i32a) {
						uint32_t num = cpuGetRegS(instr);
						cpu.lo = num / i32a;
						cpu.hi = num % i32a;
					}
					break;
				
				case 32: //ADD
					i32a = cpuGetRegS(instr);
					i32b = cpuGetRegT(instr);
					if (__builtin_sadd_overflow((int)i32a, (int)i32b, (int*)&i32d))	//if overflow happened and instr calls for trap, do it
						return cpuPrvTakeIntegerOverflowExc();
					else
						cpuSetRegD(instr, i32d);
					break;
				
				case 33: //ADDU
					i32a = cpuGetRegS(instr);
					i32b = cpuGetRegT(instr);
					i32c = i32a + i32b;
					cpuSetRegD(instr, i32c);
					break;
				
				case 34: //SUB
					i32a = cpuGetRegS(instr);
					i32b = cpuGetRegT(instr);
					if (__builtin_ssub_overflow((int)i32a, (int)i32b, (int*)&i32d))	//if overflow happened and instr calls for trap, do it
						return cpuPrvTakeIntegerOverflowExc();
					else
						cpuSetRegD(instr, i32d);
					break;
				
				case 35: //SUBU
					i32a = cpuGetRegS(instr);
					i32b = cpuGetRegT(instr);
					i32c = i32a - i32b;
					cpuSetRegD(instr, i32c);
					break;
				
				case 36: //AND
					cpuSetRegD(instr, cpuGetRegS(instr) & cpuGetRegT(instr));
					break;
				
				case 37: //OR
					cpuSetRegD(instr, cpuGetRegS(instr) | cpuGetRegT(instr));
					break;
				
				case 38: //XOR
					cpuSetRegD(instr, cpuGetRegS(instr) ^ cpuGetRegT(instr));
					break;
				
				case 39: //NOR
					cpuSetRegD(instr, ~(cpuGetRegS(instr) | cpuGetRegT(instr)));
					break;
				
				case 42: //SLT
					cpuSetRegD(instr, ((int32_t)cpuGetRegS(instr) < (int32_t)cpuGetRegT(instr)) ? 1 : 0);
					break;
		
				case 43: //SLTU
					cpuSetRegD(instr, (cpuGetRegS(instr) < cpuGetRegT(instr)) ? 1 : 0);
					break;

#if defined(SUPPORT_TRAPCC) || defined(R4000)
				case 48: //TGE
					if ((int32_t)cpuGetRegS(instr) >= (int32_t)cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 49: //TGEU
					if (cpuGetRegS(instr) >= cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 50: //TLT
					if ((int32_t)cpuGetRegS(instr) < (int32_t)cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 51: //TLTU
					if (cpuGetRegS(instr) < cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 52: //TEQ
					if (cpuGetRegS(instr) == cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 54: //TNE
					if (cpuGetRegS(instr) != cpuGetRegT(instr))
						return cpuPrvTakeTrapExc();
					break;
#endif
				default:
					goto invalid;
			}
			break;

		case 1:	//conditional branches and traps
			switch (i8 = ((instr >> 16) & 0x1F)) {
				
				case 16: //BLTZAL
					cpu.regs[MIPS_REG_RA] = cpu.pc + 8;
					//fallthrough
				
				case 0: //BLTZ
					if (((int32_t)cpuGetRegS(instr)) < 0)
						return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
					break;
					
				case 17: //BGEZAL
					cpu.regs[MIPS_REG_RA] = cpu.pc + 8;
					//fallthrough
				
				case 1: //BGEZ
					if (((int32_t)cpuGetRegS(instr)) >= 0)
						return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
					break;
				
			#if defined(R4000) || defined(SUPPORT_LIKELY_BRANCHES)
			
				case 18: //BLTZALL
					cpu.regs[MIPS_REG_RA] = cpu.pc + 8;
					//fallthrough
				
				case 2: //BLTZL
					if (((int32_t)cpuGetRegS(instr)) < 0)
						return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
					else
						return cpuPrvSkipNextInstr();
					break;
					
				case 19: //BGEZALL
					cpu.regs[MIPS_REG_RA] = cpu.pc + 8;
					//fallthrough
				
				case 3: //BGEZL
					if (((int32_t)cpuGetRegS(instr)) >= 0)
						return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
					else
						return cpuPrvSkipNextInstr();
					break;
				
			#endif
					break;

#if defined(SUPPORT_TRAPCC) || defined(R4000)
				case 8: //TGEI
					if ((int32_t)cpuGetRegS(instr) >= cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 9: //TGEIU
					if (cpuGetRegS(instr) >= (uint32_t)cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 10: //TLTI
					if ((int32_t)cpuGetRegS(instr) < cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 11: //TLTIU
					if (cpuGetRegS(instr) < (uint32_t)cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 12: //TEQI
					if ((int32_t)cpuGetRegS(instr) == cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
				
				case 14: //TNEI
					if ((int32_t)cpuGetRegS(instr) != cpuGetSImm(instr))
						return cpuPrvTakeTrapExc();
					break;
#endif

				default:
					goto invalid;
			}
			break;

		case 3:	//JAL
			cpu.regs[MIPS_REG_RA] = cpu.pc + 8;
			//fallthrough
		case 2:	//J
			return cpuPrvBranchTo((cpu.npc & 0xf0000000ul) | ((instr << 2) & 0x0ffffffful));

		case 4:	//BEQ
			if (cpuGetRegS(instr) == cpuGetRegT(instr))
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			break;

		case 5:	//BNE
			if (cpuGetRegS(instr) != cpuGetRegT(instr))
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			break;
		
		case 6:	//BLEZ
			if ((int32_t)cpuGetRegS(instr) <= 0)
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			break;

		case 7:	//BGTZ
			if ((int32_t)cpuGetRegS(instr) > 0)
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			break;

		case 8: //ADDI
			i32a = cpuGetSImm(instr);
			i32b = cpuGetRegS(instr);
			if (__builtin_sadd_overflow((int)i32a, (int)i32b, (int*)&i32d)) 
				return cpuPrvTakeIntegerOverflowExc();
			else
				cpuSetRegT(instr, i32d);
			break;

		case 9: //ADDIU
			i32a = cpuGetSImm(instr);
			i32b = cpuGetRegS(instr);
			i32c = i32a + i32b;
			cpuSetRegT(instr, i32c);
			break;

		case 10: //SLTI
			cpuSetRegT(instr, ((int32_t)cpuGetRegS(instr) < cpuGetSImm(instr)) ? 1 : 0);
			break;

		case 11: //SLTIU
			cpuSetRegT(instr, (cpuGetRegS(instr) < (uint32_t)cpuGetSImm(instr)) ? 1 : 0);
			break;

		case 12: //ANDI
			cpuSetRegT(instr, cpuGetRegS(instr) & cpuGetUImm(instr));
			break;

		case 13: //ORI
			cpuSetRegT(instr, cpuGetRegS(instr) | cpuGetUImm(instr));
			break;

		case 14: //XORI
			cpuSetRegT(instr, cpuGetRegS(instr) ^ cpuGetUImm(instr));
			break;

		case 15: //LUI
			cpuSetRegT(instr, cpuGetUImm(instr) << 16);
			break;
		
		case 16: //COP0 ops
			if (!cpuPrvCopAccess(0))
				return;
			switch ((instr >> 21) & 0x1f) {
				
				case 2:		//CFC seems to also do the same thing
				case 0:		//MFC0	(move from coproc)
					if (instr & 0x000007ff)
						goto invalid;
					switch (cpuGetRegNumD(instr)) {
						
						case 0:
							cpuSetRegT(instr, cpu.index);
							break;
						
						case 1:
							cpuSetRegT(instr, ((uint32_t)cpuPrvRefreshRandom()) << 8);
							break;
						
						case 2:
							cpuSetRegT(instr, cpu.entryLo);
							break;
						
						case 4:
							cpuSetRegT(instr, cpu.context);
							break;
						
						case 8:
							cpuSetRegT(instr, cpu.badva);
							break;
						
						case 10:
							cpuSetRegT(instr, cpu.entryHi);
							break;
						
						case 12:
							cpuSetRegT(instr, cpu.status);
							break;
						
						case 13:
							cpuSetRegT(instr, cpu.cause);
							break;
						
						case 14:
							cpuSetRegT(instr, cpu.epc);
							break;
						
						case 15:
							cpuSetRegT(instr, PRID_VALUE);
							break;
						
						default:
							goto invalid;
					}
					break;
					
				case 4:		//MTC0	(move to coproc)
					if (instr & 0x000007ff)
						goto invalid;
					
					switch (cpuGetRegNumD(instr)) {
						
						case 0:
							cpu.index = cpuGetRegT(instr);
							break;

						case 2:
							cpu.entryLo = cpuGetRegT(instr);
							break;

						case 4:
							cpu.context = (cpu.context &~ CP0_CTX_PTEBASE_MASK) | (cpuGetRegT(instr) & CP0_CTX_PTEBASE_MASK);
							break;
						
						case 8:
							cpu.badva = cpuGetRegT(instr);
							break;
						
						case 10:
							i32a = cpu.entryHi;
							cpu.entryHi = cpuGetRegT(instr);
							cpuPrvMaybeAsidChanded(i32a);
							break;

						case 12:
							//what CAN we write?
#ifdef R4000
							i32a = CP0_STATUS_CU_MASK | CP0_STATUS_IM_MASK | CP0_STATUS_ERL | CP0_STATUS_EXL | CP0_STATUS_IE;
#else
							i32a = CP0_STATUS_ISC | CP0_STATUS_SWC | CP0_STATUS_CU_MASK | CP0_STATUS_IM_MASK | CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE;
#endif
							cpu.status = (cpu.status &~ i32a) | (cpuGetRegT(instr) & i32a);
							break;
						
						case 13:
							i32a = CP0_CAUSE_IV | CP0_CAUSE_WP | (3 << CP0_CAUSE_IP_SHIFT);
							cpu.cause = (cpu.cause &~ i32a) | (cpuGetRegT(instr) & i32a);
							break;
						
						case 14:
							cpu.epc = cpuGetRegT(instr);
							break;
						
						default:
							goto invalid;
					}
					break;
				
				case 8:		//bc0f (used for wb flush)
					if (instr == 0x4100FFFF)
						break;
					goto invalid;
				
				case 16:	//COP0
			
					if (instr & 0x01ffffe0)
						goto invalid;
					switch (instr & 0x3f) {
						case 1: //TLBR
							cpuPrvTlbr();
							break;
						
						case 2: //TLBWI
							cpuPrvTlbwi();
							break;
						
						case 6: //TLBWR
							cpuPrvTlbwr();
							break;
						
						case 8: //TLBP
							cpuPrvTlbp();
							break;
#ifdef R4000
						case 24: //ERET
							cpu.pc = cpu.epc;
							cpu.npc = cpu.pc + 4;
							cpu.status &=~ CP0_STATUS_EXL;
							cpu.llbit = 0;
							break;
#else
						case 16: //RFE: fuck if i know what this does anymore
							cpu.status =
								(cpu.status &~ (CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE)) |
								((cpu.status & (CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP)) >> 2);
							cpu.llbit = 0;
							break;
#endif

						default:
							goto invalid;
					}
					break;
				
				default:
					goto invalid;
			}
			break;
		
		case 17: //COP1 (FPU)
			if (!cpuPrvCopAccess(1))
				return;
	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)			
			switch (fpuOp(instr, cpu.regs, &cpu.fpu)) {
				case FpuRetInstrInval:
				default:
					fprintf(stderr, "signalling fp issue [%08x] = %08x\n", cpu.pc, instr);
					goto invalid;
				
				case FpuRetExcTaken:
					return cpuPrvTakeFloatingPointExc();
				
				case FpuBranchTaken:
					return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			
			#ifdef SUPPORT_LIKELY_BRANCHES
				case FpuLikelyBranchNotTaken:
					return cpuPrvSkipNextInstr();
			#endif
			
				case FpuRetInstrDone:
					break;
				
				case FpuCoprocUseException:
					cpuPrvTakeCoprocUnusableExc(1);
					break;
			}
			break;
	#elif defined(FPU_SUPPORT_NONE)
			if ((instr & 0xFFE0FFFF) == 0x44400000)	{ //FIR is read even if we say we have no FPU...go figure...
			
				cpuSetRegT(instr, 0);
				break;
			}
			goto invalid;
	#else
		#error "no fpu setting"
	#endif
		
		case 18: //COP2
			if (!cpuPrvCopAccess(2))
				return;
			goto invalid;
		
		case 19: //COP3 (reserved, used for hypercall)
			if (!cpuPrvIsInKernelMode())
				goto invalid;
			if (instr != MIPS_HYPERCALL)
				goto invalid;
			if (!cpuExtHypercall())
				goto invalid;
			break;
	
	#if defined(R4000) || defined(SUPPORT_LIKELY_BRANCHES)
		case 20: //BEQL
			if (cpuGetRegS(instr) == cpuGetRegT(instr))
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			else
				return cpuPrvSkipNextInstr();
			break;
		
		case 21: //BNEL
			if (cpuGetRegS(instr) != cpuGetRegT(instr))
				return cpuPrvBranchTo(cpu.npc + (cpuGetSImm(instr) << 2));
			else
				return cpuPrvSkipNextInstr();
			break;
	#endif
		
		
		case 28: //SPECIAL2
			switch (instr & 0x3f) {
	
	#ifdef SUPPORT_MUL		
				case 2: //MUL
					if (instr & 0x7C0)
						goto invalid;
					cpuSetRegD(instr, cpuGetRegS(instr) * cpuGetRegT(instr));
					break;
	#endif		
	#ifdef SUPPORT_MAC
				case 0: //MADD
					cpu.hilo64 += (int64_t)(int32_t)cpuGetRegS(instr) * (int64_t)(int32_t)cpuGetRegT(instr);
					break;
					
				case 1: //MADDU
					cpu.hilo64 += (uint64_t)(uint32_t)cpuGetRegS(instr) * (uint64_t)(uint32_t)cpuGetRegT(instr);
					break;
				
				case 4: //MSUB
					cpu.hilo64 -= (int64_t)(int32_t)cpuGetRegS(instr) * (int64_t)(int32_t)cpuGetRegT(instr);
					break;
				
				case 5: //MSUBU
					cpu.hilo64 -= (uint64_t)(uint32_t)cpuGetRegS(instr) * (uint64_t)(uint32_t)cpuGetRegT(instr);
					break;
	#endif
	#ifdef SUPPORT_CLZ
				case 32: //CLZ
					i32a = cpuGetRegS(instr);
					cpuSetRegD(instr, i32a ? __builtin_clz(i32a) : 32);
					break;
					
				case 33: //CLO
					i32a = ~cpuGetRegS(instr);
					cpuSetRegD(instr, i32a ? __builtin_clz(i32a) : 32);
					break;
				
	#endif
				default:
					goto invalid;
			}
			break;
	
		case 31: //SPECIAL3
			switch (instr & 0x3f) {
	
	#ifdef SUPPORT_BITFIELD_OPS	
				case 0:	//EXT
				{
					uint32_t lsb = (instr >> 6) & 0x1f;
					uint32_t bitlen = ((instr >> 11) & 0x1f) + 1;
					uint32_t lsl = 32 - bitlen - lsb;
					uint32_t lsr = 32 - bitlen;
					
					i32a = cpuGetRegS(instr);
					i32a <<= lsl;
					i32a >>= lsr;
					
					cpuSetRegT(instr, i32a);
					break;
				}
	
				case 4: //INS
				{
					uint32_t lsb = (instr >> 6) & 0x1f;
					uint32_t msb = (instr >> 11) & 0x1f;
					uint32_t bitlen = msb - lsb + 1;
					uint32_t mask = ((1 << bitlen) - 1) << lsb;
					
					i32a = cpuGetRegT(instr);
					i32b = cpuGetRegS(instr);
					
					i32a &=~ mask;
					i32a |= (i32b << lsb) & mask;
					
					cpuSetRegT(instr, i32a);
					break;
				}
	#endif		
				case 32:
					switch (cpuGetRegNumA(instr)) {
						
	#ifdef SUPPORT_BYTESWAP
						case 2: //WSBH
							i32a = cpuGetRegT(instr);
							i32a = ((i32a & 0xff00ff00) >> 8) | ((i32a << 8) & 0xff00ff00);
							cpuSetRegD(instr, i32a);
							break;
	#endif
	#ifdef SUPPORT_EXTEND_OPS
						case 16: //SEB
							cpuSetRegD(instr, (int32_t)(int8_t)cpuGetRegT(instr));
							break;
						
						case 24: //SEH
							cpuSetRegD(instr, (int32_t)(int16_t)cpuGetRegT(instr));
							break;
	#endif
						
						default:
							goto invalid;
					}
					break;
					
				case 59: //RDHWR
					goto invalid_nowarn;
	
				default:
					goto invalid;
			}
			break;
	
		case 32: //LB
			if (!cpuPrvDataAccess(&i8, cpuGetRegS(instr) + cpuGetSImm(instr), 1, false))
				return;
			cpuSetRegT(instr, (int32_t)(int8_t)i8);
			break;

		case 33: //LH
			if (!cpuPrvDataAccess(&i16, cpuGetRegS(instr) + cpuGetSImm(instr), 2, false))
				return;
			cpuSetRegT(instr, (int32_t)(int16_t)i16);
			break;

		case 34: //LWL
		case 38: //LWR
		case 42: //SWL
		case 46: //SWR
			i8 = (instr >> 26);
/*
	lwl is most significant and gets byte addr of msb (unaligned_addr + 3 for LE)
	lwr is least significant and gets byte addr of lsb (unaligned_addr + 0 for LE)
	thus to load an unaligned word from 0($s0) to $a0, we'd issue
	
	lwl $a0, 3($s0)
	lwr $a0, 0($s0)
	
	if the address IS aligned, register is overwritten twice
*/

			i32a = cpuGetRegS(instr) + cpuGetSImm(instr);	//word-aligned address
			i32b = cpuGetRegT(instr);			//value in register
			if (!cpuPrvDataAccess(&i32d, i32a &~ 3, 4, false))
				return;
			
			i32c = 0xFFFFFFFFUL;				//mask
			if (i8 & 8) {	//SWR/SWL
				if (i8 & 4) {	//SWR
					
					//LSLS/LSR by 32 is UB, avoid it
					if (i32a & 3) {
						
						i32b <<= (i32a & 3) * 8;
						i32d <<= 32 - (i32a & 3) * 8;
						i32d >>= 32 - (i32a & 3) * 8;
						i32d |= i32b;
					}
					else
						i32d = i32b;
				}
				else {		//SWL
					
					//LSLS/LSR by 32 is UB, avoid it
					if (3 - (i32a & 3)) {
					
						i32b >>= (3 - (i32a & 3)) * 8;
						
						i32d >>= 32 - (3 - (i32a & 3)) * 8;
						i32d <<= 32 - (3 - (i32a & 3)) * 8;
						
						i32d |= i32b;
					}
					else
						i32d = i32b;
				}
				
				if (!cpuPrvDataAccess(&i32d, i32a &~ 3, 4, true))
					return;
			}
			else {		//LWR/LWL
				if (i8 & 4) {	//LWR
					
					//LSLS/LSR by 32 is UB, avoid it
					if (i32a & 3) {
						i32d >>= (i32a & 3) * 8;
						
						i32b >>= 32 - (i32a & 3) * 8;
						i32b <<= 32 - (i32a & 3) * 8;
						
						i32b |= i32d;
					}
					else
						i32b = i32d;
				}
				else {		//LWL
				
					//LSLS/LSR by 32 is UB, avoid it
					if (3 - (i32a & 3)) {
						
						i32d <<= (3 - (i32a & 3)) * 8;
						
						i32b <<= 32 - (3 - (i32a & 3)) * 8;
						i32b >>= 32 - (3 - (i32a & 3)) * 8;
						
						i32b |= i32d;
					}
					else
						i32b = i32d;
				}
				cpuSetRegT(instr, i32b);
			}
			break;
		
		case 35: //LW
			if (!cpuPrvDataAccess(&i32d, cpuGetRegS(instr) + cpuGetSImm(instr), 4, false))
				return;
			cpuSetRegT(instr, i32d);
			break;

		case 36: //LBU
			if (!cpuPrvDataAccess(&i8, cpuGetRegS(instr) + cpuGetSImm(instr), 1, false))
				return;
			cpuSetRegT(instr, (uint32_t)(uint8_t)i8);
			break;

		case 37: //LHU
			if (!cpuPrvDataAccess(&i16, cpuGetRegS(instr) + cpuGetSImm(instr), 2, false))
				return;
			cpuSetRegT(instr, (uint32_t)(uint16_t)i16);
			break;

		case 40: //SB
			i8 = cpuGetRegT(instr);
			if (!cpuPrvDataAccess(&i8, cpuGetRegS(instr) + cpuGetSImm(instr), 1, true))
				return;
			break;

		case 41: //SH
			i16 = cpuGetRegT(instr);
			if (!cpuPrvDataAccess(&i16, cpuGetRegS(instr) + cpuGetSImm(instr), 2, true))
				return;
			break;
		
		case 43: //SW
			i32d = cpuGetRegT(instr);
			if (!cpuPrvDataAccess(&i32d, cpuGetRegS(instr) + cpuGetSImm(instr), 4, true))
				return;
			break;
	#ifdef SUPPORT_LL_SC
		case 48: //LL
			if (!cpuPrvDataAccess(&i32d, cpuGetRegS(instr) + cpuGetSImm(instr), 4, false))
				return;
			cpuSetRegT(instr, i32d);
			cpu.llbit = 1;
			break;
			
		case 56: //SC	
			if (!cpu.llbit)
				cpuSetRegT(instr, 0);
			else {
				i32d = cpuGetRegT(instr);
				if (!cpuPrvDataAccess(&i32d, cpuGetRegS(instr) + cpuGetSImm(instr), 4, true))
					return;
				cpuSetRegT(instr, 1);
			}
			cpu.llbit = 0;
			break;
	#endif
	
		case 49: //LWC1
			if (!cpuPrvCopAccess(1))
				return;
	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
	
			if (!cpuPrvDataAccess(&cpu.fpu.i[cpuGetRegNumT(instr)], cpuGetRegS(instr) + cpuGetSImm(instr), 4, false))
				return;
		//	LOG("LDR %08x (%f) [0x%08x] -> f%02u\r\n", cpu.fpu.i[cpuGetRegNumT(instr)], cpu.fpu.f[cpuGetRegNumT(instr)], cpuGetRegS(instr) + cpuGetSImm(instr), cpuGetRegNumT(instr));
			break;
			
	#else
			goto invalid_nowarn;
	#endif
			
		case 57: //SWC1
			if (!cpuPrvCopAccess(1))
				return;
	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
			
		//	LOG("STR %08x (%f) f%02u -> [0x%08x]\r\n", cpu.fpu.i[cpuGetRegNumT(instr)], cpu.fpu.f[cpuGetRegNumT(instr)], cpuGetRegNumT(instr), cpuGetRegS(instr) + cpuGetSImm(instr));
			if (!cpuPrvDataAccess(&cpu.fpu.i[cpuGetRegNumT(instr)], cpuGetRegS(instr) + cpuGetSImm(instr), 4, true))
				return;
			break;
			
	#else
			goto invalid_nowarn;
	#endif

		case 50: //LWC2
		case 58: //SWC2
			if (!cpuPrvCopAccess(2))
				return;
			goto invalid_nowarn;

		case 51: //PREF
			//ok...prefetched
			break;
		
		case 53: //LDC1
			if (!cpuPrvCopAccess(1))
				return;
	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
	
			if (!cpuPrvDataAccess(&cpu.fpu.d[cpuGetRegNumT(instr) / 2], cpuGetRegS(instr) + cpuGetSImm(instr), 8, false))
				return;
		//	LOG("LDD %08x%08x (%f) [0x%08x] -> d%02u\r\n", cpu.fpu.i[cpuGetRegNumT(instr) + 1], cpu.fpu.i[cpuGetRegNumT(instr)], cpu.fpu.d[cpuGetRegNumT(instr) / 2], cpuGetRegS(instr) + cpuGetSImm(instr), cpuGetRegNumT(instr) / 2);
			break;
			
	#else
			goto invalid_nowarn;
	#endif
			
		case 61: //SDC1
			if (!cpuPrvCopAccess(1))
				return;
	#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
	
		//	LOG("STD %08x%08x (%f) d%02u -> [0x%08x]\r\n", cpu.fpu.i[cpuGetRegNumT(instr) + 1], cpu.fpu.i[cpuGetRegNumT(instr)], cpu.fpu.d[cpuGetRegNumT(instr) / 2], cpuGetRegNumT(instr) / 2, cpuGetRegS(instr) + cpuGetSImm(instr));
			if (!cpuPrvDataAccess(&cpu.fpu.d[cpuGetRegNumT(instr) / 2], cpuGetRegS(instr) + cpuGetSImm(instr), 8, true))
				return;
			break;
			
	#else
			goto invalid_nowarn;
	#endif
		
		case 54: //LDC2
		case 62: //SDC2
			goto invalid;
		
		default:
			goto invalid;
	}
	cpuPrvNoBranchTaken();
	return;

invalid:
	err_str("unknown instruction 0x%08lX @ 0x%08lX (%u %u)\r\n", (unsigned long)instr, (unsigned long)cpu.pc, (unsigned)(instr >> 26), (unsigned)(instr & 0x3f));
	
invalid_nowarn:
	return cpuPrvTakeReservedInstrExc();
}

void cpuInit(uint32_t ramAmount)
{
	uint_fast8_t i;
	
	(void)ramAmount;
	
	memset(&cpu, 0, sizeof(cpu));
#ifdef R4000
	cpu.status |= CP0_STATUS_ERL;
#endif
	cpu.pc = 0xBFC00000UL;	/* mips gets reset to this addr */
	cpu.npc = cpu.pc + 4;
	cpuPrvIcacheFlushEntire();
	
	//having these in a chain simplifies removal
	for (i = 0; i < NUM_TLB_ENTRIES; i++) {
		cpu.tlb[i].va = 0x80000000;
		cpu.tlb[i].prevIdx = i - 1;
		cpu.tlb[i].nextIdx = i + 1;
	}
	cpu.tlb[NUM_TLB_ENTRIES - 1].nextIdx = -1;
	
	for (i = 0; i < TLB_HASH_ENTRIES; i++)
		cpu.tlbHash[i] = -1;
	
	cpu.tlbHash[TLB_HASH(cpu.tlb[0].va)] = 0;
}


void prTLB() {
  int i;
  	for (i = 0; i < NUM_TLB_ENTRIES; i++) {
	  pr("%d va: %08x pa: %08x asid: %02x flags: %02x rfu: %d g: %d v:%d d:%d n:%d\n",
	     i,
	     cpu.tlb[i].va,
	     cpu.tlb[i].pa,
	     cpu.tlb[i].asid,
	     cpu.tlb[i].flagsAsByte,
	     cpu.tlb[i].rfu,
	     cpu.tlb[i].g,
	     cpu.tlb[i].v,
	     cpu.tlb[i].d,
	     cpu.tlb[i].n);
	}
}
