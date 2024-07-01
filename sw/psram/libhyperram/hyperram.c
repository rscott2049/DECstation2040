#include <stdio.h>

// For setting 1.8v threshold
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "hardware/sync.h"

#include "hyperram.h"
#include "hyperram.pio.h"


// Offsets from ctrl_base_pin 
#define CTRL_PIN_CK 0
#define CTRL_PIN_CS 1
#define CTRL_PIN_RWDS 2

void hyperram_pio_init(const hyperram_inst_t *inst) {

  for (uint i = inst->dq_base_pin; i < inst->dq_base_pin + 8; ++i) {
    // Setting both pull bits enables bus-keep function
    gpio_set_pulls(i, true, true);
    pio_gpio_init(inst->pio, i);
  }

  // Disable synchronizers - remove two clock delays...
  hw_clear_bits(&inst->pio->input_sync_bypass, 0xffu << inst->dq_base_pin);

  // Init control pins: CLK, CSn, RWDS
  for (uint i = inst->ctrl_base_pin; i < inst->ctrl_base_pin + 3; ++i)
    pio_gpio_init(inst->pio, i);

  // Ensure that RWDS is deasserted when floated by PSRAM
  gpio_pull_down(inst->ctrl_base_pin + CTRL_PIN_RWDS);

  // Set slew rate/drive strength
  for (uint i = inst->ctrl_base_pin; i < inst->ctrl_base_pin + 3; ++i) {
    gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_8MA);
  }

  for (uint i = inst->dq_base_pin; i < inst->dq_base_pin + 8; ++i) {
    gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_8MA);
  }

  // All controls low except CSn
  pio_sm_set_pins_with_mask(inst->pio, inst->sm,
			    (1u << CTRL_PIN_CS) << inst->ctrl_base_pin,
			    0x0fu << inst->ctrl_base_pin
			    );

  // All controls output except RWDS (DQs will sort themselves out later)
  pio_sm_set_pindirs_with_mask(inst->pio, inst->sm,
			       (1u << CTRL_PIN_CS |
				1u << CTRL_PIN_CK) << inst->ctrl_base_pin,
			       0x03u << inst->ctrl_base_pin
			       );

  pio_sm_config c = hyperram_program_get_default_config(inst->prog_offset);
  sm_config_set_out_pins(&c, inst->dq_base_pin, 8);
  sm_config_set_in_pins(&c, inst->dq_base_pin);
  sm_config_set_set_pins(&c, inst->ctrl_base_pin + CTRL_PIN_CS, 2);
  sm_config_set_sideset_pins(&c, inst->ctrl_base_pin);
  sm_config_set_in_shift(&c, true, true, 32);
  sm_config_set_out_shift(&c, true, true, 32);

  // Setup for variable latency
  sm_config_set_jmp_pin(&c, inst->ctrl_base_pin + CTRL_PIN_RWDS);

  // Release the SM
  // Only SM 0 is started with the irq flag set
  if (inst->sm == 0) {
    pio_sm_init(inst->pio, inst->sm,
		inst->prog_offset + hyperram_offset_passOn, &c);
  } else {
    pio_sm_init(inst->pio, inst->sm, inst->prog_offset, &c);
  }
  pio_sm_set_enabled(inst->pio, inst->sm, true);

#ifdef HAVE_PSRAM_RESET_PIN
  gpio_put(inst->rst_n_pin, 1);
#endif
}




// HyperRAM command format from S27KL0641 datasheet:
// ------+------------------------------+------------------------------------+
// Bits  | Name                         | Description                        |
// ------+------------------------------+------------------------------------+
// 47    | R/W#                         | 1 for read, 0 for write            |
//       |                              |                                    |
// 46    | AS                           | 0 for memory address space, 1 for  |
//       |                              | register space                     |
//       |                              |                                    |
// 45    | Burst                        | 0 for wrapped, 1 for linear        |
//       |                              |                                    |
// 44:16 | Row and upper column address | Address bits 31:3, irrelevant bits |
//       |                              | should be 0s                       |
//       |                              |                                    |
// 15:3  | Reserved                     | Set to 0                           |
//       |                              |                                    |
// 2:0   | Lower column address         | Address bits 2:0                   |
// ------+------------------------------+------------------------------------+

