/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/


//we use reg to point to handle_irq or do_cycle
//we adjust as needed - will save a few cycles per instr
//but we need free reg first: r11
// will need to add  -ffixed-r11 to compiler args
//but gcc's FPU code uses r11 so we stash a boolean
//in cpu struct to tell us if we're in fpu code.
//on return we restore proper value to r11
//we assume no other code uses r11. bad bad things will
//happen if it does! This really should be reverified
//in m3/m7 ports too, in case libgcc uses r11 for other
//things there too

#include "../hypercall.h"


//#define DISABLE_ICACHE	


//r0..r3, r6, r7 are tmp use
#define	t0					r0	//not preserved across func cals
#define	t1					r1
#define	t2					r2
#define	t3					r3
								//preserved acrtoss func calls
#define p1					r7

#define REG_INSTR			r6
#define REG_CPU				r4
#define REG_CPU_P2			r5
#define REG_PC				r8
#define REG_NPC				r9
#define REG_NOT_DELAY_SLOT	r10		//must only take on values of 0 or 4		4 when NOT in slot, 0 when is
#define REG_DO_NEXT_CY		r11


/*
	cpu struct:
	
0x00:
	uint32_t regs[32];	//required to be first
0x80:
	uint32_t space;	//for loads and stores, also stores ram amount between init and cycle and between saveState/restoreState
	uint8_t inDelaySlot
	uint8_t haveIrqs	//MUST be either 0 or 4
	uint8_t llBit
	uint8_t inFpuOp		//for irq stuffs
	uint32_t pc, npc;
0x90:
	uint32_t lo, hi, index, cause
0xa0:
	uint32_t status, epc, badva, entryHi
0xb0:
	uint32_t entryLo, context, random, memLimit
0xc0:
	//tlb
	struct TlbEntry {
		uint32_t va;	//top-aligned, bottom zero
		uint32_t pa;	//top-aligned, bottom zero
		uint8_t asid;	//in proper bit place, all other parts zeroes
		union{
			struct {
				
				uint8_t	enabled :1;	//cache of "asid == curAsid || g"
				uint8_t rfu		:3;
				uint8_t g		:1;
				uint8_t v		:1;
				uint8_t d		:1;
				uint8_t n		:1;
			};
			uint8_t flagsAsByte;
		}
		uint8_t prevIdx;	//element index if top bit clear, bucket index if this is the first element
		uint8_t nextIdx;	//0xff if this is the last element
	}[NUM_TLB_ENTRIES]		//0x0c each
	
0x4c0:
	uint8_t hashBuckets[NUM_TLB_BUCKETS];	//0xff if empty
	
0x540:
	struct {
		uint8_t icache[ICACHE_LINE_SIZE]
		uint32_t addr;	//kept as LSRed by ICACHE_LINE_SIZE, so 0xfffffffe is a valid "empty "sentinel
	} [ICACHE_NUM_WAYS * ICACHE_NUM_SETS]

#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)

0xe40:
	union {
		uint32_t iRegs[32];
		float fRegs[32];
		double dRegs[16];
	};
	uint32_t fcr

#endif

0xec4:

*/

#define NUM_REGS				32

#define OFST_PART2				(NUM_REGS * 4)
#define OFST_TLB				(OFST_PART2 + 0x40)

#define OFST_TLB_HASH_MIN		(OFST_TLB + NUM_TLB_ENTRIES * SIZEOF_TLB_ENTRY)
#define OFST_TLB_HASH			((OFST_TLB_HASH_MIN + 15) / 16 * 16)	//make it easy to generate without a literal load

#define OFST_ICACHE_MIN			(OFST_TLB_HASH + NUM_TLB_BUCKETS)
#define OFST_ICACHE				((OFST_ICACHE_MIN + 15) / 16 * 16)	//make it easy to generate without a literal load


#define OFST_ICACHE_ADDR		(ICACHE_LINE_SIZE)

//allf of these are from the second half!
#define OFST_SPACE				0x00
#define OFST_IN_DELAY_SLOT		0x04		//only used when state is saved
#define OFST_HAVE_IRQ			0x05
#define OFST_LLBIT				0x06
#define OFST_IN_FPU_OP			0x07
#define OFST_PC					0x08
#define OFST_NPC				0x0c
#define OFST_LO					0x10
#define OFST_HI					0x14
#define OFST_CP0_INDEX			0x18
#define OFST_CP0_CAUSE			0x1c
#define OFST_CP0_STATUS			0x20
#define OFST_CP0_EPC			0x24
#define OFST_CP0_BADVA			0x28
#define OFST_CP0_ENTRYHI		0x2c
#define OFST_CP0_ENTRYLO		0x30
#define OFST_CP0_CONTEXT		0x34
#define OFST_RANDOM				0x38
#define OFST_MEMLIMIT			0x3c


//each entry is 0x10 bytes
#define OFST_TLB_VA				0x00
#define OFST_TLB_PA				0x04
#define OFST_TLB_ASID			0x08
#define OFST_TLB_BITS			0x09		//flags in top 4 bits, enabled bit in lowest bit
#define OFST_TLB_HASH_PREV_IDX	0x0a
#define OFST_TLB_HASH_NEXT_IDX	0x0b
#define SIZEOF_TLB_ENTRY		0x0c

#define TLB_FLAGS_BIT_G			4
#define TLB_FLAGS_BIT_V			5
#define TLB_FLAGS_BIT_D			6
#define TLB_FLAGS_BIT_N			7

#define OFST_FPU_FCR			128

#define FPU_FIR					0x300

#define FCR_UNIMPL				0x00020000
#define FCR_INVAL_OP			0x10
#define FCR_CEF_DIV0			0x08
#define FCR_CEF_OVERFLOW		0x04
#define FCR_CEF_UDERFLOW		0x02
#define FCR_CEF_INEXACT			0x01

#define FCR_SHIFT_FLAGS			2
#define FCR_SHIFT_ENABLES		7
#define FCR_SHIFT_CAUSE			12
#define FPU_FCR_C_SHIFT			23

#define FCR_PEROP_FLAGS			(((FCR_INVAL_OP | FCR_CEF_DIV0 | FCR_CEF_OVERFLOW | FCR_CEF_UDERFLOW | FCR_CEF_INEXACT) << FCR_SHIFT_CAUSE) | FCR_UNIMPL)


#define OFST_FPU				(OFST_ICACHE + ICACHE_LINE_STOR_SZ * ICACHE_NUM_WAYS * ICACHE_NUM_SETS)

#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)
	#define SIZEOF_FPU			(32*4+4)
#else
	#define SIZEOF_FPU			0
#endif

#define CPU_SIZE				(OFST_FPU + SIZEOF_FPU)


#define PRID_VALUE				0x0220	//R3000

#define MIPS_REGNO_RA			31

#define ORDER_NUM_TLB_BUCKETS	7
#define NUM_TLB_BUCKETS			(1 << ORDER_NUM_TLB_BUCKETS)

#define ORDER_NUM_TLB_ENTRIES	6
#define NUM_TLB_ENTRIES			(1 << ORDER_NUM_TLB_ENTRIES)
#define NUM_WIRED_ENTRIES		8
#define NUM_IRQS				8		//lower 2 are sw irqs

#define PAGE_ORDER				12

#define ICACHE_NUM_WAYS_ORDER	0
#define ICACHE_NUM_WAYS			(1 << ICACHE_NUM_WAYS_ORDER)	//number of lines a given va can be in

#define ICACHE_NUM_SETS			(1 << ICACHE_NUM_SETS_ORDER)	//number of buckets of VAs
#define ICACHE_LINE_SZ_ORDER	5
#define ICACHE_LINE_SIZE		(1 << ICACHE_LINE_SZ_ORDER)
#define ICACHE_LINE_STOR_SZ		(ICACHE_LINE_SIZE + 4)

#define CP0_CAUSE_IP_SHIFT		8

#define CP0_CTX_PTEBASE_MASK	0xffe00000
#define CP0_CTX_PTEBASE_SHIFT	21
#define CP0_CTX_BADVPN2_NBITS	19
#define CP0_CTX_BADVPN2_SHIFT	2
#define CP0_CTX_BADVPN2_MASK	(((1 << CP0_CTX_BADVPN2_NBITS) - 1) << CP0_CTX_BADVPN2_SHIFT)


#define BITNO_CP0_STATUS_KUC	1

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
#define CP0_STATUS_ISC_SHIFT	16
#define CP0_STATUS_ISC			(1 << CP0_STATUS_ISC_SHIFT)
#define CP0_STATUS_IM(x)		(0x00000100 << (x))
#define CP0_STATUS_IM_MASK		0x0000ff00
#define CP0_STATUS_IM_BITLEN	8
#define CP0_STATUS_IM_SHIFT		8
#define CP0_STATUS_KUO			0x00000020
#define CP0_STATUS_IEO			0x00000010
#define CP0_STATUS_KUP			0x00000008
#define CP0_STATUS_IEP			0x00000004
#define CP0_STATUS_KUC			(1 << BITNO_CP0_STATUS_KUC)	//0x00000002, set when in userspace
#define CP0_STATUS_IE_SHIFT		0
#define CP0_STATUS_IE			(1 << CP0_STATUS_IE_SHIFT)

#define CP0_INDEX_SHIFT			8

//MD00090-2B-MIPS32PRA-AFP-06.02.pdf page 209
#define CP0_CAUSE_BD_SHIFT		31
#define CP0_CAUSE_BD			(1 << CP0_CAUSE_BD_SHIFT)
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
#define CP0_EXC_COD_TR			13	//TRAP (R4000+ only)
#define CP0_EXC_COD_MSAFPE		14	//MSA Floating-Point exception (R4000+ only)
#define CP0_EXC_COD_FPE			15	//Floating-Point exception (R4000+ only, R3000 used RI)
#define CP0_EXC_COD_TLBRI		19	//read inhibit caught a read (R4000+ only)
#define CP0_EXC_COD_TLBXI		20	//execution inhibit caught an attempt (R4000+ only)
#define CP0_EXC_COD_MSADIS		21	//MSA Disabled exception (R4000+ only)
#define CP0_EXC_COD_WATCH		23	//watchpoint hit (R4000+ only)


#define TLB_ENTRYHI_VA_MASK		0xfffff000
#define TLB_ENTRYHI_ASID_MASK	0x00000fc0
#define TLB_ENTRYHI_ASID_SHIFT	6
#define TLB_ENTRYHI_ASID_BITLEN	6


#define TLB_ENTRYLO_PA_MASK		0xfffff000
#define TLB_ENTRYLO_N			0x00000800
#define TLB_ENTRYLO_D			0x00000400
#define TLB_ENTRYLO_V			0x00000200
#define TLB_ENTRYLO_G			0x00000100
#define TLB_ENTRYLO_FLAGS_MASK	0x00000ff0	//for flagsAsByte
#define TLB_ENTRYLO_FLAGS_SHIFT	4



#if defined(CPU_TYPE_CM0)
	#include "cpuM0.inc"
