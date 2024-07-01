#include <stdint.h>
#include "ucHw.h"
#if 0
// For setting 1.8v threshold
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"
// For sleep_ms
#include "pico/stdlib.h"
#endif
#include "hyperram.h"

// Must be called before anything, as this changes the system clock
void initHwSuperEarly(void) {
#if 0
  uint32_t target_clk = 300000000;
  uint32_t cfg_read;

  // Setup overclock, which needs a bit of a voltage boost
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);
  set_sys_clock_khz(target_clk/1000, true);


  // Set 1.8v threshold for I/O pads
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;
#endif

  hyperram_clk_init();

}

void initHw(void) {
}

//fatal errors only
void hwError(uint_fast8_t err) {
}

//in bytes
uint8_t hwGetUidLen(void) {
}

uint8_t hwGetUidByte(uint_fast8_t idx) {
}