void _hyperram_cmd_init(hyperram_cmd_t *cmd, const hyperram_inst_t *inst, hyperram_cmd_flags flags, uint32_t addr, uint len) {
  uint32_t next_pc;
  uint32_t cmd1_be;

        // Only enforce halfword alignment, so that we can read/write PSRAM regs
	addr = (addr >> 1);
	uint32_t addr_l = addr & 0x7u;
	// Add flags to addr_h upper bits for Command/Address 0
	uint32_t addr_h = (addr >> 3) | (flags << 24);
	
	// Convert word len to uint16 len - 1
	len = (len * 2) - 1;

	// Little endian, so first byte is 31:24
	// Send big endian Command/Address values via little endain PIO FIFO
	// (i.e. byte swap CA values)
	switch (flags) {
	case (HRAM_CMD_READ):
	case (HRAM_CMD_REGREAD):
	  // Start command word
	  // Big endian: 0xff, cmd len [2], CA0, CA1
	  //cmd->cmd0 = 0xff << 24 | 2 << 16 | (addr_h >> 16);
	  // Little endian: CA1, CA0, cmd len [2], 0xff
	  //	  cmd->cmd0 = (addr_h >> 16) << 16 | 2 << 8 | 0xff;
	  cmd->cmd0 = (((addr_h >> 16) & 0xff) << 24 |
		       ((addr_h >> 24) & 0xff) << 16 |
		       0xff << 8 | 2 );
	  // Big endian: CA2, 3, 4, 5
	  //cmd->cmd1 = (addr_h << 16) | addr_l;
	  // Little endian: CA5, 4, 3, 2
	  cmd1_be = (addr_h << 16) | addr_l;
	  cmd->cmd1 = (((cmd1_be >> 24) & 0xff) |
		       (((cmd1_be >> 16) & 0xff) << 8) |
		       (((cmd1_be >> 8) & 0xff) << 16) |
		       (((cmd1_be >> 0) & 0xff) << 24));

          // Goto read latency/data
	  next_pc = (inst->prog_offset + hyperram_offset_r_lat);
	  // Big endian:
	  //cmd->cmd2 = len << 16 | 0x00 << 8 | next_pc;
	  // Little endian
	  cmd->cmd2 = next_pc << 24 | 0x00 << 16| len;
	  break;
	case (HRAM_CMD_WRITE):
	  // Big endian: 0xff, cmd len [2], CA0, CA1
	  // cmd->cmd0 = 0xff << 24 | 2 << 16 | (addr_h >> 16);
	  // Little endian: CA1, CA0, cmd len [2], 0xff
	  cmd->cmd0 = (((addr_h >> 16) & 0xff) << 24 |
		       ((addr_h >> 24) & 0xff) << 16 |
		       0xff << 8 | 2);
	  // Big endian: CA2, 3, 4, 5
	  //cmd->cmd1 = (addr_h << 16) | addr_l;
	  // Little endian: CA5, 4, 3, 2
	  cmd1_be = (addr_h << 16) | addr_l;
	  cmd->cmd1 = (((cmd1_be >> 24) & 0xff) |
		       (((cmd1_be >> 16) & 0xff) << 8) |
		       (((cmd1_be >> 8) & 0xff) << 16) |
		       (((cmd1_be >> 0) & 0xff) << 24));

          // Goto write latency/data
	  next_pc = (inst->prog_offset + hyperram_offset_w_lat);
	  // Big endian:
	  //cmd->cmd2 = len << 16 | 0xff << 8 | next_pc;
	  // Little endian
	  cmd->cmd2 = next_pc << 24 | 0xff << 16| len;
	  break;
	}
}