#elif defined(CPU_TYPE_CM3) | defined(CPU_TYPE_CM4) | defined(CPU_TYPE_CM7)
	#include "cpuM3.inc"
#endif

.syntax unified





.bss
.balign 4
.globl mCpu
mCpu:
	.skip CPU_SIZE

.text



//recalc whether we have a pending irq or not
.macro	calcIrqWSta	cpuP2reg, tmp2, dstReg, curStaReg	//"recalc irqs with status"
	
	movs		\dstReg, #0
	lsrs		\tmp2, \curStaReg, #1 + CP0_STATUS_IE_SHIFT
	bcc			91f	//irqs are off

	lsrs		\dstReg, \curStaReg, #0 + CP0_STATUS_IM_SHIFT
	
	ldr			\tmp2, [\cpuP2reg, #0 + OFST_CP0_CAUSE]
	lsrs		\tmp2, #0 + CP0_CAUSE_IP_SHIFT
	
	ands		\dstReg, \tmp2
	lsls		\dstReg, \dstReg, #32 - CP0_STATUS_IM_BITLEN
	beq			91f
	movs		\dstReg, #4
91:

.endm

.macro	recalcIrqs	cpuP2reg, tmp2, tmp3, dstReg	//all regs must differ. call when reg mask changes or IE bit does
	
	ldr			\tmp3, [\cpuP2reg, #0 + OFST_CP0_STATUS]
	calcIrqWSta	\cpuP2reg, \tmp2, \dstReg, \tmp3
.endm



//for calls from emulator
.macro	setNextCyJump	regHaveIrq, regTmp, lblAdrs
	adr			\regTmp, \lblAdrs
	ldr			\regTmp, [\regTmp, \regHaveIrq]
	mov			REG_DO_NEXT_CY, \regTmp
.endm

.macro nextCyJumpTbl	name
	.balign 4
\name:
	.word do_cycle + 1
	.word handle_irq + 1
.endm

.macro regNoS dst, instr
	bfx			\dst, \instr, 21, 5
.endm

.macro regNoT dst, instr
	bfx			\dst, \instr, 16, 5
.endm

.macro regNoTz dst, instr			//also sets Z on regNo
	bfxAndZ		\dst, \instr, 16, 5
.endm

.macro regNoDz dst, instr			//also sets Z on regNo
	bfxAndZ		\dst, \instr, 11, 5
.endm

.macro getShamt dst, instr
	bfx			\dst, \instr, 6, 5
.endm


.macro setRa	valReg
	str			\valReg, [REG_CPU, #4 * MIPS_REGNO_RA]
.endm

.macro endCy tmp, isInDelaySlot	//if something else already redirected us, like an exception. or pc was advanced by endCyNoBra,endCyBranch
	
	//TODO
	
	.if \isInDelaySlot
		bw		do_cycle
	.else
		bx		REG_DO_NEXT_CY
	.endif
.endm

.macro loadState tmp
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_PC]
	mov			REG_PC, \tmp
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_NPC]
	mov			REG_NPC, \tmp
	ldrb		\tmp, [REG_CPU_P2, #0 + OFST_IN_DELAY_SLOT]
	mov			REG_NOT_DELAY_SLOT, \tmp
.endm

.macro saveState tmp
	mov			\tmp, REG_PC
	str			\tmp, [REG_CPU_P2, #0 + OFST_PC]
	mov			\tmp, REG_NPC
	str			\tmp, [REG_CPU_P2, #0 + OFST_NPC]
	mov			\tmp, REG_NOT_DELAY_SLOT
	strb		\tmp, [REG_CPU_P2, #0 + OFST_IN_DELAY_SLOT]
.endm

.macro copUseExc	tmp, tmp2, copNo

	movs		\tmp2, #3
	lsls		\tmp2, #0 + CP0_CAUSE_CE_SHIFT
	cpsid		i	//do not operate on IRQSTATE with irqs on
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	bics		\tmp, \tmp2
	movs		\tmp2, \copNo
	lsls		\tmp2, #0 + CP0_CAUSE_CE_SHIFT
	orrs		\tmp, \tmp2
	str			\tmp, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	cpsie		i
	
	movs		r0, #(CP0_EXC_COD_CPU << CP0_CAUSE_EXC_COD_SHIFT)
	bw			cpuPrvTakeException
.endm

.macro copCheck	tmp, tmp2, copNo		//only for CP1..2
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	mov lr, \tmp
	lsrs		\tmp, \copNo + 28 + 1	//shift permission bit into carry
	bcs			99f
	
	copUseExc	\tmp, \tmp2, \copNo
	
99:
.endm

.macro	refreshRng	outReg, tmp	//produces 8 bits
	ldr			\outReg, [REG_CPU_P2, #0 + OFST_RANDOM]
	ldr			\tmp, =214013
	muls		\outReg, t1
	ldr			\tmp, =2531011
	adds		\outReg, \tmp
	str			\outReg, [REG_CPU_P2, #0 + OFST_RANDOM]
	//now produce a result
	lsrs		\outReg, #24
.endm

#ifdef SLOW_FLASH

	//C-M0s often have large flash wait states for high speeds, hidden by a single-line 128-byte cache.
	//sadly this means that literal loads are NOT 2 cycles as the C-M0p TRM will have you belive, but
	//2+L where L is the latency. In those cases, a few more instrs will be faster to avoid this!

	.macro getIcPtr	dst
		loadImm		\dst, OFST_ICACHE
		add			\dst, REG_CPU
	.endm

	.macro getTlbHptr	dst
		loadImm		\dst, OFST_TLB_HASH
		add			\dst, REG_CPU
	.endm
	
	.macro getFpuPtr	dst
		loadImm		\dst, OFST_FPU
		add			\dst, REG_CPU
	.endm

#else

	.macro getIcPtr	dst
		ldr			\dst, =mCpu + OFST_ICACHE
	.endm

	.macro getTlbHptr	dst
		ldr			\dst, =mCpu + OFST_TLB_HASH
	.endm
	
	.macro getFpuPtr	dst
		ldr			\dst, =mCpu + OFST_FPU
	.endm

#endif




.macro cacheChkWy cacheAddrReg /*in/out */, hitLbl, lineRoundedAddr, tmp3, lineSzOrder, lineStoreSz, haveDirtyBit

	ldr		\tmp3, [\cacheAddrReg, #(1 << \lineSzOrder)]
	.if \haveDirtyBit
		lsrs	\tmp3, #1	//hide "dirty" bit
	.endif
	cmp		\lineRoundedAddr, \tmp3
	beq		\hitLbl
	adds	\cacheAddrReg, #\lineStoreSz

.endm

//search a given set's ways for a match. if no, leave pointer pointing past the last way and run off the end,
// if yes, leave it pointing ot the matched way and jump to "hitLbl"
// tmp2lineRoundedAddr is left with addr >> lineSzOrder, for later use
.macro cacheFindWy	cacheAddrReg /*in/out */, addr, hitLbl, numWays, tmp2lineRoundedAddr, tmp3, lineSzOrder, lineStoreSz, haveDirtyBit

	//see if we have a hit in any way in the set
	lsrs		\tmp2lineRoundedAddr, \addr, #\lineSzOrder
	
	.if ((\numWays) & 1)
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
	.endif
	.if ((\numWays) & 2)
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
	.endif
	.if ((\numWays) & 4)
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
	.endif
	.if ((\numWays) & 8)
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
		cacheChkWy	\cacheAddrReg, \hitLbl, \tmp2lineRoundedAddr, \tmp3, \lineSzOrder, \lineStoreSz, \haveDirtyBit
	.endif
	.if ((\numWays) >> 4)
		.error "that many ways not supported"
	.endif

.endm

.macro addrErr		va, isWrite, cc
	
	mov\cc		r0, \va
	.if	\isWrite
		bw		cpuPrvTakeaddrErrorW, \cc
	.else
		bw		cpuPrvTakeaddrErrorR, \cc
	.endif
.endm

.macro isUsrMode tmp		//leave 1 in carry if in user mode
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	lsrs		\tmp, #1 + BITNO_CP0_STATUS_KUC
.endm

.macro memXlate		tmp2, va, pa, isWrite, miscHandlerLbl	//va is not corrupted, no params may be same, will not return in fail case
	
	memXlateEx	\tmp2, \va, \va, \pa, \isWrite, \miscHandlerLbl, 0

.endm

.macro memRd	tmp, tmp2, dstReg, addrReg, loadOp, sizeOrder, specialLoad, instrLoad, miscHandlerLbl		//dst and addrReg MIGHT be the same
	
	.if	\instrLoad
		//speeed...
	.else
		algnChk	\tmp, \addrReg, \sizeOrder, 0
	.endif
	
	memXlate	\tmp2, \addrReg, \tmp, 0, \miscHandlerLbl	//"tmp" is dst pa

	lsrs		\tmp2, \tmp, #28			//see if RAM
	bne			95f							//not ram

// test if in graphics RAM range
	movs                    \tmp2, #0xfc
	lsls                    \tmp2, \tmp2, #20
	subs                    \tmp2, \tmp, \tmp2
	bcc			94f      // jump if not graphics 	
	ldr                     \tmp, =mFbBase
	ldr                     \tmp, [\tmp]
	adds		        \tmp, \tmp2
	b                       96f
	
// test if in allowed RAM range
94:	ldr			\tmp2, [REG_CPU_P2, #0 + OFST_MEMLIMIT]
	cmp			\tmp, \tmp2
	bcs			93f
96:	ramRead		\tmp, \sizeOrder, 97f, 98f, \loadOp	\dstReg

95:	//NOT RAM
	push		{r0-r3}
	mov			r0, \tmp
	movs		r1, #1 << \sizeOrder
	movs		r2, #0
	adds		r3, REG_CPU_P2, #0 + OFST_SPACE
	bl			memAccess	//(uint32_t addr, uint_fast8_t size, bool write, void* buf);
	cmp			r0, #0
	pop			{r0-r3}
	bne			97f

93:
//failure
	movs		r0, \tmp
	.if \instrLoad
		bw		cpuPrvTakeBusErrorI
	.else
		bw		cpuPrvTakeBusErrorD
	.endif
	
97:
	.if \specialLoad
		movs	\dstReg, #0 + OFST_SPACE
		\loadOp	\dstReg, [REG_CPU_P2, \dstReg]
	.else
		\loadOp	\dstReg, [REG_CPU_P2, #0 + OFST_SPACE]
	.endif

98:
.endm

.macro memWr	tmp, tmp2, srcReg, addrReg, strOp, sizeOrder, miscHandlerLbl

	algnChk		\tmp, \addrReg, \sizeOrder, 1
	
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	lsrs		\tmp, #1 + CP0_STATUS_ISC_SHIFT
	bcs			memWrIscSet\miscHandlerLbl\()_\addrReg		//ISC is set
	
	memXlate	\tmp2, \addrReg, \tmp, 1, \miscHandlerLbl	//"tmp" is dst pa

	lsrs		\tmp2, \tmp, #28			//see if RAM
	bne			95f							//not ram

// test if in graphics RAM range
	movs                    \tmp2, #0xfc
	lsls                    \tmp2, \tmp2, #20
	subs                    \tmp2, \tmp, \tmp2
	bcc			94f      // jump if not graphics 	
	ldr                     \tmp, =mFbBase
	ldr                     \tmp, [\tmp]
	adds		        \tmp, \tmp2
	b                       96f
	
// test if in allowed RAM range	
94:	ldr			\tmp2, [REG_CPU_P2, #0 + OFST_MEMLIMIT]
	cmp			\tmp, \tmp2
	bcs			93f
96:	ramWrite	\tmp, \srcReg, \strOp, \sizeOrder
	b			97f

95:	//NOT RAM
	\strOp		\srcReg, [REG_CPU_P2, #0 + OFST_SPACE]
	push		{r0-r3}
	mov			r0, \tmp
	movs		r1, #1 << \sizeOrder
	movs		r2, #1
	adds		r3, REG_CPU_P2, #0 + OFST_SPACE
	bl			memAccess	//(uint32_t addr, uint_fast8_t size, bool write, void* buf);
	cmp			r0, #0
	pop			{r0-r3}
	bne			97f

93:
//failure
	movs		r0, \tmp
	bw			cpuPrvTakeBusErrorD
	
97:
	
.endm

.macro memRdSB	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldrsb, 0, 1, 0, \miscHandlerLbl
.endm

.macro memRdSH	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldrsh, 1, 1, 0, \miscHandlerLbl
.endm

.macro memRdUB	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldrb, 0, 0, 0, \miscHandlerLbl
.endm

.macro memRdUH	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldrh, 1, 0, 0, \miscHandlerLbl
.endm

.macro memRdW	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldr, 2, 0, 0, \miscHandlerLbl
.endm

.macro memRdInstr	tmp, tmp2, dstReg, addrReg, miscHandlerLbl			//dstReg, addrReg, tmp, tmp2 must be distinct
	memRd		\tmp, \tmp2, \dstReg, \addrReg, ldr, 2, 0, 1, \miscHandlerLbl
.endm

.macro memWrB	tmp, tmp2, srcReg, addrReg, miscHandlerLbl
	memWr		\tmp, \tmp2, \srcReg, \addrReg, strb, 0, \miscHandlerLbl
.endm

.macro memWrH	tmp, tmp2, srcReg, addrReg, miscHandlerLbl
	memWr		\tmp, \tmp2, \srcReg, \addrReg, strh, 1, \miscHandlerLbl
.endm

.macro memWrW	tmp, tmp2, srcReg, addrReg, miscHandlerLbl
	memWr		\tmp, \tmp2, \srcReg, \addrReg, str, 2, \miscHandlerLbl
.endm

.macro memIscHandler num, adrReg
	memWrIscSet\num\()_\adrReg:
		/*
			You might think that it is worthwhile to flush the cache partially,
			but in reality this is so uncommon, that it is not worth it! If you
			do want to try, "adrReg" here contains the VA :)
		*/
		bl			cpuPrvIcacheFlushEntire
		endCyNoBra	REG_INSTR
.endm

.macro memMiscHandlers num, vaReport, forWrite
	
	.if \forWrite
	
		tlbInvalExcW\num\()_\vaReport:	//tlb invalid exception on write
			mov		r0, \vaReport
			bw 		cpuPrvTakeTlbInvalidExcW

		tlbModifiedExc\num\()_\vaReport:	//tlb modified exception
			mov		r0, \vaReport
			bw		cpuPrvTakeTlbModifiedExc
	.else
	
		tlbInvalExcR\num\()_\vaReport:	//tlb invalid exception on read
			mov		r0, \vaReport
			bw 		cpuPrvTakeTlbInvalidExcR
	
	.endif
.endm


cpuAsidChanged:		//corrupts p1
	
	addl		r0, REG_CPU, OFST_TLB	//get a pointer to the TLB
	movs		t2, #64
	
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	bfx			t1, t1, 6, 6
	
1:
	ldrb		p1, [r0, #0 + OFST_TLB_BITS]
	lsrs		r3, p1, #1 + TLB_FLAGS_BIT_G
	bcs			4f		//global -> always valid. we marked valid on insertion so nothing to do
	
	lsrs		p1, #1	//clear "valid" bit
	lsls		p1, #1
	
	ldrb		r3, [r0, #0 + OFST_TLB_ASID]
	cmp			r3, t1
	bne			3f		//no match -> in valid
2:
	adds		p1, #1

3:
	strb		p1, [r0, #0 + OFST_TLB_BITS]

4:
	adds		r0, #0 + SIZEOF_TLB_ENTRY
	subs		t2, #1
	bne			1b
	//fallthrough to cpuPrvIcacheFlushEntire

/*
	technically, an entry being overwritten in the TLB would also require this
	but since they are usually overwriten "random" order, nobody does this and
	we do not support it. This is, technically, not 100% correct. This CAN be
	fixed by calling cpuPrvIcacheFlushEntire from tlbWrite(). But the costs are
	high and payoff is nil for linux.
*/
cpuPrvIcacheFlushEntire:

	getM1		t1

	getIcPtr	t0
	loadImm		t2, ICACHE_NUM_WAYS * ICACHE_NUM_SETS
1:
	str			t1, [t0, #0 + OFST_ICACHE_ADDR]
	adds		t0, ICACHE_LINE_STOR_SZ
	subs		t2, t2, #1
	bne			1b
	bx			lr

.ltorg

cpuPrvIcacheFlushPage:	//r0 = va
	getM1		t1
	lsrs		t0, t0, #0 + PAGE_ORDER
	mov			r12, t0

	getIcPtr	t0
	loadImm		t2, ICACHE_NUM_WAYS * ICACHE_NUM_SETS
1:
	ldr			t3, [t0, #0 + OFST_ICACHE_ADDR]
	
	lsrs	t3, t3, #PAGE_ORDER - ICACHE_LINE_SZ_ORDER
	cmp		t3, r12

	bne		2f		//only invalidate lines if they match
	str		t1, [t0, #0 + OFST_ICACHE_ADDR]
2:

	adds		t0, ICACHE_LINE_STOR_SZ
	subs		t2, t2, #1
	bne			1b

	bx			lr



.globl cpuInit
cpuInit:			//(ramAmount) -> void
	push		{r0, REG_CPU, REG_CPU_P2, lr}
	ldr			REG_CPU, =mCpu
	mov			r0, REG_CPU
	movs		r1, #0
	ldr			r2, =CPU_SIZE
	bl			memset
	movs		REG_CPU_P2, #0 + OFST_PART2
	add			REG_CPU_P2, REG_CPU
	pop			{r0}
	str			r0, [REG_CPU_P2, #0 + OFST_MEMLIMIT]
	ldr			r0, =0xbfc00000
	str			r0, [REG_CPU_P2, #0 + OFST_PC]
	adds		r0, #4
	str			r0, [REG_CPU_P2, #0 + OFST_NPC]
	movs		r0, #0
	bl			cpuPrvIcacheFlushEntire			//expects REG_CPU/REG_CPU_P2 set
	
	//put all entries into a chain into bucket 0 (actual bucket does not matter as these entries do not map a VA)
	//start them all with va of 0x80000000
	movs		r0, #0
	ldr			r1, =mCpu + OFST_TLB
	movs		r3, #0x80
	lsls		r3, #24
1:
	subs		r2, r0, #1	//prev (and 0xff for 0th)
	adds		r0, #1		//next
	str			r0, [r1, #0 + OFST_TLB_VA]
	strb		r0, [r1, #0 + OFST_TLB_HASH_NEXT_IDX]
	strb		r2, [r1, #0 + OFST_TLB_HASH_PREV_IDX]
	adds		r1, #0 + SIZEOF_TLB_ENTRY
	cmp			r0, #0 + NUM_TLB_ENTRIES
	bne			1b

	//properly terminate the end of the chain
	subs		r1, #0 + SIZEOF_TLB_ENTRY
	movs		r0, #0xff
	strb		r0, [r1, #0 + OFST_TLB_HASH_NEXT_IDX]
	
	//first entry needs to properly name the bucket
	ldr			r1, =mCpu + OFST_TLB
	movs		r2, #0x80	//0th bucket
	strb		r2, [r1, #0 + OFST_TLB_HASH_PREV_IDX]

//mark all buckets as empty except first
	getTlbHptr	r0
	movs		r1, #0
	movs		r2, #0
1:
	strb		r2, [r0]
	movs		r2, #0xff
	adds		r0, #1
	adds		r1, #1
	cmp			r1, #0 + NUM_TLB_BUCKETS
	bne			1b
	
	pop			{REG_CPU, REG_CPU_P2, pc}
.ltorg


cpuPrvFinishSettingIrq:		//assumes: irq status in r0, mCpu + OFST_PART2 in r3. returns via lr
	ldrb		r1, [r3, #0 + OFST_IN_FPU_OP]
	cmp			r1, #0
	bne			1f
	setNextCyJump	r0, r1, jump_adrs_finish
1:
	cpsie		i
	bx			lr
nextCyJumpTbl	jump_adrs_finish

.globl cpuIrq
cpuIrq:				//(uint_fast8_t idx, bool raise) -> void
	movs		r2, #1
	adds		r0, #0 + CP0_CAUSE_IP_SHIFT
	lsls		r2, r0
	lsls		r1, r0
	ldr			r3, =mCpu + OFST_PART2
	cpsid		i	//do not operate on IRQSTATE with irqs on
	ldr			r0, [r3, #0 + OFST_CP0_CAUSE]
	bics		r0, r2
	orrs		r0, r1
	str			r0, [r3, #0 + OFST_CP0_CAUSE]
	recalcIrqs	r3, r1, r2, r0
	strb		r0, [r3, #0 + OFST_HAVE_IRQ]
	b			cpuPrvFinishSettingIrq
.ltorg

.globl cpuGetRegExternal
cpuGetRegExternal:	//(uint8_t reg) -> u32
	ldr			r1, =mCpu
	movs		r2, #0 + NUM_REGS
	subs		r2, r0, r2
	bge			1f
	lsls		r0, #2
	ldr			r0, [r1, r0]
	bx			lr
1:
	adds		r1, #0 + OFST_PART2
	lsls		r2, #2
	add			pc, r2
	nop
//32 + 0 = pc
	ldr			r0, [r1, #0 + OFST_PC]
	bx			lr
//32 + 1 = hi
	ldr			r0, [r1, #0 + OFST_HI]
	bx			lr
//32 + 2 = lo
	ldr			r0, [r1, #0 + OFST_LO]
	bx			lr
//32 + 3 = badva
	ldr			r0, [r1, #0 + OFST_CP0_BADVA]
	bx			lr
//32 + 4 = cause
	ldr			r0, [r1, #0 + OFST_CP0_CAUSE]
	bx			lr
//32 + 5 = status
	ldr			r0, [r1, #0 + OFST_CP0_STATUS]
	bx			lr
//32 + 6 = entrylo
	ldr			r0, [r1, #0 + OFST_CP0_ENTRYLO]
	bx			lr
//32 + 7 = entryhi
	ldr			r0, [r1, #0 + OFST_CP0_ENTRYHI]
	bx			lr

.ltorg


.globl cpuSetRegExternal
cpuSetRegExternal:	// (uint8_t reg, uint32_t val)
	ldr			r3, =mCpu
	movs		r2, #0 + NUM_REGS
	subs		r2, r0, r2
	bge			1f
	lsls		r0, #2
	beq			2f	//skip for zero reg
	str			r1, [r3, r0]
2:
	bx			lr
1:
	adds		r3, #0 + OFST_PART2
	lsls		r2, #2
	add			pc, r2
	nop
//32 + 0 = pc
	str			r0, [r3, #0 + OFST_PC]
	bx			lr
//32 + 1 = hi
	str			r0, [r3, #0 + OFST_HI]
	bx			lr
//32 + 2 = lo
	str			r0, [r3, #0 + OFST_LO]
	bx			lr
//32 + 3 = badva
	str			r0, [r3, #0 + OFST_CP0_BADVA]
	bx			lr
//32 + 4 = cause
	cpsid		i	//do not operate on IRQSTATE with irqs on
	str			r0, [r3, #0 + OFST_CP0_CAUSE]
	recalcIrqs	r3, r1, r2, r0
	strb		r0, [r3, #0 + OFST_HAVE_IRQ]
	b			cpuPrvFinishSettingIrq
//32 + 5 = status
	cpsid		i	//do not operate on IRQSTATE with irqs on
	str			r2, [r3, #0 + OFST_CP0_STATUS]
	calcIrqWSta	r3, r1, r0, r2
	strb		r0, [r3, #0 + OFST_HAVE_IRQ]
	b			cpuPrvFinishSettingIrq
.ltorg

cpuPrvTakeExceptionWithZeroVector:
	movs		r1, #0
	//fallthrough

cpuPrvTakeExceptionWithVector:		//(r0 = cause << CP0_CAUSE_EXC_COD_SHIFT, r1 = vector) does not return. needs to end cycle as needed

	movs		r2, #0x80
	lsls		r2, #24
	orrs		r1, r2					//vector addr
	mov			r2, REG_NOT_DELAY_SLOT	//4 if not in delay slot, 0 if yes
	movs		r3, #4
	mov			REG_NOT_DELAY_SLOT, r3	//we're not in a delay slot now in the irq handler
	movs		r3, #0
	cpsid		i	//do not operate on IRQSTATE with irqs on
	strb		r3, [REG_CPU_P2, #0 + OFST_HAVE_IRQ]	//we are about to disable irqs, so we won't have one pending by definition
	ldr			r3, =do_cycle + 1						//account for that
	mov			REG_DO_NEXT_CY, r3
	subs		r3, r2, #4				//0 if not in delay slot, -4 if yes
	lsrs		r2, r3, #31				//1 if in delay slot, 0 else
	add			r3, REG_PC				//epc
	str			r3, [REG_CPU_P2, #0 + OFST_CP0_EPC]
	mov			REG_PC, r1				//set PC and NPC to vector
	adds		r1, #4
	mov			REG_NPC, r1
	ldr			r3, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	ldr			r1, =(CP0_CAUSE_EXC_COD_MASK | CP0_CAUSE_BD)
	bics		r3, r1	//clear cause and BD
	lsls		r2, #0 + CP0_CAUSE_BD_SHIFT
	orrs		r3, r2					//set CAUSE.BD with "isInDelaySlot", keep new "cause" val in r3
	orrs		r3, r0					//set cause field
	str			r3, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	ldr			r3, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	movs		r1, #0 | CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE
	movs		r2, #0 | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE
	ands		r2, r3
	lsls		r2, r2, #2
	bics		r3, r1
	orrs		r3, r2
	str			r3, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	cpsie		i
	endCy		r0, 0

cpuPrvTakeException:
	movs		r1, #0x80	//vector
	b			cpuPrvTakeExceptionWithVector

cpuPrvTakeTrapExc:
	movs		r0, #(CP0_EXC_COD_TR << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException
	
cpuPrvTakeaddrErrorR:	//take an address error exception on read. r0 = va
	setBadVA	r1, r0, r0
	movs		r0, #(CP0_EXC_COD_ADEL << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

cpuPrvTakeaddrErrorW:	//take an address error exception on write. r0 = va
	setBadVA	r1, r0, r0
	movs		r0, #(CP0_EXC_COD_ADES << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

cpuPrvTakeTlbRefillExcR:	//take a tlb refill exception on read. r0 = va
	setBadVA	r1, r2, r0
	setEntryHi	r1, r2, r0
	lsls		r1, r0, #1	//top bit into C
	movs		r0, #(CP0_EXC_COD_TLBL << CP0_CAUSE_EXC_COD_SHIFT)
	bcs			refill_nonku_r
refull_ku_r:	//TODO: this check can be avoided by splitting this func in 2 and improving memXlate
	b			cpuPrvTakeExceptionWithZeroVector
refill_nonku_r:
	b			cpuPrvTakeException

cpuPrvTakeTlbRefillExcW:	//take a tlb refill exception on write. r0 = va
	setBadVA	r1, r2, r0
	setEntryHi	r1, r2, r0
	lsls		r1, r0, #1	//top bit into C
	movs		r0, #(CP0_EXC_COD_TLBS << CP0_CAUSE_EXC_COD_SHIFT)
	bcs			refill_nonku_w
refull_ku_w:	//TODO: this check can be avoided by splitting this func in 2 and improving memXlate
	b			cpuPrvTakeExceptionWithZeroVector
refill_nonku_w:
	b			cpuPrvTakeException

cpuPrvTakeTlbModifiedExc:	//take a tlb modified exception on read. r0 = va
	setBadVA	r1, r2, r0
	setEntryHi	r1, r2, r0
	movs		r0, #(CP0_EXC_COD_MOD << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

cpuPrvTakeTlbInvalidExcR:	//take a tlb invalid exception on read. r0 = va
	setBadVA	r1, r2, r0
	setEntryHi	r1, r2, r0
	movs		r0, #(CP0_EXC_COD_TLBL << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

cpuPrvTakeTlbInvalidExcW:	//take a tlb invalid exception on write. r0 = va
	setBadVA	r1, r2, r0
	setEntryHi	r1, r2, r0
	movs		r0, #(CP0_EXC_COD_TLBS << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

cpuPrvTakeBusErrorI:		//takes a bus error exception on instruction fetch. r0 = pa
	bl			decReportBusErrorAddr
	movs		r0, #(CP0_EXC_COD_IBE << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException
	
cpuPrvTakeBusErrorD:		//takes a bus error exception on data access. r0 = pa
	bl			decReportBusErrorAddr
	movs		r0, #(CP0_EXC_COD_DBE << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

signal_overflow_2:
	movs		r0, #(CP0_EXC_COD_OV << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

instr_001000_addi:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	sxth		t2, REG_INSTR
	adds		t1, t2
	bvs			signal_overflow_2	//shared path for space saving
	setRegT_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

instr_001001_addiu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	sxth		t2, REG_INSTR
	adds		t1, t2
	setRegT_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

instr_001010_slti:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	sxth		t2, REG_INSTR
	scc			t3, t1, t2, lt, ge
	setRegT_f	t2, REG_INSTR, t3, t0
	endCyNoBra	REG_INSTR

instr_001011_sltiu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	sxth		t2, REG_INSTR
	scc			t3, t1, t2, lo, hs
	setRegT_f	t2, REG_INSTR, t3, t0
	endCyNoBra	REG_INSTR

instr_001100_andi:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	uxth		t2, REG_INSTR
	ands		t1, t2
	setRegT_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

instr_001101_ori:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	uxth		t2, REG_INSTR
	orrs		t1, t2
	setRegT_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

instr_001110_xori:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	uxth		t2, REG_INSTR
	eors		t1, t2
	setRegT_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

instr_001111_lui:
	lsls		t1, REG_INSTR, #16
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

condbr_likely_not_taken_3:
	endCySkpNxt REG_INSTR

instr_010100_beql:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bne			condbr_likely_not_taken_3
	relbra		t1, t2, REG_INSTR

instr_010101_bnel:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	beq			condbr_likely_not_taken_3
	relbra		t1, t2, REG_INSTR

instr_010110_blezl:
	getRegS		t1, REG_INSTR
	cmp			t1, #0
	bgt			condbr_likely_not_taken_3
	relbra		t1, t2, REG_INSTR

instr_010111_bgtzl:
	getRegS		t1, REG_INSTR
	cmp			t1, #0
	ble			condbr_likely_not_taken_3
	relbra		t1, t2, REG_INSTR

.globl cpuCycle		/// more than one, really
cpuCycle:


#if 0			//we never really return, why pretend?

	push		{REG_CPU, REG_CPU_P2,lr}
	mov			r0, REG_PC
	mov			r1, REG_NPC
	mov			r2, REG_NOT_DELAY_SLOT
	push		{r0, r1, r2, r}

#else			//let's actually use our stack

//	ldr			r4, =__stack_top
	// RP2040 build env changed the name:
	ldr			r4, =__StackTop
	mov			sp, r4

#endif

	ldr			t0, =do_cycle + 1
	mov			REG_DO_NEXT_CY, t0
	ldr			REG_CPU, =mCpu
	movs		REG_CPU_P2, #0 + OFST_PART2
	add			REG_CPU_P2, REG_CPU
	loadState	t3

	b			do_cycle

.ltorg


handle_irq:
	movs		r0, #(CP0_EXC_COD_IRQ << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException
	
do_cycle:

	//get PC
	mov			p1, REG_PC
	
#ifdef DISABLE_ICACHE
	
	memRdInstr	t0, t1, REG_INSTR, p1, 99
	b interp_instr
	
	memMiscHandlers 99, p1, 0	//for the load above
	
#else

	//get pointer to icache
	getIcPtr	REG_INSTR
	
	//pick an icache set
	cachePickSt	REG_INSTR, p1, t0, t1, ICACHE_LINE_SZ_ORDER, ICACHE_LINE_STOR_SZ, ICACHE_NUM_SETS_ORDER, ICACHE_NUM_WAYS

	//find matching way (if any), leave addr >> ICACHE_LINE_SZ_ORDER in t2
	cacheFindWy	REG_INSTR, p1, icache_hit, ICACHE_NUM_WAYS, t2, t3, ICACHE_LINE_SZ_ORDER, ICACHE_LINE_STOR_SZ, 0

icache_miss:
	
	//pick a victim line
	cachePickVc	REG_INSTR, t0, t1, ICACHE_NUM_WAYS_ORDER, ICACHE_LINE_STOR_SZ

	lsls		t2, t2, #0 + ICACHE_LINE_SZ_ORDER	//va to actually read

	memXlateEx	t0, t2, p1, t3, 0, 99, 0	//t3 is PA

	lsrs		t0, t3, #28					//see if RAM
	bne			instrs_from_nonram			//not ram

	//we were careful to not need to push things here
//RAM
	ramRead_ic	t3, REG_INSTR
	b			icache_filled

memMiscHandlers 99, p1, 0	//for the load above

//MEM load fail
instr_cache_fill_fail:
	movs		r0, t3
	b			cpuPrvTakeBusErrorI

instrs_from_nonram:	//NOT RAM
	push		{t3}
	mov			r0, t3
	movs		r1, #1 << ICACHE_LINE_SZ_ORDER
	movs		r2, #0
	mov			r3, REG_INSTR
	bl			memAccess	//(uint32_t addr, uint_fast8_t size, bool write, void* buf);
	cmp			r0, #0
	pop			{t3}
	beq			instr_cache_fill_fail

icache_filled:
	lsrs		t2, p1, #0 + ICACHE_LINE_SZ_ORDER
	str			t2, [REG_INSTR, #OFST_ICACHE_ADDR]

icache_hit:

	bfx			p1, p1, 0, ICACHE_LINE_SZ_ORDER
	ldr			REG_INSTR, [REG_INSTR, p1]
#endif

interp_instr:
	
/*
	///aaa
	push		{r0-r3}
	mov			r1, REG_PC
	mov			r0, REG_INSTR
	bl			cycleReport
	pop			{r0-r3}
	///aaa
//*/

	brOnBitsL	t1, t2, REG_INSTR, 26, 6
	brCaseLnear	instr_000000_special0
	brCaseLnear	instr_000001_condbr
	brCaseLnear	instr_000010_j
	brCaseLnear	instr_000011_jal
	brCaseLnear	instr_000100_beq
	brCaseLnear	instr_000101_bne
	brCaseLnear	instr_000110_blez
	brCaseLnear	instr_000111_bgtz
	brCaseLnear	instr_001000_addi
	brCaseLnear	instr_001001_addiu
	brCaseLnear	instr_001010_slti
	brCaseLnear	instr_001011_sltiu
	brCaseLnear	instr_001100_andi
	brCaseLnear	instr_001101_ori
	brCaseLnear	instr_001110_xori
	brCaseLnear	instr_001111_lui
	brCaseLfar	instr_010000_cop0
	brCaseLfar	instr_010001_cop1
	brCaseLfar	instr_010010_cop2
	brCaseLfar	instr_010011_cop3
	brCaseLnear	instr_010100_beql
	brCaseLnear	instr_010101_bnel
	brCaseLnear	instr_010110_blezl
	brCaseLnear	instr_010111_bgtzl
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_100000_lb
	brCaseLfar	instr_100001_lh
	brCaseLfar	instr_100010_lwl
	brCaseLfar	instr_100011_lw
	brCaseLfar	instr_100100_lbu
	brCaseLfar	instr_100101_lhu
	brCaseLfar	instr_100110_lwr
	brCaseLfar	instr_invalid
	brCaseLfar	instr_101000_sb
	brCaseLfar	instr_101001_sh
	brCaseLfar	instr_101010_swl
	brCaseLfar	instr_101011_sw
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_101110_swr
	brCaseLfar	instr_invalid
	brCaseLfar	instr_110000_ll
	brCaseLfar	instr_110001_lwc1
	brCaseLfar	instr_110010_lwc2
	brCaseLfar	instr_110011_pref
	brCaseLfar	instr_invalid
	brCaseLfar	instr_110101_ldc1
	brCaseLfar	instr_110110_ldc2
	brCaseLfar	instr_invalid
	brCaseLfar	instr_111000_sc
	brCaseLfar	instr_111001_swc1
	brCaseLfar	instr_111010_swc2
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_111101_sdc1
	brCaseLfar	instr_111110_sdc2
	brCaseLfar	instr_invalid
.ltorg

instr_000011_jal:
	getPC_p8	t2
	setRa		t2
	//fallthrough
	
instr_000010_j:
	mov			t1, REG_NPC

#ifdef ARMv7
	bfi			t1, REG_INSTR, 2, 26	//works
#else
	bfx			t3, REG_INSTR, -2, 28	//works
	lsrs		t1, t1, #28
	lsls		t1, t1, #28
	orrs		t1, t3
#endif
	endCyBranch	t2, t1

instr_000100_beq:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bne			condbr_not_taken_2
	relbra		t1, t2, REG_INSTR

instr_000101_bne:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	beq			condbr_not_taken_2
	relbra		t1, t2, REG_INSTR

instr_000110_blez:
	getRegS		t1, REG_INSTR
	cmp			t1, #0
	bgt			condbr_not_taken_2
	relbra		t1, t2, REG_INSTR

instr_000111_bgtz:
	getRegS		t1, REG_INSTR
	cmp			t1, #0
	ble			condbr_not_taken_2
	relbra		t1, t2, REG_INSTR

condbr_not_taken_2:
	endCyNoBra	REG_INSTR

instr_000000_special0:		//SPECIAL0
	brOnBitsL	t1, t0, REG_INSTR, 0, 6
	brCaseLnear	special0_000000_sll
	brCaseLfar	instr_invalid
	brCaseLnear	special0_000010_srl
	brCaseLnear	special0_000011_sra
	brCaseLnear	special0_000100_sllv
	brCaseLfar	instr_invalid
	brCaseLnear	special0_000110_srlv
	brCaseLnear	special0_000111_srav
	brCaseLnear	special0_001000_jr
	brCaseLnear	special0_001001_jalr
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLnear	special0_001100_syscall
	brCaseLnear	special0_001101_break
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLnear	special0_010000_mfhi
	brCaseLnear	special0_010001_mthi
	brCaseLnear	special0_010010_mflo
	brCaseLnear	special0_010011_mtlo
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLnear	special0_011000_mult
	brCaseLnear	special0_011001_multu
	brCaseLnear	special0_011010_div
	brCaseLnear	special0_011011_divu
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLnear	special0_100000_add
	brCaseLnear	special0_100001_addu
	brCaseLnear	special0_100010_sub
	brCaseLnear	special0_100011_subu
	brCaseLnear	special0_100100_and
	brCaseLnear	special0_100101_or
	brCaseLnear	special0_100110_xor
	brCaseLnear	special0_100111_nor
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLnear	special0_101010_slt
	brCaseLnear	special0_101011_sltu
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	special0_110000_tge
	brCaseLfar	special0_110001_tgeu
	brCaseLfar	special0_110010_tlt
	brCaseLfar	special0_110011_tltu
	brCaseLfar	special0_110100_teq
	brCaseLfar	instr_invalid
	brCaseLfar	special0_110110_tne
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid
	brCaseLfar	instr_invalid

special0_000000_sll:
	lsrs		t3, REG_INSTR, #9
	bne			sll_not_nop

is_nop:
	endCyNoBra	REG_INSTR

sll_not_nop:
	prepare_f	t0
	ands		t3, t0					//regD index
	beq			is_nop					//weird non-zeor shift into dummy reg
	
	getRegT_f	t1, REG_INSTR, t0
	getShamt	t2, REG_INSTR
	lsls		t1, t2
	str			t1, [REG_CPU, t3]
	endCyNoBra	REG_INSTR

special0_000010_srl:
	prepare_f	t0
	getRegT_f	t1, REG_INSTR, t0
	getShamt	t2, REG_INSTR
	lsrs		t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_000011_sra:
	prepare_f	t0
	getRegT_f	t1, REG_INSTR, t0
	getShamt	t2, REG_INSTR
	asrs		t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_000100_sllv:
	prepare_f	t0
	getRegT_f	t1, REG_INSTR, t0
	getRegS_f	t2, REG_INSTR, t0
	bfx			t2, t2, 0, 5
	lsls		t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_000110_srlv:
	prepare_f	t0
	getRegT_f	t1, REG_INSTR, t0
	getRegS_f	t2, REG_INSTR, t0
	bfx			t2, t2, 0, 5
	lsrs		t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_000111_srav:
	prepare_f	t0
	getRegT_f	t1, REG_INSTR, t0
	getRegS_f	t2, REG_INSTR, t0
	bfx			t2, t2, 0, 5
	asrs		t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_001000_jr:
	getRegS		t1, REG_INSTR
	//TODO: we can check for unaligned pc here
	endCyBranch t2, t1

special0_001001_jalr:
	getRegS		t1, REG_INSTR
	//TODO: we can check for unaligned pc here
	getPC_p8	t2
	setRa		t2
	endCyBranch t2, t1

special0_001100_syscall:
	movs		r0, #(CP0_EXC_COD_SYS << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

special0_001101_break:
	movs		r0, #(CP0_EXC_COD_BP << CP0_CAUSE_EXC_COD_SHIFT)
	b			cpuPrvTakeException

special0_010000_mfhi:
	ldr			t1, [REG_CPU_P2, #0 + OFST_HI]
	setRegD		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

special0_010001_mthi:
	getRegS		t1, REG_INSTR
	str			t1, [REG_CPU_P2, #0 + OFST_HI]
	endCyNoBra	REG_INSTR

special0_010010_mflo:
	ldr			t1, [REG_CPU_P2, #0 + OFST_LO]
	setRegD		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

special0_010011_mtlo:
	getRegS		t1, REG_INSTR
	str			t1, [REG_CPU_P2, #0 + OFST_LO]
	endCyNoBra	REG_INSTR

special0_011000_mult:
	prepare_f	t2
	getRegT_f	t0, REG_INSTR, t2
	getRegS_f	t1, REG_INSTR, t2
	smul32x32_to_64	t0, t1, t3, t2, REG_INSTR, p1	//	t0 * t1 -> t3:t2
	str			t3, [REG_CPU_P2, #0 + OFST_HI]
	str			t2, [REG_CPU_P2, #0 + OFST_LO]
	endCyNoBra	REG_INSTR
	
special0_011001_multu:
	prepare_f	t2
	getRegT_f	t0, REG_INSTR, t2
	getRegS_f	t1, REG_INSTR, t2
	umul32x32_to_64	t0, t1, t3, t2, REG_INSTR, p1	//	t0 * t1 -> t3:t2
	str			t3, [REG_CPU_P2, #0 + OFST_HI]
	str			t2, [REG_CPU_P2, #0 + OFST_LO]
	endCyNoBra	REG_INSTR

special0_011010_div:
	prepare_f	t2
	getRegT_f	r1, REG_INSTR, t2
	cbz			r1, 1f
	getRegS_f	r0, REG_INSTR, t2
	sdivmod
1:
	endCyNoBra	REG_INSTR

special0_011011_divu:
	prepare_f	t2
	getRegT_f	r1, REG_INSTR, t2
	cbz			r1, 1f
	getRegS_f	r0, REG_INSTR, t2
	udivmod
1:
	endCyNoBra	REG_INSTR
.ltorg
special0_100000_add:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	adds		t1, t1, t2
	bvs			signal_overflow	//shared path for space saving
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

signal_overflow:
	movs		REG_INSTR, #(CP0_EXC_COD_OV << CP0_CAUSE_EXC_COD_SHIFT)
	bw			cpuPrvTakeException

special0_100001_addu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	adds		t1, t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100010_sub:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	subs		t1, t1, t2
	bvs			signal_overflow	//shared path for space saving
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100011_subu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	subs		t1, t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100100_and:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	ands		t1, t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100101_or:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	orrs		t1, t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100110_xor:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	eors		t1, t1, t2
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_100111_nor:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	orrs		t1, t1, t2
	mvns		t1, t1
	setRegD_f	t2, REG_INSTR, t1, t0
	endCyNoBra	REG_INSTR

special0_101010_slt:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	scc			t3, t1, t2, lt, ge
	setRegD_f	t2, REG_INSTR, t3, t0
	endCyNoBra	REG_INSTR

special0_101011_sltu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	scc			t3, t1, t2, lo, hs
	setRegD_f	t2, REG_INSTR, t3, t0
	endCyNoBra	REG_INSTR

condbr_01010_tlti:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, lt, ge
	endCyNoBra	REG_INSTR

condbr_01011_tltiu:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, lo, hs
	endCyNoBra	REG_INSTR

condbr_01100_teqi:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, eq, ne
	endCyNoBra	REG_INSTR

condbr_01110_tnei:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, ne, eq
	endCyNoBra	REG_INSTR

instr_000001_condbr:
	brOnBitsS	t1, t0, REG_INSTR, 16, 5
	brCaseS		condbr_00000_bltz
	brCaseS		condbr_00001_bgez
	brCaseS		condbr_00010_bltzl
	brCaseS		condbr_00011_bgezl
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_01000_tgei
	brCaseS		condbr_01001_tgeiu
	brCaseS		condbr_01010_tlti
	brCaseS		condbr_01011_tltiu
	brCaseS		condbr_01100_teqi
	brCaseS		condbr_inval
	brCaseS		condbr_01110_tnei
	brCaseS		condbr_inval
	brCaseS		condbr_10000_bltzal
	brCaseS		condbr_10001_bgezal
	brCaseS		condbr_10010_bltzall
	brCaseS		condbr_10011_bgezall
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseS		condbr_inval
	brCaseSdone

condbr_inval:
	bw			instr_invalid

condbr_01000_tgei:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, ge, lt
	endCyNoBra	REG_INSTR

condbr_01001_tgeiu:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, hs, lo
	endCyNoBra	REG_INSTR
	
condbr_10000_bltzal:
	getPC_p8	t1
	setRa		t1
	//fallthrough

condbr_00000_bltz:
	getRegS		t1, REG_INSTR
	movs		t1, t1
	bpl			condbr_not_taken
//taken
	relbra 		t1, t2, REG_INSTR

condbr_not_taken:
	endCyNoBra	REG_INSTR

condbr_10001_bgezal:
	getPC_p8	t1
	setRa		t1
	//fallthrough

condbr_00001_bgez:
	getRegS		t1, REG_INSTR
	movs		t1, t1
	bmi			condbr_not_taken
	relbra 		t1, t2, REG_INSTR

condbr_10010_bltzall:
	getPC_p8	t1
	setRa		t1
	//fallthrough

condbr_00010_bltzl:
	getRegS		t1, REG_INSTR
	movs		t1, t1
	bpl			condbr_likely_not_taken
//taken
	relbra 		t1, t2, REG_INSTR

condbr_likely_not_taken:
	endCySkpNxt REG_INSTR

condbr_10011_bgezall:
	getPC_p8	t1
	setRa		t1
	//fallthrough

condbr_00011_bgezl:
	getRegS		t1, REG_INSTR
	movs		t1, t1
	bmi			condbr_likely_not_taken
	relbra 		t1, t2, REG_INSTR

instr_100000_lb:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	memRdSB		t3, t0, t2, t1, 5
	setRegT_f	t1, REG_INSTR, t2, p1
	endCyNoBra	REG_INSTR

memMiscHandlers 5, t1, 0	//for lb and lh

instr_100001_lh:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	memRdSH		t3, t0, t2, t1, 5
	setRegT_f	t1, REG_INSTR, t2, p1
	endCyNoBra	REG_INSTR

instr_100010_lwl:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t1, t2
	lsrs		t3, t1, #2
	lsls		t3, t3, #2			//word addr
	subs		t1, t3, t1			//-byte_ofst
	adds		t1, #3				//3 - byte_ofst
	lsls		t1, #3				//8 * (3 - byte_ofst)
	memRdW		p1, t0, t2, t3, 6	//loaded word
	lsls		t2, t1				//val to ORR in
	negs		t1, t1				//-8 * (3 - byte_ofst)
	adds		t1, #32				//32 - 8 * (3 - byte_ofst)
	getRegT		t3, REG_INSTR		//orig reg value
	lsls		t3, t1				//value that stays
	lsrs		t3, t1
	orrs		t3, t2				//final value
	setRegT		t2, REG_INSTR, t3
	endCyNoBra	REG_INSTR

memMiscHandlers 6, t3, 0	//for lwl & lw

instr_100011_lw:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t3, t1, t2
	memRdW		t1, t0, t2, t3, 6
	setRegT_f	t1, REG_INSTR, t2, p1
	endCyNoBra	REG_INSTR
.ltorg

instr_100100_lbu:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	memRdUB		t3, t0, t2, t1, 7
	setRegT_f	t1, REG_INSTR, t2, p1
	endCyNoBra	REG_INSTR

memMiscHandlers 7, t1, 0	//for lbu & lhu

instr_100101_lhu:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	memRdUH		t3, t0, t2, t1, 7
	setRegT_f	t1, REG_INSTR, t2, p1
	endCyNoBra	REG_INSTR

instr_100110_lwr:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t1, t2
	lsrs		t3, t1, #2
	lsls		t3, t3, #2			//word addr
	subs		t1, t3				//byte_ofst
	lsls		t1, #3				//8 * byte_ofst
	memRdW		p1, t0, t2, t3, 8	//loaded word
	lsrs		t2, t1				//val to ORR in
	negs		t1, t1				//-8 * byte_ofst
	adds		t1, #32				//32 - 8 * byte_ofst
	getRegT		t3, REG_INSTR		//orig reg value
	lsrs		t3, t1				//value that stays
	lsls		t3, t1
	orrs		t3, t2				//final value
	setRegT		t2, REG_INSTR, t3
	endCyNoBra	REG_INSTR
.ltorg

memMiscHandlers 8, t3, 0	//for lwr

instr_101000_sb:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	getRegT_f	t2, REG_INSTR, p1
	memWrB		t3, t0, t2, t1, 1
	endCyNoBra	REG_INSTR

memIscHandler	1, t1
memMiscHandlers 1, t1, 1	//for sb & sh

instr_101001_sh:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t1, t2
	getRegT_f	t2, REG_INSTR, p1
	memWrH		t3, t0, t2, t1, 1
	endCyNoBra	REG_INSTR

memMiscHandlers 2, t2, 0	//for swl

instr_101010_swl:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t1, t2
	lsrs		t2, t1, #2
	lsls		t2, t2, #2			//word addr
	subs		t1, t2, t1			//-byte_ofst
	adds		t1, #3				//3 - byte_ofst
	lsls		t1, #3				//8 * (3 - byte_ofst)
	memRdW		p1, t0, t3, t2, 2	//loaded word
	getRegT		p1, REG_INSTR		//orig reg value
	lsrs		p1, t1
	negs		t1, t1				//-8 * (3 - byte_ofst)
	adds		t1, #32				//32 - 8 * (3 - byte_ofst)
	lsrs		t3, t1
	lsls		t3, t1
	orrs		t3, p1
	memWrW		t0, t1, t3, t2, 2
	endCyNoBra	REG_INSTR
.ltorg

memMiscHandlers 2, t2, 1	//for swl & sw
memIscHandler	2, t2

instr_101011_sw:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t2, t1
	getRegT_f	t1, REG_INSTR, p1
	memWrW		t3, t0, t1, t2, 2
	endCyNoBra	REG_INSTR


memMiscHandlers 3, t2, 0	//for swr

instr_101110_swr:
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t1, t2
	lsrs		t2, t1, #2
	lsls		t2, t2, #2			//word addr
	subs		t1, t2				//byte_ofst
	lsls		t1, #3				//8 * byte_ofst
	memRdW		p1, t0, t3, t2, 3	//loaded word
	getRegT		p1, REG_INSTR		//orig reg value
	lsls		p1, t1
	negs		t1, t1				//-8 * byte_ofst
	adds		t1, #32				//32 - 8 * byte_ofst
	lsls		t3, t1
	lsrs		t3, t1
	orrs		t3, p1
	memWrW		t0, t1, t3, t2, 3
	endCyNoBra	REG_INSTR
.ltorg

memIscHandler	3, t2
memMiscHandlers 3, t2, 1	//for swr


special0_110000_tge:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, ge, lt
	endCyNoBra	REG_INSTR

special0_110001_tgeu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, hs, lo
	endCyNoBra	REG_INSTR

special0_110010_tlt:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, lt, ge
	endCyNoBra	REG_INSTR

special0_110011_tltu:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, lo, hs
	endCyNoBra	REG_INSTR

special0_110100_teq:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, eq, ne
	endCyNoBra	REG_INSTR

special0_110110_tne:
	prepare_f	t0
	getRegS_f	t1, REG_INSTR, t0
	getRegT_f	t2, REG_INSTR, t0
	cmp			t1, t2
	bwcc		cpuPrvTakeTrapExc, ne, eq
	endCyNoBra	REG_INSTR

instr_110011_pref:
	endCyNoBra	REG_INSTR

instr_010010_cop2:
instr_110010_lwc2:
instr_110110_ldc2:
instr_111010_swc2:
instr_111110_sdc2:
	copCheck	t1, t2, #2

instr_010011_cop3:
	
	isUsrMode	t1
	bcs			instr_invalid
	
	ldr			t1, =#0 + HYPERCALL
	cmp			REG_INSTR, t1
	bne			instr_invalid
	saveState	t1
	mov			r0, REG_CPU
	bl			cpuExtHypercall
	loadState	t1
	cmp			r0, #0
	beq			instr_invalid
	endCyNoBra	REG_INSTR
.ltorg

instr_invalid:
/*
///aaa
	mov			r0, REG_PC
	mov			r1, REG_INSTR
	bl			reportInvalid
///aaa
*/
	movs		r0, #(CP0_EXC_COD_RI << CP0_CAUSE_EXC_COD_SHIFT)
	bw			cpuPrvTakeException	

//////COP0 ops


tlbWrite:	//r0 = index to write
	push		{r0, lr}
	//get pointer to entry
	movs		r1, #0 + SIZEOF_TLB_ENTRY
	muls		r0, r1
	adds		r0, OFST_TLB
	adds		r0, REG_CPU	
	
	//fill entry (ignore bucket number for now)
	ldr			r1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYLO]
	lsrs		r2, r1, #0 + TLB_ENTRYLO_FLAGS_SHIFT
	adds		r2, #1	//mark it as enabled //as we write it, we get ASID from entryHi, which means that by definition at the current point in time, this entry IS enabled
	strb		r2, [r0, #0 + OFST_TLB_BITS]
	lsrs		r1, r1, #0 + PAGE_ORDER
	lsls		r1, r1, #0 + PAGE_ORDER
	str			r1, [r0, #0 + OFST_TLB_PA]
	ldr			r1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	bfx			r2, r1, 6, 6			//asid
	subs		r1, r2					//va (assume low bits are clear as they must be)
	ldr			r3, [r0, #0 + OFST_TLB_VA]		//read old VA
	mov			lr, r3
	str			r1, [r0, #0 + OFST_TLB_VA]		//store new va
	strb		r2, [r0, #0 + OFST_TLB_ASID]	//store asid
	
	//calc new bucket index, save it for later
	tlbDoHash	r3, r1
	mov			r12, r3

unlink_from_cur_bucket:
	ldrb		r2, [r0, #0 + OFST_TLB_HASH_NEXT_IDX]		//current "next" value
	ldrb		r3, [r0, #0 + OFST_TLB_HASH_PREV_IDX]		//current "prev" value
	cmp			r2, #0xff
	beq			current_next_handled

currently_have_next:
	movs		r1, #0 + SIZEOF_TLB_ENTRY
	muls		r1, r2
	adds		r1, #0 + OFST_TLB + OFST_TLB_HASH_PREV_IDX
	strb		r3, [REG_CPU, r1]							//set next's "prev" to whatver our "prev" is

current_next_handled:
	lsls		r3, #25		//check top bit by shifting it into C
	bcc			currently_middle_of_list

currently_head_of_list:
	lsrs		r3, #25		//bucket number
	loadImm		r1, OFST_TLB_HASH
	adds		r1, r3
	strb		r2, [REG_CPU, r1]
	b			current_list_removal_done

currently_middle_of_list:
	lsrs		r3, #25		//bucket number
	movs		r1, #0 + SIZEOF_TLB_ENTRY
	muls		r3, r1
	adds		r3, #0 + OFST_TLB + OFST_TLB_HASH_NEXT_IDX
	strb		r2, [REG_CPU, r3]							//store our "next" into "prev"'s "next"

current_list_removal_done:
	//now we need to insert it
	mov			r3, r12
	adds		r3, #0x80
	strb		r3, [r0, #0 + OFST_TLB_HASH_PREV_IDX]	//we have no prev, store bucket number with proper 0x80 bit set
	pop			{r3}		//entry index for entry we just added
	
	//r12 is bucket we belong in, r3 is our entry index

	loadImm		r1, OFST_TLB_HASH
	add			r1, r12
	ldrb		r2, [REG_CPU, r1]	//index of previous head
	strb		r3, [REG_CPU, r1]	//write us as new head index
	strb		r2, [r0, #0 + OFST_TLB_HASH_NEXT_IDX]	//store prev head as our next
	
	//if "next" exists, we need to set its "prev"
	cmp			r2, #0xff
	beq			insertion_completed

have_new_next:
	movs		r1, #0 + SIZEOF_TLB_ENTRY
	muls		r2, r1
	adds		r2, #0 + OFST_TLB + OFST_TLB_HASH_PREV_IDX
	strb		r3, [REG_CPU, r2]

insertion_completed:
	mov			r0, lr
	bl			cpuPrvIcacheFlushPage
	pop			{pc}

.ltorg


cop0_tlbr:
	ldr			r0, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	lsls		r0, r0, #32 - CP0_INDEX_SHIFT - ORDER_NUM_TLB_ENTRIES
	lsrs		r0, r0, #32 - ORDER_NUM_TLB_ENTRIES
	movs		r1, #0 + SIZEOF_TLB_ENTRY
	muls		r0, r1
	adds		r0, r0, #0 + OFST_TLB
	add			r0, REG_CPU			//points to our entry
	ldr			r1, [r0, #0 + OFST_TLB_VA]
	ldr			r2, [r0, #0 + OFST_TLB_PA]
	ldrb		r3, [r0, #0 + OFST_TLB_ASID]
	ldrb		r0, [r0, #0 + OFST_TLB_BITS]
	lsls		r3, #6	//shift asid into shape
	orrs		r1, r3	//hi
	lsrs		r0, #1	//remove "enabled "bit
	lsls		r0, r0, #1 + TLB_ENTRYLO_FLAGS_SHIFT
	orrs		r2, r0	//lo
	str			r2, [REG_CPU_P2, #0 + OFST_CP0_ENTRYLO]
	ldr			r2, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	str			r1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	mebeAsidChg	r1, r2
	endCyNoBra	r0

cop0_tlbwi:
	ldr			r0, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	lsls		r0, r0, #32 - CP0_INDEX_SHIFT - ORDER_NUM_TLB_ENTRIES
	lsrs		r0, r0, #32 - ORDER_NUM_TLB_ENTRIES
	bl			tlbWrite
	endCyNoBra	r0

cop0_branches:
	ldr			t1, =0x4100FFFF	//bc0f (used for wb flush)
	cmp			REG_INSTR, t1
	bne			instr_invalid
	endCyNoBra	REG_INSTR

cop0_tlbwr:
	bl			cpuPrvRefreshRandom	
	bl			tlbWrite
	endCyNoBra	r0

cop0_rfe:
	movs		r1, #0 | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE
	movs		r2, #0 | CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP
	cpsid		i	//do not operate on IRQSTATE with irqs on
	ldr			r0, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	ands		r2, r0
	bics		r0, r1
	lsrs		r2, r2, #2
	orrs		r2, r0
	str			r2, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	calcIrqWSta	REG_CPU_P2, r1, r0, r2
	strb		r0, [REG_CPU_P2, #0 + OFST_HAVE_IRQ]
	setNextCyJump	r0, r1, jump_adrs_rfe
	cpsie		i
	
	//clear LLbit
	movs		r0, #0
	strb		r0, [REG_CPU_P2, #0 + OFST_LLBIT]
	
	endCyNoBra	r0

.ltorg
nextCyJumpTbl jump_adrs_rfe



cop0_tlbp:		//xxx: should use hash...
	loadImm		t0, SIZEOF_TLB_ENTRY * NUM_TLB_ENTRIES + OFST_TLB
	add			t0, REG_CPU
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	movs		t2, #0 + NUM_TLB_ENTRIES

1:
	subs		t0, #0 + SIZEOF_TLB_ENTRY
	
	//verify VA match
	ldr			t3, [t0, #0 + OFST_TLB_VA]
	eors		t3, t1
	lsrs		t3, #0 + PAGE_ORDER
	bne			2f
	
	//verify ASID match or global bit
	ldrb		t3, [t0, #0 + OFST_TLB_BITS]
	lsrs		t3, #1
	bcc			2f

	//match
	subs		t2, #1
	lsls		t2, t2, #CP0_INDEX_SHIFT
	str			t2, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	endCyNoBra r0

2:
	subs		t2, #1
	bne			1b

//not found
	loadImm		t0, 0x80000000
	str			t0, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	endCyNoBra	REG_INSTR


cop0_ops:
	brOnBitsS	t1, t0, REG_INSTR, 0, 5
	brCaseS		instr_invalid
	brCaseS		cop0_tlbr
	brCaseS		cop0_tlbwi
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		cop0_tlbwr
	brCaseS		instr_invalid
	brCaseS		cop0_tlbp
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		cop0_rfe
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseS		instr_invalid
	brCaseSdone

cop0_usr_access:
	copUseExc	t2, t1, #0


instr_010000_cop0:
	isUsrMode	t1
	bcs			cop0_usr_access
	
	brOnBitsS	t1, t0, REG_INSTR, 21, 5
	brCaseS		cop0_mfc
	brCaseS		j_instr_invalid_0
	brCaseS		cop0_cfc		//CFC0. as seen in untrix, seems to do the same thing as mfc, do not ask.
	brCaseS		j_instr_invalid_0
	brCaseS		cop0_mtc
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0		//CTC0
	brCaseS		j_instr_invalid_0
	brCaseS		cop0_branches
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		cop0_ops
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseSdone

cop0_cfc:
cop0_mfc:
	
	brOnBitsS	t1, t0, REG_INSTR, 11, 5
	brCaseS		mfc0_index
	brCaseS		mfc0_random
	brCaseS		mfc0_entryLo
	brCaseS		j_instr_invalid_0
	brCaseS		mfc0_context
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		mfc0_badVa
	brCaseS		j_instr_invalid_0
	brCaseS		mfc0_entryHi
	brCaseS		j_instr_invalid_0
	brCaseS		mfc0_status
	brCaseS		mfc0_cause
	brCaseS		mfc0_epc
	brCaseS		mfc0_cpuid
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseSdone

.ltorg

cop0_mtc:
	
	brOnBitsS	t1, t0, REG_INSTR, 11, 5
	brCaseS		mtc0_index
	brCaseS		j_instr_invalid_0
	brCaseS		mtc0_entryLo
	brCaseS		j_instr_invalid_0
	brCaseS		j_mtc0_context
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		mtc0_badVa
	brCaseS		j_instr_invalid_0
	brCaseS		mtc0_entryHi
	brCaseS		j_instr_invalid_0
	brCaseS		mtc0_status
	brCaseS		mtc0_cause
	brCaseS		mtc0_epc
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseS		j_instr_invalid_0
	brCaseSdone

j_instr_invalid_0:	
	b			instr_invalid

j_mtc0_context:					//c-m3 range
	b			mtc0_context

mfc0_index:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_random:
	bl			cpuPrvRefreshRandom
	lsls		t1, t0, #8
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_entryLo:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYLO]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR
	
mfc0_context:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_CONTEXT]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_badVa:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_BADVA]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_entryHi:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_status:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR
	
mfc0_cause:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_epc:
	ldr			t1, [REG_CPU_P2, #0 + OFST_CP0_EPC]
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mfc0_cpuid:
	ldr			t1, =PRID_VALUE
	setRegT		t2, REG_INSTR, t1
	endCyNoBra	REG_INSTR

mtc0_index:
	getRegT		t1, REG_INSTR
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_INDEX]
	endCyNoBra	REG_INSTR

mtc0_entryLo:
	getRegT		t1, REG_INSTR
	lsrs		t1, #8			//low bits must be clear
	lsls		t1, #8
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYLO]
	endCyNoBra	REG_INSTR

mtc0_badVa:
	getRegT		t1, REG_INSTR
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_BADVA]
	endCyNoBra	REG_INSTR
	
mtc0_entryHi:
	getRegT		t1, REG_INSTR
	lsrs		t1, #6			//zero bits
	lsls		t1, #6
	ldr			t2, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	mebeAsidChg	t1, t2
	endCyNoBra	REG_INSTR

mtc0_epc:
	getRegT		t1, REG_INSTR
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_EPC]
	endCyNoBra	REG_INSTR

mtc0_cause:
	getRegT		t1, REG_INSTR
	ldr			t3, =CP0_CAUSE_IV | CP0_CAUSE_WP | (3 << CP0_CAUSE_IP_SHIFT)
	cpsid		i	//do not operate on IRQSTATE with irqs on
	ldr			t2, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	bics		t2, t3
	ands		t1, t3
	orrs		t1, t2
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_CAUSE]
	recalcIrqs	REG_CPU_P2, r1, r2, r0
	strb		r0, [REG_CPU_P2, #0 + OFST_HAVE_IRQ]
	setNextCyJump	r0, r1, jump_adrs_mtc
	cpsie		i
	endCyNoBra	REG_INSTR

mtc0_status:
	getRegT		t1, REG_INSTR
	ldr			t3, =CP0_STATUS_ISC | CP0_STATUS_SWC | CP0_STATUS_CU_MASK | CP0_STATUS_IM_MASK | CP0_STATUS_KUO | CP0_STATUS_IEO | CP0_STATUS_KUP | CP0_STATUS_IEP | CP0_STATUS_KUC | CP0_STATUS_IE
	
	cpsid		i	//do not operate on IRQSTATE with irqs on
	ldr			t2, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	bics		t2, t3
	ands		t1, t3
	orrs		t2, t1
	str			t2, [REG_CPU_P2, #0 + OFST_CP0_STATUS]
	calcIrqWSta	REG_CPU_P2, t3, t1, t2
	strb		t1, [REG_CPU_P2, #0 + OFST_HAVE_IRQ]
	setNextCyJump	t1, t0, jump_adrs_mtc
	cpsie		i	
	endCyNoBra	REG_INSTR

nextCyJumpTbl jump_adrs_mtc

mtc0_context:
	getRegT		t1, REG_INSTR
	ldr			t2, [REG_CPU_P2, #0 + OFST_CP0_CONTEXT]
	lsls		t2, t2, #32 - CP0_CTX_PTEBASE_SHIFT
	lsrs		t2, t2, #32 - CP0_CTX_PTEBASE_SHIFT
	lsrs		t1, t1, #0 + CP0_CTX_PTEBASE_SHIFT
	lsls		t1, t1, #0 + CP0_CTX_PTEBASE_SHIFT
	orrs		t1, t2
	str			t1, [REG_CPU_P2, #0 + OFST_CP0_CONTEXT]
	endCyNoBra	REG_INSTR

cpuPrvRefreshRandom:
	refreshRng	t0, t1		//r0 is output
	
	//this method of calculating modulus is good for inputs up to 2048 assuming base is 48
	ldr			t1, =(65536 + (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES) - 1) / (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES)
	muls		t1, t0
	lsrs		t1, t1, #16	//t1 = output / (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES)
	movs		t2, #0 + (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES)
	muls		t1, t2		//t1 = output / (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES) * (NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES)
	subs		t0, t1		//t1 = output %(NUM_TLB_ENTRIES - NUM_WIRED_ENTRIES)
	adds		t0, #0 + NUM_WIRED_ENTRIES
	bx			lr

instr_110000_ll:
	prepare_f	p1
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t2, t1
	memRdW		t3, t0, t1, t2, 4
	setRegT_f	t0, REG_INSTR, t1, p1
	
	//set LLbit
	movs		t0, #1
	strb		t0, [REG_CPU_P2, #0 + OFST_LLBIT]
	
	endCyNoBra	REG_INSTR

memMiscHandlers 4, t2, 0	//for ll
memMiscHandlers 4, t2, 1	//for sc
memIscHandler	4, t2

instr_111000_sc:
	
	prepare_f	p1
	//test and clear LLBIT
	movs		t0, #0
	ldrb		t3, [REG_CPU_P2, #0 + OFST_LLBIT]
	strb		t0, [REG_CPU_P2, #0 + OFST_LLBIT]
	cmp			t3, #0
	beq			sc_will_fail

sc_will_succeed:
	getRegS_f	t1, REG_INSTR, p1
	sxth		t2, REG_INSTR
	adds		t2, t1
	getRegT_f	t1, REG_INSTR, p1
	memWrW		t3, t0, t1, t2, 4
	movs		r3, #1
	setRegT_f	t0, REG_INSTR, t3, p1
	endCyNoBra	REG_INSTR

sc_will_fail:
	movs		r3, #0
	setRegT_f	t0, REG_INSTR, t3, p1
	endCyNoBra	REG_INSTR

.ltorg

.globl cpuMemAccessExternal			//(void *buf, uint32_t va, uint_fast8_t sz, bool write, enum CpuMemAccessType type)  --  access type ignored, always "as current"
cpuMemAccessExternal:
	push		{REG_CPU, REG_CPU_P2, REG_INSTR, p1, lr}

	ldr			REG_CPU, =mCpu
	movs		REG_CPU_P2, #0 + OFST_PART2
	add			REG_CPU_P2, REG_CPU

	//REG_INSTR is tmp, p1 is out pa
	cmp			r3, #0
	beq			is_read

is_write:
	memXlateEx	REG_INSTR, r1, r1, p1, 1, xxx, 1
	b			access_typed
	
is_read:
	memXlateEx	REG_INSTR, r1, r1, p1, 0, xxx, 1

access_typed:
	cmp			REG_INSTR, #0
	beq			xlate_fail
	
	mov			r1, r2
	mov			r2, r3
	mov			r3, r0
	mov			r0, p1
	bl			memAccess	//(uint32_t addr, uint_fast8_t size, bool write, void* buf);
	//return val in r0 already
	
	pop			{REG_CPU, REG_CPU_P2, REG_INSTR, p1, pc}

xlate_fail:
	bl printRegions
	movs		r0, #0
	pop			{REG_CPU, REG_CPU_P2, REG_INSTR, p1, pc}
.ltorg
	

#if defined(FPU_SUPPORT_FULL) || defined(FPU_SUPPORT_MINIMAL)

	//there once was a boy named Dmitry
	//who thought that assembly was fun
	//a MIPS FPU was a challenge enough
	//so he wrote it in C and said "done"
	
	instr_010001_cop1:
		copCheck	t1, t2, #1
		mov			t0, REG_INSTR
		mov			t1, REG_CPU
		getFpuPtr	t2
		movs		t3, #1
		strb		t3, [REG_CPU_P2, #0 + OFST_IN_FPU_OP]
		bl			fpuOp
		movs		t3, #0
		cpsid		i
		strb		t3, [REG_CPU_P2, #0 + OFST_IN_FPU_OP]
		ldrb		t3, [REG_CPU_P2, #0 + OFST_HAVE_IRQ]
		setNextCyJump	t3, t2, jump_adrs_fpu
		cpsie		i
		
		brOnBitsS	t1, t2, t0, 0, 31
		brCaseS		fpu_inval			//0 = inval opcode
		brCaseS		fpu_exc				//1 = fpu exception
		brCaseS		fpu_branch			//2 = relbra
		brCaseS		fpu_unlikely		//3 = "likely" branch not taken
		brCaseS		fpu_use_exc			//4 = coprocesor 1 usage exception
		brCaseSlastHere					//5 = success
	
	//success
		endCyNoBra	REG_INSTR
	
	fpu_inval:
		bw			instr_invalid
	
	fpu_exc:
		//R4000 uses FPE exception, R3000 used RI
		movs		r0, #(CP0_EXC_COD_RI << CP0_CAUSE_EXC_COD_SHIFT)
		bw			cpuPrvTakeException
	
	fpu_branch:
		relbra		t2, t3, REG_INSTR
		
	fpu_unlikely:
		endCySkpNxt REG_INSTR
	
	fpu_use_exc:
		copUseExc	r0, r1, 1

nextCyJumpTbl	jump_adrs_fpu

.ltorg

	//these NEED to be fast as they are in the context-switch path
	instr_110001_lwc1:
		prepare_f	p1
		copCheck	t1, t2, #1
		getRegS_f	t1, REG_INSTR, p1
		sxth		t2, REG_INSTR
		adds		t3, t1, t2
		memRdW		t1, t0, t2, t3, 10
		
		regNoTM4_f	t1, REG_INSTR, p1
		getFpuPtr	t3
		str			t2, [t3, t1]
		endCyNoBra	REG_INSTR
	
	memMiscHandlers 10, t3, 0	//for lwc & ldc
	
	instr_110101_ldc1:
		prepare_f	p1
		copCheck	t1, t2, #1
		getRegS_f	t1, REG_INSTR, p1
		sxth		t2, REG_INSTR
		adds		t3, t1, t2
		memRdW		t1, t0, p1, t3, 10		//p1 is output
	
		adds		t3, #4
		memRdW		t1, t0, t2, t3, 11		//t2 is output
		
		regNoT		t1, REG_INSTR
		lsls		t1, #2
		getFpuPtr	t3
		adds		t3, t1
		str			p1, [t3, #0]
		str			t2, [t3, #4]
		endCyNoBra	REG_INSTR
	
	memMiscHandlers 11, t3, 0	//for ldc
.ltorg
	memMiscHandlers 10, t2, 1	//for sdc
	memIscHandler	10, t2
	
	instr_111101_sdc1:
		prepare_f	p1
		copCheck	t1, t2, #1
		getRegS_f	t1, REG_INSTR, p1
		sxth		t2, REG_INSTR
		adds		t2, t1
		regNoTM4_f	t1, REG_INSTR, p1
		getFpuPtr	t3
		adds		p1, t1, t3
		ldmia		p1, {t1, p1}
		movs		REG_INSTR, t2
		memWrW		t3, t0, t1, t2, 10		//XXX maybe these can be rolled together, then again, FPU is slow enough that who cares?
		adds		t2, REG_INSTR, #4
		memWrW		t3, t0, p1, t2, 11
		endCyNoBra	REG_INSTR
	
	memMiscHandlers 11, t2, 1	//for sdc & swc
	memIscHandler	11, t2
	
	instr_111001_swc1:
		prepare_f	p1
		copCheck	t1, t2, #1
		getRegS_f	t1, REG_INSTR, p1
		sxth		t2, REG_INSTR
		adds		t2, t1
		regNoTM4_f	t1, REG_INSTR, p1
		getFpuPtr	t3
		ldr			t1, [t1, t3]
		memWrW		t3, t0, t1, t2, 11
		endCyNoBra	REG_INSTR

	
#else
	
	instr_010001_cop1:
		copCheck	t1, t2, #1
		ldr			t0, =0x222
		lsrs		t1, REG_INSTR, #21
		subs		t0, t1
		bne			not_read_fir
	
	is_read_fir:
		setRegT		t1, REG_INSTR, t0
		endCyNoBra	REG_INSTR
	
	not_read_fir:
		b			fpinstr_invalid
	
	instr_110001_lwc1:
		copCheck	t1, t2, #1
		b			fpinstr_invalid
	
	instr_110101_ldc1:
		copCheck	t1, t2, #1
		b			fpinstr_invalid
	
	instr_111101_sdc1:
		copCheck	t1, t2, #1
		b			fpinstr_invalid
	
	instr_111001_swc1:
		copCheck	t1, t2, #1
		b			fpinstr_invalid

	
	fpinstr_invalid:
/*
///aaa
	mov			r0, REG_PC
	mov			r1, REG_INSTR
	bl			reportInvalid
///aaa
*/
	movs		r0, #(CP0_EXC_COD_RI << CP0_CAUSE_EXC_COD_SHIFT)
	bw			cpuPrvTakeException	

#endif
