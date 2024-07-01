#
#	(c) 2021 Dmitry Grinberg   https://dmitry.gr
#	Non-commercial use only OR licensing@dmitry.gr
#

SOURCES		= mem.c decBus.c dz11.c lance.c esar.c sii.c scsiDevice.c scsiDisk.c scsiNothing.c
LDFLAGS		= -lm -g
CCFLAGS		= -fno-math-errno -flto		#LTO does make things smaller
CPU			?= atsamd21
FPU			?= full

ifeq ($(CPU),stm32g0)
	ZWT_ADDR = 0x20001FF8
	CCFLAGS	+= -Ofast -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_STM32G0 -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=105000000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=5 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	CCFLAGS	+= -DSUPPORT_MULTIBLOCK_ACCESSES_TO_SD
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_stm32g0.c printf.c sd.c ucHwStm32G0.c timebase.c main_uc.c spiRamStm32G0.c usartStm.c ds1287stm32.c cpuAsm.S
#	SOURCES += sdHwStm32G0spi.c
	SOURCES += sdHwStm32G0sdio.c
	LKR		= linker_stm32g0.lkr
else ifeq ($(CPU),CortexEmu)
	ZWT_ADDR = 0x4fffffff
	CCFLAGS	+= -Ofast -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft -mno-unaligned-access
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable  -fno-jump-tables -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_CORTEXEMU -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=100000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=5 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DMICRO_LANCE -DCOLOR_FRAMEBUFFER
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_CortexEmu.c printf.c sd.c ucHwCortexEmu.c timebase.c main_uc.c spiRamCortexEmu.c usartCortexEmu.c ds1287CortexEmu.c sdHwCortexEmu.c
	#SOURCES += cpuJit.c
	SOURCES += cpuAsm.S
	LKR		= linker_CortexEmu.lkr
else ifeq ($(CPU),atsamda1e16)
	ZWT_ADDR = 0x41002010
	CCFLAGS	+= -Os -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft -mno-unaligned-access
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter -fstack-reuse=all
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -fno-jump-tables -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_SAMD -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=90000000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=6 -DL2CACHE_NUM_SETS=32 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DMULTICHANNEL_UART
	CCFLAGS	+= -DMICRO_LANCE -DNO_FRAMEBUFFER
#	CCFLAGS	+= -DRAM_FUNCS_IN_RAM
#	CCFLAGS	+= -DSTACKGUARD
#	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_atsamd21.c printf.c main_uc.c spiRamAtsamd21.c timebase.c ucHwAtsamd21.c sd.c usartAtsamd21.c ds1287atsamd21.c usbDev.c
	#SOURCES += cpuJit.c
	SOURCES += cpuAsm.S
	SOURCES += sdHwAtsamd21spi.c
	LKR		= linker_atsamda1e16.lkr
else ifeq ($(CPU),atsamd21e17)
	ZWT_ADDR = 0x41002010
	CCFLAGS	+= -Os -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft -mno-unaligned-access
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter -fstack-reuse=all
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -fno-jump-tables -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_SAMD -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=72000000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=7 -DL2CACHE_NUM_SETS=96 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DMULTICHANNEL_UART
	CCFLAGS	+= -DMICRO_LANCE -DNO_FRAMEBUFFER
	CCFLAGS	+= -DRAM_FUNCS_IN_RAM
#	CCFLAGS	+= -DSTACKGUARD
#	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_atsamd21.c printf.c main_uc.c spiRamAtsamd21.c timebase.c ucHwAtsamd21.c sd.c usartAtsamd21.c ds1287atsamd21.c usbDev.c
	#SOURCES += cpuJit.c
	SOURCES += cpuAsm.S
	SOURCES += sdHwAtsamd21spi.c
	LKR		= linker_atsamd21e17.lkr
else ifeq ($(CPU),stm32h7)
	ZWT_ADDR = 0x2001BFFC
	CCFLAGS	+= -Ofast -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -mcpu=cortex-m7 -I. -mfpu=fpv5-d16 -mfloat-abi=hard
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -Wno-unused-but-set-variable -Wno-unused-variable -ffixed-r11
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_CM7 -DCPU_TYPE_CM7 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=300000000U -DEXTRARAM
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=7 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DMICRO_LANCE
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	CCFLAGS	+= -DSUPPORT_MULTIBLOCK_ACCESSES_TO_SD
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_stm32h7.c printf.c sd.c sdHwStm32H7.c ucHwStm32H7.c timebase.c main_uc.c spiRamStm32H7.c usartStm.c cpuAsm.S ds1287stm32.c
	LKR		= linker_stm32h7.lkr
else
	CCFLAGS	+= -Og -g -ggdb3
#	CCFLAGS	+= -O3
	CCFLAGS	+= -Wall -Wextra -Werror -D"err_str(...)=fprintf(stderr, __VA_ARGS__)" -DGDB_SUPPORT
	CCFLAGS	+= -D_FILE_OFFSET_BITS=64 -D__USE_LARGEFILE64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
#	CCFLAGS	+= -DCDROM_SUPORTED=1
	CC		= gcc
	SOURCES	+= cpu.c soc_pc.c main.c ds1287.c lk401.c inputSDL.c
	
	#pointing device (only one may be chosen), for 
#	SOURCES += decMouse.c
	SOURCES += decTablet.c
	
	#graphics
	CCFLAGS += -DCOLOR_FRAMEBUFFER -DMOUSE_AND_KBD
	SOURCES += graphics.c
	LDFLAGS	+= -lSDL2
endif

ifeq ($(FPU),full)
	SOURCES += fpu.c
	CCFLAGS += -DFPU_SUPPORT_FULL
else ifeq ($(FPU),minimal)
	SOURCES += fpu.c
	CCFLAGS += -DFPU_SUPPORT_MINIMAL
else
	CCFLAGS += -DFPU_SUPPORT_NONE
endif

LDFLAGS += $(CCFLAGS)

APP		= uMIPS

#no changes below please

OBJS	= $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SOURCES)))

$(APP): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

loader.inc: ../romboot/loader.bin Makefile
	xxd -i $< | grep 0x > $@

soc_uc.o: loader.inc

%.o : %.c Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

%.o : %.S Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(APP) $(OBJS)

%.bin: %
	arm-none-eabi-objcopy -O binary $< $@ -j.text -j.rodata -j.data -j.vectors

test: uMIPS.bin
	sudo CortexProg power on write $< 4096 trace $(ZWT_ADDR) info

trace:
	sudo CortexProg power on trace $(ZWT_ADDR) info

testM7: uMIPS.bin
	sudo CortexProg power 2900 write $< trace $(ZWT_ADDR) info

