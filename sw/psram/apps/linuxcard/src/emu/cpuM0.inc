/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#define ARMv6

.macro cbnz reg, dst
	cmp			\reg, #0
	bne			\dst
.endm

.macro cbz reg, dst
	cmp			\reg, #0
	beq			\dst
.endm

.macro bw dst, cc
	bl\cc		\dst
.endm

.macro scc	dst, lhs, rhs, cond, notCond
	movs		\dst, #0
	cmp			\lhs, \rhs
	b\notCond	1f
	movs		\dst, #1
1:
.endm

.macro bwcc dst, cond, notCond
	b\notCond	1f
	bw			\dst
1:
.endm

.macro addl	rD, rS, amt		//for when amt is < 8 bits but more than 3
	movs		\rD, \rS
	adds		\rD, \amt
.endm

.macro sfOnBf tmp, reg, lsb, nBits		//set flag on a bitfield in a reg
	lsrs		\tmp, \reg, #0 + \lsb
	lsls		\tmp, \tmp, #32 - \nBits
.endm

.macro getM1 reg
	movs		\reg, #1
	mvns		\reg, \reg
.endm

.macro brOnBitsL tmp, tmp2, reg, lsb, nbits	//branch to far
	.if \lsb + \nbits != 32
		lsls	\tmp, \reg, #32 - (\lsb + \nbits)
		lsrs	\tmp, \tmp, #32 - \nbits
		lsls	\tmp, \tmp, #2
	.elseif \lsb == 0
		bfx		\tmp, \reg, (-2), (\nbits + 2)
	.else
		lsrs	\tmp, \reg, #32 - \nbits
		lsls	\tmp, \tmp, #2
	.endif
	add			pc, \tmp
	nop
.endm

.macro brCaseLnear dst					//a "near" case for brOnBitsL. short branch may be faster
	b.n			\dst
	nop
.endm

.macro brCaseLfar dst					//a "far" case for brOnBitsL, destroys LR
	bl			\dst
.endm

.macro brOnBitsS tmp, tmp2, reg, lsb, nbits
	bfx			\tmp, \reg, (\lsb - 1), (\nbits + 1)
	add			pc, t1
	nop
.endm

.macro brCaseS dst
	b.n			\dst
.endm

.macro brCaseSlastHere			//ends a list of short switch. IS last item
	//nothing
.endm

.macro brCaseSdone			//ends a list of short switch, if no "brCaseSlastHere" here
	//nothing
.endm


.macro	umul32x32_to_64	src1, src2, dstHi, dstLo, tmp1, tmp2	//destroys sources, no regs may be the same
	uxth		\tmp1, \src1
	uxth		\tmp2, \src2
	lsrs		\src1, \src1, #16
	lsrs		\src2, \src2, #16
	
	mov			\dstLo, \tmp1
	muls		\dstLo, \tmp2			//dstLo = src1.lo * src2.lo
	mov			\dstHi, \src1
	muls		\dstHi, \src2			//dstHi = src1.hi * src2.hi
	
	muls		\tmp1, \src2			//tmp1 = mid1
	muls		\tmp2, \src1			//tmp2 = mid2
	
	lsrs		\src1, \tmp1, #16		//src1 = mid1.hi
	lsrs		\src2, \tmp2, #16		//src2 = mid2.hi
	
	lsls		\tmp1, \tmp1, #16		//tmp1 = mid1.lo << 16
	lsls		\tmp2, \tmp2, #16		//tmp2 = mid2.lo << 16
	
	adds		\dstLo, \tmp1
	adcs		\dstHi, \src1
	
	adds		\dstLo, \tmp2
	adcs		\dstHi, \src2
.endm

.macro	smul32x32_to_64	src1, src2, dstHi, dstLo, tmp1, tmp2	//destroys sources, no regs may be the same
	uxth		\tmp1, \src1
	uxth		\tmp2, \src2
	asrs		\src1, \src1, #16
	asrs		\src2, \src2, #16
	
	mov			\dstLo, \tmp1
	muls		\dstLo, \tmp2			//dstLo = src1.lo * src2.lo
	
	mov			\dstHi, \src1
	muls		\dstHi, \src2			//dstHi = src1.hi * src2.hi
	
	muls		\tmp1, \src2			//tmp1 = mid1
	muls		\tmp2, \src1			//tmp2 = mid2
	
	asrs		\src1, \tmp1, #16		//src1 = mid1.hi
	asrs		\src2, \tmp2, #16		//src2 = mid2.hi
	
	lsls		\tmp1, \tmp1, #16		//tmp1 = mid1.lo << 16
	lsls		\tmp2, \tmp2, #16		//tmp2 = mid2.lo << 16
	
	adds		\dstLo, \tmp1
	adcs		\dstHi, \src1
	
	adds		\dstLo, \tmp2
	adcs		\dstHi, \src2
