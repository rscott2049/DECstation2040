#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "hyperram.h"
#include "hyperram.pio.h"

// For setting 1.8v threshold
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

hyperram_inst_t hram_all[4] = {
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

hyperram_inst_t hram = {
  .pio = pio0,
  .sm = 0,
  .dq_base_pin = 22,
  //		.ctrl_base_pin = 0,
  //  .ctrl_base_pin = 18,
  // Start with debug pin
  //.ctrl_base_pin = 17,
  .ctrl_base_pin = 18,
};

void printdata(const uint8_t *data, uint len) {
	for (int i = 0; i < len; ++i)
		printf("%02x%c", data[i], i % 16 == 15 ? '\n' : ' ');
	if (len % 16 != 15)
		printf("\n"); 
}

void printdataint(const uint32_t *data, uint len) {
	for (int i = 0; i < len; ++i)
		printf("%08x%c", data[i], i % 8 == 7 ? '\n' : ' ');
	//	if (len % 8 != 7) printf("\n"); 
}

#define TESTLEN 256

// Generate a 32 bit psudeo-random number
// https://en.wikipedia.org/wiki/Xorshift
uint32_t lfsr(uint32_t x) {

    x |= x == 0;   // if x == 0, set x = 1 instead
    x ^= (x & 0x0007ffff) << 13;
    x ^= x >> 17;
    x ^= (x & 0x07ffffff) << 5;
    return x & 0xffffffff;
}		 



#define SEC_SIZE (1024*1024/4)
#define BUF_SIZE (1024/4)
#define NUM_BUF (SEC_SIZE/BUF_SIZE*32)
//#define BUF_SIZE 4
//#define BUF_SIZE 8

uint32_t gen_addr(uint32_t *rand, uint8_t *hit_buf) {
  uint32_t addr;
  uint32_t buf_num;
  uint32_t hit;

  while (1) {
    *rand = lfsr(*rand);
    addr = *rand & ((32 * (1 << 20)) - 1) & ~((BUF_SIZE << 2) - 1);
    buf_num = addr/(BUF_SIZE << 2);
    hit = hit_buf[buf_num];
    if (hit == 0) {
      hit_buf[buf_num]++;
      break;
    }
  }
  return addr;
}

int32_t memtest(uint32_t seed, uint32_t test_sel, uint32_t sm) {
  uint32_t i, j, k;
  uint32_t rand;
  uint32_t val;
  uint32_t addr;
  uint32_t size;
  uint32_t offset;
  uint32_t ret_val;
  uint32_t buf[BUF_SIZE];
  uint32_t rbuf[BUF_SIZE];
  uint32_t err_cnt;
  uint8_t hit_buf[NUM_BUF];

  // Note: this is number of 32 bit words per MB to test
  size = (test_sel == 1) ? 1024/4 : (1024 * 1024)/4;

  ret_val = 0;
  err_cnt = 0;

  // Short test does "size" writes then reads per 1 MB section
  if (test_sel == 1) {
    // We're testing 32 bit words, in 1 MB sections
    for (j = 0; j < 32; j++) {
      // Write data
      printf("W");
      rand = seed + (1 << j);
      for (i = 0; i < size/BUF_SIZE; i++) {
	// Generate a buffer of write data
	for (k = 0; k < BUF_SIZE; k++) {
	  rand = lfsr(rand);
	  buf[k] = rand;
	}

	// 32 bit word address, starting at each MB
	addr = (j * (1024 * 1024/4)) + i * BUF_SIZE;
	// Send 32 bit byte aligned addresses to psram
	hyperram_write_blocking(&hram_all[sm], addr<<2, buf, BUF_SIZE);
	//sleep_ms(1);
	//hyperram_write_blocking(&hram, addr<<2, buf, BUF_SIZE);
      }

      printf("R");
      rand = seed + (1 << j);
      for (i = 0; i < size/BUF_SIZE; i++) {
	addr = (j * (1024 * 1024/4)) + i * BUF_SIZE;

	// Get buffer from psram
	hyperram_read_blocking(&hram_all[sm], addr<<2, buf, BUF_SIZE);

	// Check it
	for (k = 0; k < BUF_SIZE; k++) {
	  rand = lfsr(rand);
	  val = buf[k];
	  if (val != rand) {
	    printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		   (addr + k) << 2 , rand, val);
	    printdataint(&buf[k], 8);
	    // 2nd read
	    hyperram_read_blocking(&hram_all[sm], addr<<2, buf, BUF_SIZE);
	    printdataint(&buf[k], 8);

	    ret_val--;
	    err_cnt++;
	    if (err_cnt > 3) return ret_val;
	  }
	}
      }
    }
  } else if (test_sel == 0) {
    // Long test does writes across entire memory, then reads
    // We're testing 32 bit words, in 1 MB sections
    for (j = 0; j < 32; j++) {
      // Write data
      printf("W");
      rand = seed + (1 << j);
      for (i = 0; i < size; i++) {
	// 32 bit word address, starting at each MB
	addr = (j * (1024 * 1024)/4) + i;
	rand = lfsr(rand);
	//rand = addr;
	// Send 32 bit byte aligned addresses to write mem
	hyperram_write_blocking(&hram_all[sm], addr << 2, &rand, 1);
      }
    }
    for (j = 0; j < 32; j++) {
      printf("R");
      rand = seed + (1 << j);
      for (i = 0; i < size; i++) {
	// 32 bit word address, starting at each MB
	addr = (j * (1024 * 1024)/4) + i;
	rand = lfsr(rand);
	//rand = addr;
	hyperram_read_blocking(&hram_all[sm], addr << 2, &val, 1);
	if (val != rand) {
	  printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		 addr << 2, rand, val);
	  for (int q = 0; q < 8; q++) {
	    hyperram_read_blocking(&hram_all[sm], (addr + q) << 2, &val, 1);
	    printf("%08x ", val);
	  }
	  printf("\n");
	  ret_val--;
	  err_cnt++;
	  if (err_cnt > 3) return ret_val;
	}
      }
    }
  } else if (test_sel == 2) {
    // Random test writes random length buffers to random addresses, then reads.
    // Check results every 1 MB, do 32 MB total.
    int count = 0;
    for (int pass = 0 ; pass < 32; pass++) {
      printf("W");
      rand = seed + (1 << pass);
      for (i = 0; i < NUM_BUF; i++) {
	hit_buf[i] = 0;
      }
      // Do 1 MB or so writes
      for (k = 0;k < (1 << 18);) {
	// Generate size
	rand = lfsr(rand);
	// BUF_SIZE must be power of two for this to work
	j = rand & (BUF_SIZE - 1);
	if (j == 0) j = 1;
	// Update running size
	k += j;
	for (i = 0; i < j; i++) {
	  rand = lfsr(rand);
	  buf[i] = rand;
	}
	// Make BUF_SIZE aligned 32MB addr (Buffer is word sized)
	rand = lfsr(rand);
	//addr = rand & ((32 * (1 << 20)) - 1) & ~((BUF_SIZE << 2) - 1);
	addr = gen_addr(&rand, hit_buf);
	// Write to middle of buffer
	addr = addr + (((BUF_SIZE - 1 - j)/2) << 2);
	// Write buffer to memory
      	hyperram_write_blocking(&hram_all[sm], addr, buf, j);
      }    
      
      // Do 1 MB or so reads
      printf("R");
      rand = seed + (1 << pass);
      for (i = 0; i < NUM_BUF; i++) {
	hit_buf[i] = 0;
      }
      for (k = 0; k < (1 << 18);) {
	// Generate size
	rand = lfsr(rand);
	// BUF_SIZE must be power of two for this to work
	j = rand & (BUF_SIZE - 1);
	if (j == 0) j = 1;
	// Update running size
	k += j;
	for (i = 0; i < j; i++) {
	  rand = lfsr(rand);
	  buf[i] = rand;
	}
	// Make BUF_SIZE aligned 32MB address
	rand = lfsr(rand);
	//addr = rand & ((32 * (1 << 20)) - 1) & ~((BUF_SIZE << 2) - 1);
	addr = gen_addr(&rand, hit_buf);
	// Read from middle of buffer
	addr = addr + (((BUF_SIZE - 1 - j)/2) << 2);
	// Read buffer from memory and check
      	hyperram_read_blocking(&hram_all[sm], addr, rbuf, j);
	for (i = 0; i < j; i++) {
	  if (buf[i] != rbuf[i]) {
	    printf("\nreq addr/size: %08x %04x", addr, j);
	    printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		   (addr + i), buf[i], rbuf[i]);
	    printdataint(&rbuf[i], 8);
	    // 2nd read
	    hyperram_read_blocking(&hram_all[sm], addr, rbuf, j);
	    printdataint(&rbuf[i], 8);
	    ret_val--;
	    err_cnt++;
	    if (err_cnt > 3) return ret_val;
	  }
	}
      }    
    }
  } else if (test_sel == 4) {
    // Speed test: does 40 MB reads/writes, with perf report
    if (BUF_SIZE != 1024/4) {
      printf("BUF_SIZE is not 1KB\n");
      return -1;
    }

    // Generate data buf
    rand = lfsr(seed);
    for (i = 0; i < BUF_SIZE; i++) {
      rand = lfsr(rand);
      buf[i] = rand;
    }

    // Write 40 MB, or 10MW
    absolute_time_t from = get_absolute_time();
    uint32_t payload = 10 * 1024 * 1024/BUF_SIZE;
    uint32_t xfer_tot = 0;
    for (i = 0; i < payload; i++) {
      for (j = 0; j < BUF_SIZE/32; j++) {
	// Address is in bytes
	addr = (i * BUF_SIZE) << 2;
	// Transfer size is in words
	hyperram_write_blocking(&hram_all[sm], addr, &buf[j], 32);
	xfer_tot += 32;
      }
    }

    absolute_time_t to = get_absolute_time();
    int64_t deltaT = absolute_time_diff_us(from, to);
    float MB = (xfer_tot * 4)/(1024 * 1024);
    // convert usec to sec: 1000000us = 1sec
    printf("Write MB/s: %f\n", (MB * 1000000)/(float)(deltaT));
    printf("Total MB: %f\n", MB);

    // Read 40 MB, or 10MW
    from = get_absolute_time();
    payload = 10 * 1024 * 1024/BUF_SIZE;
    xfer_tot = 0;
    for (i = 0; i < payload; i++) {
      for (j = 0; j < BUF_SIZE/32; j++) {
	// Address is in bytes
	addr = (i * BUF_SIZE) << 2;
	// Transfer size is in words
	hyperram_read_blocking(&hram_all[sm], addr, &rbuf[j], 32);
	xfer_tot += 32;
	// Should do checking here, but that would impact time reported
      }
    }

    to = get_absolute_time();
    deltaT = absolute_time_diff_us(from, to);
    MB = (xfer_tot * 4)/(1024 * 1024);
    // convert usec to sec: 1000000us = 1sec
    printf("Read MB/s: %f\n", (MB * 1000000)/(float)(deltaT));
    printf("Total MB: %f\n", MB);
  } else {
    printf("Unknown test selection: %d\n", test_sel);
    ret_val = -1;
  }
  return ret_val;
}


