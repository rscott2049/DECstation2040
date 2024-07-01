/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _CORTEX_EMU_CPU_H_
#define _CORTEX_EMU_CPU_H_

#include <stdbool.h>
#include <stdint.h>


typedef enum IRQn
{
/******  Cortex-M3 Processor Exceptions Numbers ***************************************************/
  NonMaskableInt_IRQn         = -14,    /*!< 2 Non Maskable Interrupt                             */
  MemoryManagement_IRQn       = -12,    /*!< 4 Cortex-M3 Memory Management Interrupt              */
  BusFault_IRQn               = -11,    /*!< 5 Cortex-M3 Bus Fault Interrupt                      */
  UsageFault_IRQn             = -10,    /*!< 6 Cortex-M3 Usage Fault Interrupt                    */
  SVCall_IRQn                 = -5,     /*!< 11 Cortex-M3 SV Call Interrupt                       */
  DebugMonitor_IRQn           = -4,     /*!< 12 Cortex-M3 Debug Monitor Interrupt                 */
  PendSV_IRQn                 = -2,     /*!< 14 Cortex-M3 Pend SV Interrupt                       */
  SysTick_IRQn                = -1,     /*!< 15 Cortex-M3 System Tick Interrupt                   */

/******  cpu Specific Interrupt Numbers ********************************************************/

  Input_IRQn                  = 0,
  RtcHz_IRQn                  = 1,
  RtcAlarm_IRQn               = 2,
  OsTimer_IRQn                = 3,
  AudioOut_IRQn               = 4,
  AudioIn_IRQn                = 5,
  Ethernet_IRQn               = 6,
  Joystick_IRQn               = 7,

} IRQn_Type;

#define __Vendor_SysTickConfig		1

//we test m0 with no mpu to make sure we properly support that

#define __NVIC_PRIO_BITS		2
#define __VTOR_PRESENT			1
#define __MPU_PRESENT			1
#include "core_cm0plus.h"




#endif