.endm

.macro sdivmod //regs assumed
	
	//__divsi3 is not guaranteed to return remainder in r1 (it does right now for ARMv6M libc and not for ARMv7M) so we must calculate it
	mov			p1, r0
	mov			REG_INSTR, r1
	bl			__divsi3
	muls		REG_INSTR, r0
	subs		r1, p1, REG_INSTR
	
	str			r1, [REG_CPU_P2, #0 + OFST_HI]
	str			r0, [REG_CPU_P2, #0 + OFST_LO]

.endm

.macro udivmod //regs assumed
	
	//__udivsi3 is not guaranteed to return remainder in r1 (it does right now for ARMv6M libc and not for ARMv7M) so we must calculate it
	mov			p1, r0
	mov			REG_INSTR, r1
	bl			__udivsi3
	muls		REG_INSTR, r0
	subs		r1, p1, REG_INSTR
	
	str			r1, [REG_CPU_P2, #0 + OFST_HI]
	str			r0, [REG_CPU_P2, #0 + OFST_LO]

.endm

//XXX: when replacing these macros keep in mind sometimes negative values are passed in for "firstBit"
.macro bfxAndZ	dstReg, srcReg, firstBit, numBits		//dst and src can be the same, setz Z flag on result
	lsls		\dstReg, \srcReg, #32 - (\firstBit) - (\numBits)
	.if \numBits != 32
		lsrs		\dstReg, \dstReg, #32 - (\numBits)
	.endif
.endm

.macro bfx		dstReg, srcReg, firstBit, numBits		//dst and src can be the same
	bfxAndZ		\dstReg, \srcReg, \firstBit, \numBits
.endm

.macro getPC_p8 dst	//pc plus 8
	mov			\dst, REG_PC
	adds		\dst, #8
.endm


//fast reg access
.macro prepare_f fastreg
	movs		\fastreg, #0x7c
.endm

.macro getRegS_f dst, instr, fastreg
	lsrs		\dst, \instr, #19
	ands		\dst, \fastreg
	ldr			\dst, [REG_CPU, \dst]
.endm

.macro getRegT_f dst, instr, fastreg
	lsrs		\dst, \instr, #14
	ands		\dst, \fastreg
	ldr			\dst, [REG_CPU, \dst]
.endm

.macro setRegT_f tmp, instr, valReg, fastreg	//tmp can be same as instr
	lsrs		\tmp, \instr, #14
	ands		\tmp, \fastreg
	beq			69f
	str			\valReg, [REG_CPU, \tmp]
69:
.endm

.macro setRegD_f tmp, instr, valReg, fastreg	//tmp can be same as instr
	lsrs		\tmp, \instr, #9
	ands		\tmp, \fastreg
	beq			69f
	str			\valReg, [REG_CPU, \tmp]
69:
.endm

.macro regNoTM4_f dst, instr, fastreg
	lsrs		\dst, \instr, #14
	ands		\dst, \fastreg
.endm




//normal reg access

.macro getRegS dst, instr
	regNoS		\dst, \instr
	lsls		\dst, \dst, #2
	ldr			\dst, [REG_CPU, \dst]
.endm

.macro getRegT dst, instr
	regNoT		\dst, \instr
	lsls		\dst, \dst, #2
	ldr			\dst, [REG_CPU, \dst]
.endm

.macro setRegT tmp, instr, valReg	//tmp can be same as instr
	regNoTz		\tmp, \instr
	beq			69f
	lsls		\tmp, \tmp, #2
	str			\valReg, [REG_CPU, \tmp]
69:
.endm

.macro setRegD tmp, instr, valReg	//tmp can be same as instr
	regNoDz		\tmp, \instr
	beq			69f
	lsls		\tmp, \tmp, #2
	str			\valReg, [REG_CPU, \tmp]
69:
.endm


.macro endCySkpNxt tmp
	movs		\tmp, #4
	add			REG_NPC, \tmp
	mov			REG_PC, REG_NPC
	add			REG_NPC, \tmp
	mov			REG_NOT_DELAY_SLOT, \tmp
	endCy		\tmp, 0
.endm

.macro endCyNoBra tmp			///maybe this pc advance code can be placed before "do_cycle_irqcheck" instead to share a common copy?
	mov			REG_PC, REG_NPC
	movs		\tmp, #4
	add			REG_NPC, \tmp
	mov			REG_NOT_DELAY_SLOT, \tmp
	endCy		\tmp, 0
.endm

.macro endCyBranch tmp, dstReg
	mov			REG_PC, REG_NPC
	mov			REG_NPC, \dstReg
	movs		\tmp, #0
	mov			REG_NOT_DELAY_SLOT, \tmp
	endCy		\tmp, 1
.endm

.macro relbra tmp, tmp2, instr			//tmp2 can be same as instr if it is ok to destroy it
	mov			\tmp, REG_NPC
	lsls		\tmp2, \instr, #16
	asrs		\tmp2, \tmp2, #14
	adds		\tmp2, \tmp
	endCyBranch	\tmp, \tmp2
.endm


.macro setBadVA	tmp, tmp2, va	//tmp2 can be va, if you are ok destroying it
	str			\va, [REG_CPU_P2, #0 + OFST_CP0_BADVA]
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_CONTEXT]
	lsrs		\tmp, \tmp, #0 + CP0_CTX_PTEBASE_SHIFT
	lsls		\tmp, \tmp, #0 + CP0_CTX_PTEBASE_SHIFT
	
	lsls		\tmp2, \va, #1
	lsrs		\tmp2, \tmp2, #1 + PAGE_ORDER
	lsls		\tmp2, \tmp2, #0 + CP0_CTX_BADVPN2_SHIFT
	
	orrs		\tmp, \tmp, \tmp2
	
	str			\tmp, [REG_CPU_P2, #0 + OFST_CP0_CONTEXT]
.endm

.macro setEntryHi tmp, tmp2, va	//tmp2 can be va, if you are ok destroying it
	ldr			\tmp, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
	lsls		\tmp, \tmp, #32 - PAGE_ORDER
	lsrs		\tmp, \tmp, #32 - PAGE_ORDER
	lsrs		\tmp2, \va, #0 + PAGE_ORDER
	lsls		\tmp2, \tmp2, #0 + PAGE_ORDER
	orrs		\tmp, \tmp, \tmp2
	str			\tmp, [REG_CPU_P2, #0 + OFST_CP0_ENTRYHI]
.endm

.macro	mebeAsidChg	val1, val2		//corrupts val1
	
	eors		\val1, \val2
	lsrs		\val1, \val1, #TLB_ENTRYHI_ASID_SHIFT
	lsls		\val1, \val1, #32 - TLB_ENTRYHI_ASID_BITLEN
	beq			99f
	bl			cpuAsidChanged
99:
.endm

.macro loadImm dst, val		//in least cycles, literal pool allowed

	//can a move work?
	.if !((\val) &~ 0xff)
		movs	\dst, (\val)
		
	//can a move and an add work?
	.elseif ((\val) > 0xff) && ((\val) < 0x1fe)
		movs	\dst, #0xff
		adds	\dst, #(\val) - 0xff

	//can sxtb help us?
	.elseif !(((\val) >> 23) - 0x1ffffff)
		movs	\dst, ((\val) >> 0) & 0xff
		sxtb	\dst, \dst
		
	//can revsh help us?
	.elseif !(((\val) >> 15) - 0x1ffff) && !((\val) & 0xff)
		movs	\dst, ((\val) >> 8) & 0xff
		revsh	\dst, \dst

	//can sxth help us?
	.elseif !(((\val) >> 15) - 0x1ffff)
		loadImm	\dst, ((\val) & 0xffff)
		sxth	\dst, \dst

	//try some likely shifts
	.elseif !((\val) &~ 0xff0)
		movs	\dst, (\val) >> 4
		lsls	\dst, \dst, #4
	
	
	//try some likely shifts
	.elseif !((\val) &~ 0x1fe0)
		movs	\dst, (\val) >> 5
		lsls	\dst, \dst, #5
	
	//all
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 16) & 0xff) && (((\val) >> 8) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	//all but one
	.elseif (((\val) >> 16) & 0xff) && (((\val) >> 8) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 8) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 16) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 16) & 0xff) && (((\val) >> 8) & 0xff)
		ldr		\dst, =(\val)
	
	//any two
	.elseif (((\val) >> 8) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 16) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 16) & 0xff) && (((\val) >> 8) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 0) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 8) & 0xff)
		ldr		\dst, =(\val)
	
	.elseif (((\val) >> 24) & 0xff) && (((\val) >> 16) & 0xff)
		ldr		\dst, =(\val)
	
	//any one
	.elseif (((\val) >> 24) & 0xff)
		movs	\dst, #0 + ((\val) >> 24) & 0xff
		lsls	\dst, #24
	
	.elseif (((\val) >> 16) & 0xff)
		movs	\dst, #0 + ((\val) >> 16) & 0xff
		lsls	\dst, #16
	
	.elseif (((\val) >> 8) & 0xff)
		movs	\dst, #0 + ((\val) >> 8) & 0xff
		lsls	\dst, #8
	
	.else
		movs	\dst, #(\val)

	.endif

