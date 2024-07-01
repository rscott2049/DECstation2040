/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include  <stdint.h>
#define VEC_(nm, pfx)	void nm##pfx(void) __attribute__ ((weak, alias ("IntDefaultHandler"))) 
#define VEC(nm)		VEC_(nm, _Handler)
#define VECI(nm)	VEC_(nm, _IRQHandler)


void __attribute__ ((weak)) IntDefaultHandler(void);
VEC(NMI);
VEC(HardFault);
VEC(SVC);
VEC(PendSV);
VEC(SysTick);


VECI(INPUT);
VECI(RTC_HZ);
VECI(RTC_ALARM);
VECI(OS_TIMER);
VECI(SOUND_OUT);
VECI(SOUND_IN);
VECI(ETHERNET);
VECI(JOYSTICK);



//micromain must exist
extern void micromain(void);

//stack top (provided by linker)
extern void __stack_top();
extern uint32_t __data_data[];
extern uint32_t __data_start[];
extern uint32_t __data_end[];
extern uint32_t __bss_start[];
extern uint32_t __bss_end[];




void __attribute__((noreturn)) IntDefaultHandler(void)
{
	while (1) {		
		asm("wfi":::"memory");
	}
}

void __attribute__((noreturn)) ResetISR(void)
{
	uint32_t *dst, *src, *end;

	//copy data
	dst = __data_start;
	src = __data_data;
	end = __data_end;
	while(dst != end)
		*dst++ = *src++;

	//init bss
	dst = __bss_start;
	end = __bss_end;
	while(dst != end)
		*dst++ = 0;

	micromain();

//if main returns => bad
	while(1);
}


__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,
	ResetISR,
	NMI_Handler,
	HardFault_Handler,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	SVC_Handler,		// SVCall handler
	0,					// Reserved
	0,					// Reserved
	PendSV_Handler,		// The PendSV handler
	SysTick_Handler,	// The SysTick handler
	
	// Chip Level
	INPUT_IRQHandler,
	RTC_HZ_IRQHandler,
	RTC_ALARM_IRQHandler,
	OS_TIMER_IRQHandler,
	SOUND_OUT_IRQHandler,
	SOUND_IN_IRQHandler,
	ETHERNET_IRQHandler,
	JOYSTICK_IRQHandler,
};