void _hyperram_cmd_init_reg_w(hyperram_cmd_reg_w_t *cmd, const hyperram_inst_t *inst, hyperram_cmd_flags flags, uint32_t addr, uint len) {
  uint32_t next_pc;
  uint32_t cmd1_be;

  // Only enforce halfword alignment, so that we can read/write PSRAM regs
  addr = (addr >> 1);
  uint32_t addr_l = addr & 0x7u;
  // Add flags to addr_h upper bits for Command/Address 0
  uint32_t addr_h = (addr >> 3) | (flags << 24);
	
  // Convert word len to uint16 len - 1
  len = (len * 2) - 1;

  // Little endian, so first byte is 31:24
  // Send big endian Command/Address values via little endain PIO FIFO
  // (i.e. byte swap CA values)
  // 0xff, cmd len [4], CA0, CA1
  cmd->cmd0 = (((addr_h >> 16) & 0xff) << 24 |
	       ((addr_h >> 24) & 0xff) << 16 |
	       0xff << 8 | 4);

  // Big endian: CA2, 3, 4, 5
  //cmd->cmd1 = (addr_h << 16) | addr_l;
  // Little endian: CA5, 4, 3, 2
  cmd1_be = (addr_h << 16) | addr_l;
  cmd->cmd1 = (((cmd1_be >> 24) & 0xff) |
	       (((cmd1_be >> 16) & 0xff) << 8) |
	       (((cmd1_be >> 8) & 0xff) << 16) |
	       (((cmd1_be >> 0) & 0xff) << 24));

  // Next command word (cmd2 is register data)
  next_pc = (inst->prog_offset + hyperram_offset_passOn);
  // Big endian:
  //cmd->cmd3 = next_pc;
  // Little endian:
  cmd->cmd3 = next_pc << 24;
}

void __not_in_flash_func(hyperram_read_blocking)(const hyperram_inst_t *inst, uint32_t addr, uint32_t *dst, uint len) {
	hyperram_cmd_t cmd;

	//uint32_t save_irq = save_and_disable_interrupts();

	_hyperram_cmd_init(&cmd, inst, HRAM_CMD_READ, addr, len);

	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
       	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd2);

	for (uint i = 0; i < len; ++i) {
	  dst[i] = pio_sm_get_blocking(inst->pio, inst->sm);
	}

	//restore_interrupts(save_irq);
}

void __not_in_flash_func(hyperram_write_blocking)(const hyperram_inst_t *inst, uint32_t addr, const uint32_t *src, uint len) {
	hyperram_cmd_t cmd;

	//uint32_t save_irq = save_and_disable_interrupts();

	_hyperram_cmd_init(&cmd, inst, HRAM_CMD_WRITE, addr, len);

	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd2);

	for (uint i = 0; i < len; ++i)
			pio_sm_put_blocking(inst->pio, inst->sm, src[i]);

	//restore_interrupts(save_irq);

}

void hyperram_cfg_write_blocking(const hyperram_inst_t *inst, uint32_t addr, uint16_t wdata_be) {
	hyperram_cmd_reg_w_t cmd;
	uint32_t wdata;

	_hyperram_cmd_init_reg_w(&cmd, inst, HRAM_CMD_REGWRITE, addr, 0);

	// Need to byte swap cfg value - registers are big endian
	wdata = wdata_be << 8 | ((wdata_be >> 8) & 0xff);

	// And replicate it, as we always write 32 bits
	wdata = wdata << 16 | wdata;

	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1);
	pio_sm_put_blocking(inst->pio, inst->sm, wdata);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd3);
}

 uint16_t hyperram_cfg_read_blocking(const hyperram_inst_t *inst, uint32_t addr) {
	hyperram_cmd_t cmd;
	uint32_t data_be;
	uint16_t data;

	_hyperram_cmd_init(&cmd, inst, HRAM_CMD_REGREAD, addr, 1);

	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd2);

	data_be = pio_sm_get_blocking(inst->pio, inst->sm);
	
	// Byte swap register data - they are big endian
	data = (data_be & 0xff) << 8 | ((data_be >> 8) & 0xff);

	return data;
}

 // Friendly functions that don't require passing in hyperram_inst_t...