.endm

//adjust cache reg pointer to point to the proper set given an address
.macro cachePickSt	cacheAddrReg /*in/out */, addr, tmp1, tmp2, lineSzOrder, lineStoreSz, numSetsOrder, numWays
	.if \numSetsOrder
		bfx		\tmp1, \addr, \lineSzOrder, \numSetsOrder
		loadImm	\tmp2, \lineStoreSz * \numWays
		muls	\tmp2, \tmp1
		adds	\cacheAddrReg, \tmp2
	.endif
.endm

//pick a victim from a set to replace. assumes pointer points just past the last way of the set
.macro cachePickVc	cacheAddrReg /*in/out */, tmp0, tmp1, numWaysOrder, lineStoreSz
	.if \numWaysOrder
		//pick a victim line
		
		//cacheAddrReg currently points just PAST the last icache line, so to
		// pick a random way, we need to subtract (1...numLines) * lineStoreSz from it
		
		refreshRng	\tmp0, \tmp1		//output in \tmp0
		lsls		\tmp0, #32 - \numWaysOrder
		lsrs		\tmp0, #32 - \numWaysOrder
		adds		\tmp0, #1
		movs		\tmp1, #\lineStoreSz
		muls		\tmp0, \tmp1
		subs		\cacheAddrReg, \tmp0	//now points to the victim line
	.else
		subs		\cacheAddrReg, #\lineStoreSz
	.endif