int main() {
  int clkdiv;
  int pass = 0;

#if 1
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);
  //  set_sys_clock_khz(252 * 1000, true);
  //pass = set_sys_clock_khz(300 * 1000, true);
  // Seems to be max RP2040 operational frequency
  //pass = set_sys_clock_khz(315 * 1000, true);
  //pass = set_sys_clock_khz(100 * 1000, true);
  //pass = set_sys_clock_khz(133 * 1000, true);
  //pass = set_sys_clock_khz(166 * 1000, true);
  //pass = set_sys_clock_khz(180 * 1000, true);
  //pass = set_sys_clock_khz(190 * 1000, true);
  //pass = set_sys_clock_khz(200 * 1000, true);

  //pass = set_sys_clock_khz(225 * 1000, true);
  //pass = set_sys_clock_khz(250 * 1000, true);
  //pass = set_sys_clock_khz(266 * 1000, true);
  // Passes for > 1 day here
  //pass = set_sys_clock_khz(133 * 1000, true);
  //pass = set_sys_clock_khz(280 * 1000, true);  
  pass = set_sys_clock_khz(300 * 1000, true);
  sleep_ms(1000);
#endif

  //	setup_default_uart();
  stdio_init_all();
  sleep_ms(3000);

  if (pass) {
    printf("Set sysclk to 300 MHz\n");
  } else {
    printf("Did not set sysclk to 300 MHz\n");
  }

  // Set 1.8v threshold for I/O pads