int hyperram_clk_init() {
  uint32_t target_clk = 300000000;
    
  // Setup overclock, which needs a bit of a voltage boost
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);
  set_sys_clock_khz(target_clk/1000, true);

  // Set 1.8v threshold for I/O pads
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;

  // Save system clk
  for (int i = 0; i < 4; i++) {
    g_hram_all[i].target_clk = target_clk;
  }

  return target_clk;
}

int hyperram_get_sysclk() {
  return g_hram_all[0].target_clk;
}

int hyperram_ram_init() {
  uint32_t cfg_read;

  g_hram_all[0].prog_offset = pio_add_program(g_hram_all[0].pio, &hyperram_program);

  for (int i = 0; i < 4; i++) {
    g_hram_all[i].prog_offset = g_hram_all[0].prog_offset;
    hyperram_pio_init(&g_hram_all[i]);
    pio_sm_set_clkdiv(g_hram_all[i].pio, g_hram_all[i].sm, 1);
  }

  uint16_t cfgreg_init =
    (0x1u << 15) | // Do not enter power down
    //(0x1u << 12) | // 115R drive strength
    //(0x0u << 12) | // Default drive strength (34R)
    //(0x5u << 12) | // 27R drive strength
    (0x7u << 12) | // 19R drive strength
    (0xeu << 4)  | // 3 latency cycles (in bias -5 format)
    //(0xfu << 4)  | // 4 latency cycles (in bias -5 format)
    //(0x0u << 4)  | // 5 latency cycles (in bias -5 format)
    //(0x1u << 4)  | // 6 latency cycles (in bias -5 format)
    //(0x2u << 4)  | // Default 7 latency cycles (in bias -5 format)
    //(0x1u << 3);   // Fixed 2x latency mode
    (0x0u << 3);   // Variable latency mode
    
  // Don't care about wrap modes, we're always linear
  //printf("Writing %04x to CR0 register\n", cfgreg_init);
  hyperram_cfg_write_blocking(&g_hram_all[0], HRAM_REG_CFG0, cfgreg_init);

  // Check to see if cfg_write matches with expected
  cfg_read = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_CFG0);
	   
  // Sometimes needs a second chance
  cfg_read = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_CFG0);

  // Only look at read/write bits
  if ((cfg_read & 0xff) != (cfgreg_init & 0xff)) {
    //printf("init cfg exp/act: %02x %02x\n", cfgreg_init & 0xff, cfg_read & 0xff);
    return -1;
  }

  return 0;
}

int hyperram_init() {
  
  int target_clk = hyperram_clk_init();

  if (hyperram_ram_init() == -1) return -1;

  return target_clk;
}


// Friendly read/write functions 
// Address, length are bytes - will be aligned to word quantities
void hyperram_read(uint32_t addr, uint8_t *dst, uint len) {
  // Make byte length into word length
  len = len >> 2;
  hyperram_read_blocking(&g_hram_all[0], addr, (uint32_t*)dst, len);
}

void hyperram_write(uint32_t addr, const uint8_t *src, uint len) {
  // Make byte length into word length
  len = len >> 2;
  hyperram_write_blocking(&g_hram_all[0], addr, (uint32_t *)src, len);
}
 
// Word operations
void hyperram_read_32(uint32_t addr, uint32_t *dst, uint len) {
  hyperram_read_blocking(&g_hram_all[0], addr, dst, len);
}

void hyperram_write_32(uint32_t addr, const uint32_t *src, uint len) {
  hyperram_write_blocking(&g_hram_all[0], addr, src, len);
}