.endm

.macro tlbDoHash dst, src											//dst & src must not be same
	lsrs		\dst, \src, #12
	eors		\dst, \src
	lsls		\dst, #20 - ORDER_NUM_TLB_BUCKETS
	lsrs		\dst, #32 - ORDER_NUM_TLB_BUCKETS
.endm

//va is not corrupted, no params may be same, will not return in fail case
//if returnErr is true, tmp2 will have "bool success" on output
.macro memXlateEx tmp2, vaUse, vaReport, pa, isWrite, miscHandlerLbl, returnErr



	//quickly discriminate address type by shifting left by one:
	// CPSR.C gets bit 31, which is clear for kuseg and set for all others
	// CPSR.N is set for ksseg and kseg3 which translate in kernel mode and crash in user mode
	// CPSR.N is clear for kseg0/kseg1 which access physmem in kernel mode and crash in user mode 
	lsls		\pa, \vaUse, #1
	bcc			80f					//kuseg - needs translation
	bmi			81f					//ksseg/kseg3 - translate if not in user mode

//kseg0 and kseg1 - direct map to PA in kernel mode, fault in user mode
	isUsrMode	\pa
	bcs			88f					//fault
	lsls		\pa, \vaUse, #3
	lsrs		\pa, \pa, #3
	.if \returnErr
		movs	\tmp2, #1
	.endif
	b			89f

88:						//address error
	.if \returnErr
		b		88f
	.else
		addrErr \vaReport, \isWrite
	.endif

81:		//ksseg/kseg3 - translate if not in user mode
	isUsrMode	\pa
	bcs			88b

80:						//xlation needed
	getTlbHptr	\pa
	tlbDoHash	\tmp2, \vaUse
	ldrb		\tmp2, [\pa, \tmp2]	//get pointer to hash chain