#if 1
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;

  printf("pad bank0 voltage select addr/data: %08x %08x\n", (uint32_t)addr,
	 (uint32_t)*addr);
#else
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_3V3 << PADS_BANK0_VOLTAGE_SELECT_LSB;

  printf("pad bank0 voltage select addr/data: %08x %08x\n", (uint32_t)addr,
	 (uint32_t)*addr);
#endif

  printf("Loading program\n");
  hram.prog_offset = pio_add_program(hram.pio, &hyperram_program);

  for (int i = 0; i < 4; i++) {
    printf("Initialising psram state machine %d\n", i);
    hyperram_pio_init(&hram_all[i]);
  }

  clkdiv = 1;

  printf("Setting clock divisor to %d\n", clkdiv);
  pio_sm_set_clkdiv(hram.pio, hram.sm, clkdiv);

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


  uint16_t cfg_reg;
#if 0
  printf("Writing %04x to CR0 register\n", cfgreg_init);
  hyperram_cfg_write_blocking(&hram, HRAM_REG_CFG0, cfgreg_init);
#else
  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_CFG0);
  printf("Reading cfg 0: %08x\n", cfg_reg);

  printf("Writing %04x to CR0 register\n", cfgreg_init);
  hyperram_cfg_write_blocking(&hram, HRAM_REG_CFG0, cfgreg_init);

  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_CFG0);
  printf("Reading cfg 0: %08x\n", cfg_reg);
