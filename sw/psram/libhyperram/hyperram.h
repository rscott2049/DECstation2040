#ifndef _HYPERRAM_H
#define _HYPERRAM_H

#include "hardware/pio.h"

// 12-byte command packet to push to the PIO SM. After pushing the packet, can
// either push write data, or wait for read data.
typedef struct {
        uint32_t cmd0;
	uint32_t cmd1;
	uint32_t cmd2;
} hyperram_cmd_t;

// 16 byte command packet, used for reg write commands
typedef struct {
        uint32_t cmd0;
	uint32_t cmd1;
	uint32_t cmd2;
	uint32_t cmd3;
} hyperram_cmd_reg_w_t;

typedef enum {
	HRAM_CMD_READ = 0xA0,
	HRAM_CMD_WRITE = 0x20,
	HRAM_CMD_REGWRITE = 0x40,
	HRAM_CMD_REGREAD = 0xC0
} hyperram_cmd_flags;

// Number of uint32_t words to write to SM for a given command 
typedef enum {
	HRAM_CMD_READ_LEN = 3,
	// Memory write length does not include write data
	HRAM_CMD_WRITE_LEN = 3,
	// Reg writes are weird - write data is in third word sent
	HRAM_CMD_REGWRITE_LEN = 4,
	HRAM_CMD_REGREAD_LEN = 3
} hyperram_cmd_len;


// Note these are *byte* addresses, so are off by a factor of 2 from those given in datasheet
enum {
	HRAM_REG_ID0  = 0u << 12 | 0u << 1,
	HRAM_REG_ID1  = 0u << 12 | 1u << 1,
	HRAM_REG_CFG0 = 1u << 12 | 0u << 1,
	HRAM_REG_CFG1 = 1u << 12 | 1u << 1
};


typedef struct {
	PIO pio;
	uint sm;
	uint prog_offset;
	// DQ0->DQ7 starting here:
	uint dq_base_pin;
	// CSn, RWDS, CK, starting here:
	uint ctrl_base_pin;
        uint target_clk;
} hyperram_inst_t;

// Friendly functions
static hyperram_inst_t g_hram_all[4] = {
				 {.pio = pio0,
				  .sm = 0,
				  .dq_base_pin = 22,
				  .ctrl_base_pin = 18,
				 },
				 {.pio = pio0,
				  .sm = 1,
				  .dq_base_pin = 22,
				  .ctrl_base_pin = 18,
				 },
				 {.pio = pio0,
				  .sm = 2,
				  .dq_base_pin = 22,
				  .ctrl_base_pin = 18,
				 },
				 {.pio = pio0,
				  .sm = 3,
				  .dq_base_pin = 22,
				  .ctrl_base_pin = 18,
				 }
};

int hyperram_ram_init();
int hyperram_clk_init();
int hyperram_get_sysclk();
int hyperram_init();
void hyperram_read(uint32_t addr, uint8_t *dst, uint len);
void hyperram_write(uint32_t addr, const uint8_t *src, uint len);


void hyperram_pio_init(const hyperram_inst_t *inst);

void hyperram_read_blocking(const hyperram_inst_t *inst, uint32_t addr, uint32_t *dst, uint len);
void hyperram_write_blocking(const hyperram_inst_t *inst, uint32_t addr, const uint32_t *src, uint len);

void hyperram_write_with_mask(const hyperram_inst_t *inst, uint32_t addr, uint32_t src, uint32_t mask); 


void hyperram_write_blocking_unaligned(const hyperram_inst_t *inst, uint32_t addr, uint8_t *src, uint len);


uint16_t hyperram_cfg_read_blocking(const hyperram_inst_t *inst, uint32_t addr);
void hyperram_cfg_write_blocking(const hyperram_inst_t *inst, uint32_t addr, uint16_t wdata);

void _hyperram_cmd_init(hyperram_cmd_t *cmd, const hyperram_inst_t *inst, hyperram_cmd_flags flags, uint32_t addr, uint len);

#endif