84:
	cmp			\tmp2, #0xff
	beq			88f					//refill needed
	
	movs		\pa, #0 + SIZEOF_TLB_ENTRY
	muls		\tmp2, \pa
	adds		\tmp2, #0 + OFST_TLB
	adds		\tmp2, REG_CPU
	
	//verify VA match
	ldr			\pa, [\tmp2, #0 + OFST_TLB_VA]
	eors		\pa, \vaUse
	lsrs		\pa, #0 + PAGE_ORDER
	bne			87f
	
	//load "flags" and "enabled"
	ldrb		\pa, [\tmp2, #0 + OFST_TLB_BITS]
	
	//verify "enabled" (aka: ASID match or global bit)
	lsrs		\pa, \pa, #1
	bcc			87f
	
	//verify "valid" bit. shift D bit into "C" flag, valid bit into "N" flag, remember the above shift has been done)
	lsls		\pa, \pa, #33 - TLB_FLAGS_BIT_D		//now "D" flag is in carry bit, "V" flag is in top bit of pa and in "N" flag
	
	.if \returnErr
		
		bpl		88f
		.if \isWrite
			bcc	88f
		.endif
		
	.else
	
		.if \isWrite
			bpl	tlbInvalExcW\miscHandlerLbl\()_\vaReport
		.else
			bpl	tlbInvalExcR\miscHandlerLbl\()_\vaReport
		.endif
		
		.if \isWrite
			bcc tlbModifiedExc\miscHandlerLbl\()_\vaReport
		.endif
	.endif
	
	//we have a match!
	ldr			\pa, [\tmp2, #0 + OFST_TLB_PA]
	lsls		\tmp2, \vaUse, #32 - PAGE_ORDER
	lsrs		\tmp2, \tmp2, #32 - PAGE_ORDER
	adds		\pa, \tmp2
	.if \returnErr
		movs	\tmp2, #1
	.endif
	b			89f

87:
	ldrb		\tmp2, [\tmp2, #0 + OFST_TLB_HASH_NEXT_IDX]
	b			84b
	
88:
	
//TLB refill needed
	.if \returnErr
		movs	\tmp2, #0
	.else
	
		mov		r0, \vaReport
		.if	\isWrite
			bl	cpuPrvTakeTlbRefillExcW
		.else
			bl	cpuPrvTakeTlbRefillExcR
		.endif
	.endif
	

89:						//xlation completed successfully

.endm

.macro algnChk	tmp, addrReg, sizeOrder, isWrite
	.if \sizeOrder
		lsls	\tmp, \addrReg, #32 - \sizeOrder
		beq		98f
		
		addrErr \addrReg, \isWrite
	98:
	.endif
.endm

.macro clrTopBit reg
	lsls		\reg, #1
	lsrs		\reg, #1
.endm

.macro ramRead	ofst, sizeOrder, branchLoadFromSpace, branchLoadComplete, loadOp, dstReg
	push		{r0-r3}
	mov			r0, \ofst
	adds		r1, REG_CPU_P2, #0 + OFST_SPACE
	movs		r2, #1 << \sizeOrder
	bl			spiRamRead
	pop			{r0-r3}
	b			\branchLoadFromSpace
.endm

.macro ramReadToP2	ramOfst, p2Ofst
	.if			\ramOfst != r0
		movs		t0, \ramOfst
	.endif
	.if			\p2Ofst
		movs		t1, #0 + \p2Ofst
		adds		t1, REG_CPU_P2
	.else
		movs		t1, REG_CPU_P2
	.endif
	movs		t2, #4
	bl			spiRamRead			//(uint32_t addr, void *dataP, uint_fast16_t sz)
.endm

.macro ramReadWord	ofst, dstReg		//assumes ABI regs are ok to corrupt
	ramReadToP2	\ofst, OFST_SPACE
	ldr			\dstReg, [REG_CPU_P2, #0 + OFST_SPACE]
.endm

.macro ramReadHalf	ofst, dstOfst		//assumes ABI regs are ok to corrupt
	movs		t0, \ofst
	adds		t1, REG_CPU_P2, #0 + OFST_SPACE + \dstOfst
	movs		t2, #2
	bl			spiRamRead			//(uint32_t addr, void *dataP, uint_fast16_t sz)
.endm

.macro ramReadSplitWord4	dst
	ramReadHalf	#0x04, 2	//high 2 bytes of LE32 word
	ramReadHalf	#0x08, 0	//low 2 bytes of LE32 word
	ldr			\dst, [REG_CPU_P2, #0 + OFST_SPACE]
.endm

.macro ramRead_ic	ramOfst, dstAdr		//assumes ABI regs are ok to corrupt
	mov			r0, \ramOfst
	mov			r1, \dstAdr
	movs		r2, #1 << ICACHE_LINE_SZ_ORDER
#ifdef RAM_FUNCS_IN_RAM
	ldr			r3, =spiRamRead
	blx			r3
#else
	bl			spiRamRead
#endif
.endm

.macro ramWrite ramOfst, srcReg, strOp, sizeOrder		//ramOfst ok to corrupt
	\strOp		\srcReg, [REG_CPU_P2, #0 + OFST_SPACE]
	push		{r0-r3}
	mov			r0, \ramOfst
	adds		r1, REG_CPU_P2, #0 + OFST_SPACE
	movs		r2, #1 << \sizeOrder
#ifdef RAM_FUNCS_IN_RAM
	ldr			r3, =spiRamWrite
	blx			r3
#else
	bl			spiRamWrite
#endif
	pop			{r0-r3}
.endm