#endif


  uint32_t testdata[128];
  uint32_t rdata[128];

#if 1
  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_ID0);
  printf("id 0: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_ID1);
  printf("id 1: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_CFG0);
  printf("cfg 0: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&hram, HRAM_REG_CFG1);
  printf("cfg 1: %08x\n", cfg_reg);
#endif

#if 0
  uint32_t wdata[] = {
    0x0000100f, 0x0a8000ef, 0x00000517, 0x01c50513,
    0x30551073, 0xfff00513, 0x3b051073, 0xdeadbeef};

  hyperram_write_blocking(&hram_all[1], 0x0, wdata, 8);
  hyperram_read_blocking(&hram_all[0], 0x0, rdata, 8);
  printdataint(wdata, 8);
  printdataint(rdata, 8);
  while(1) {}
#endif

  //hyperram_cfg_read_blocking(&hram, HRAM_REG_ID1, testdata, 1);

  //while (1) {}

  //while (1) hyperram_cfg_read_blocking(&hram, (1 << 12) + 2, testdata, 1);

  //hyperram_cfg_write_blocking(&hram, (1 << 12) + 2, 0xc1ff);
  //while (1) hyperram_cfg_write_blocking(&hram, (1 << 12) + 2, 0xc1ff);

  //while (1) hyperram_read_blocking(&hram, 0, testdata, 1);

  //while (1) hyperram_write_blocking(&hram, 0x0, testdata, 1);

  //while (1) hyperram_read_blocking(&hram, 0, testdata, 8);
  //hyperram_read_blocking(&hram, 0, testdata, 8);
  //while (1) {}

  //while (1) hyperram_write_blocking(&hram, 0x0, testdata, 8);
  //hyperram_write_blocking(&hram, 0x0, testdata, 4);
  //while (1) {}

#if 0
  while (1) {
  hyperram_cfg_write_blocking(&hram, 0x0, 0x0);
  hyperram_cfg_write_blocking(&hram, 0x0, -1);
  }
#endif

#if 0
  while (1) {
    hyperram_read_blocking(&hram, 0, testdata, 1);
    hyperram_write_blocking(&hram, 0x0, testdata, 1);
  }
#endif

#if 0
  uint8_t *testdatab = &testdata[0];
  for (int i = 0; i < 32; i++) {
    testdatab[i] = i;
  }
  hyperram_write_blocking(&hram, 0x40000 << 2, testdata, 4);
  hyperram_read_blocking(&hram,  0x40000 << 2, testdata, 8);
  printdataint(testdata, 8);
  hyperram_read_blocking(&hram, 0x0 << 2, testdata, 8);
  printdataint(testdata, 8);

  for (int i = 0; i < 32; i++) {
    testdatab[i] = ~i;
  }
  hyperram_write_blocking(&hram, 0x0, testdata, 4);
  hyperram_read_blocking(&hram, 0x0 << 2, testdata, 8);
  printdataint(testdata, 8);
  hyperram_read_blocking(&hram,  0x40000 << 2, testdata, 8);
  printdataint(testdata, 8);

  while (1) {}

#endif

#if 0
  uint32_t rand = 2;
  for (int i = 0; i < 16; i++) {
    rand = lfsr(rand);
    testdata[i] = rand;
    testdata[i] = -1;
  }
  printdataint(testdata, 8);

  hyperram_write_blocking(&hram, 0x0, testdata, 16);
  
  hyperram_read_blocking(&hram, 0x0, rdata, 16);
  printdataint(rdata, 8);
  printdataint(&rdata[8], 8);

  for (int i = 0; i < 16; i++) {
    hyperram_write_with_mask(&hram, i << 2, 0, i);
  }
  hyperram_read_blocking(&hram, 0x0, rdata, 16);
  printdataint(rdata, 8);
  printdataint(&rdata[8], 8);

#endif

#if 0
  for (uint32_t i = 0; i < 8; i++) {
    hyperram_write_blocking(&hram, 0x0, testdata, 8);
    hyperram_write_blocking_unaligned(&hram, 0, testbyte, i);
    printf("%d ", i);
    hyperram_read_blocking(&hram, 0x0, rdata, 8);
    printdataint(rdata, 8);
  }
#endif

#if 0
  for (uint32_t i = 0; i < 8; i++) {
    hyperram_write_blocking(&hram, 0x0, testdata, 8);
    hyperram_write_blocking_unaligned(&hram, 3, testbyte, 5);
    printf("%d ", i);
    hyperram_read_blocking(&hram, 0x0, rdata, 8);
    printdataint(rdata, 8);
  }
#endif  

  //  while(1) { }
  //while (1) hyperram_read_blocking(&hram, 0, testdata, 8);

  //while (1) hyperram_write_blocking(&hram, 0x0, testdata, 1);  
  //sleep_us(500);
  //hyperram_write_blocking(&hram, 0x0, testdata, 4);
  //hyperram_write_blocking(&hram, 0x0, testdata, 1);  
  //while (1) {}

#if 0
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 16; i++) testdata[i] = 0;
    hyperram_read_blocking(&hram, j * 4, testdata, 8);
    printdataint(testdata, 16);
  }

  sleep_ms(2000);
  return 0;
#endif

#if 0
  hyperram_read_blocking(&hram, 0xc0000000, testdata, 1);
  while(1) {}
#endif

#if 0
  while(1) {
    hyperram_read_blocking(&hram, 0xc0000000, testdata, 1);
  }
#endif


  pass = 0;
  //pass = 45000;
  //pass = 1985416 - 1;
  //pass = 2001800 - 1;
  //pass = 2009992 - 1;
  //pass = 2119015 - 1;
  //pass = 4362778 - 10;
  
  // Check MB/s
  memtest(pass, 4, 0);

  sleep_ms(4000);
  
  while (1) {
#if 0
    printf("Pass: %4d ", pass);
    if (memtest(pass, 0, 0)) break;
    printf("\nShort      ", pass);
    if (memtest(pass, 1, 1)) break;
    printf("\nRandom     ", pass);
    if (memtest(pass, 2, 2)) break;
#else
    printf("Pass: %4d ", pass);
    if (memtest(pass, 1, 0)) break;
#endif
    printf("\n");
    pass++;
  }
  sleep_ms(2000);
#if 0
	for (int clkdiv = 10; clkdiv > 0; --clkdiv) {
		printf("\nSetting clock divisor to %d\n", clkdiv);
		pio_sm_set_clkdiv(hram.pio, hram.sm, clkdiv);
		printf("Writing:\n");
		uint8_t testdata[TESTLEN];
		// Clear, then write test pattern
		for (int i = 0; i < TESTLEN; ++i)
			testdata[i] = 0;
		hyperram_write_blocking(&hram, 0x1234, (const uint32_t*)testdata, TESTLEN / sizeof(uint32_t));
		for (int i = 0; i < TESTLEN; ++i)
			testdata[i] = i & 0xff;
		// printdata(testdata, TESTLEN);
		hyperram_write_blocking(&hram, 0x1234, (const uint32_t*)testdata, TESTLEN / sizeof(uint32_t));

		printf("Reading back:\n");
		hyperram_read_blocking(&hram, 0x1234, (uint32_t*)testdata, TESTLEN / sizeof(uint32_t));
		printdata(testdata, TESTLEN);
	}
#endif
}

