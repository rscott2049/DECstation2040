#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "hyperram.h"
#include "fb_mono.h"

#if 0
// For setting 1.8v threshold
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

// VGA pins
#ifdef REV_1_2
#define VGA_VSYNC_PIN 13
#define VGA_HSYNC_PIN 14
#define VGA_GREEN_PIN 16
#else
// Rev 1.3 and above
#define VGA_HSYNC_PIN 7
#define VGA_VSYNC_PIN 8
#define VGA_GREEN_PIN 9
#endif

// Select one video format from below
//#define USE_1024x768
#define USE_1024x768_70
//#define USE_640x480
#endif

// Parameters for 1024x768 @ 60 Hz
#ifdef USE_1024x768
#define PIX_CLK 65000000

#define H_TOTAL 1344
#define H_ACTIVE 1024
#define H_FP 24
#define H_SYNC 136
#define H_BP 160
#define H_SYNC_POLARITY 0

#define V_TOTAL 806
#define V_ACTIVE 768
#define V_FP 3
#define V_SYNC 6
#define V_BP 29
#define V_SYNC_POLARITY 0
#endif

// Parameters for 1024x768 @ 70 Hz
#ifdef USE_1024x768_70
#define PIX_CLK 75000000

#define H_TOTAL 1328
#define H_ACTIVE 1024
#define H_FP 24
#define H_SYNC 136
#define H_BP 144
#define H_SYNC_POLARITY 0

#define V_TOTAL 806
#define V_ACTIVE 768
#define V_FP 3
#define V_SYNC 6
#define V_BP 29
#define V_SYNC_POLARITY 0
#endif

// Parameters for 640 x 480 @60 Hz
#ifdef USE_640x480
#define PIX_CLK 25175000

#define H_TOTAL 800
#define H_ACTIVE 640
#define H_FP 16
#define H_SYNC 96
#define H_BP 48
#define H_SYNC_POLARITY 1

#define V_TOTAL 525
#define V_ACTIVE 480
#define V_FP 10
#define V_SYNC 2
#define V_BP 33
#define V_SYNC_POLARITY 1
#endif


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
//#define BUF_SIZE (1024/4)
// Works with FB enabled
//#define BUF_SIZE (1024/8)
// A 1024 1bpp scanline
#define BUF_SIZE 32
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
  uint32_t i, j, k, y;
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
	addr = (j * (1024 * 1024/4)) + (i * BUF_SIZE) + 128;
	// Send 32 bit byte aligned addresses to psram
	hyperram_write_blocking(&g_hram_all[sm], addr<<2, buf, BUF_SIZE);
      }

      printf("R");
      rand = seed + (1 << j);
      for (i = 0; i < size/BUF_SIZE; i++) {
	addr = (j * (1024 * 1024/4)) + (i * BUF_SIZE) + 128;

	// Get buffer from psram
	hyperram_read_blocking(&g_hram_all[sm], addr<<2, buf, BUF_SIZE);

	// Check it
	for (k = 0; k < BUF_SIZE; k++) {
	  rand = lfsr(rand);
	  val = buf[k];
	  if (val != rand) {
	    printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		   (addr + k) << 2 , rand, val);
	    printdataint(&buf[k], 8);
	    // 2nd read
	    hyperram_read_blocking(&g_hram_all[sm], addr<<2, buf, BUF_SIZE);
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
	hyperram_write_blocking(&g_hram_all[sm], addr << 2, &rand, 1);
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
	hyperram_read_blocking(&g_hram_all[sm], addr << 2, &val, 1);
	if (val != rand) {
	  printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		 addr << 2, rand, val);
	  for (int q = 0; q < 8; q++) {
	    hyperram_read_blocking(&g_hram_all[sm], (addr + q) << 2, &val, 1);
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
	// Make BUF_SIZE aligned 32MB addr (Buffer is word sized)
	rand = lfsr(rand);
	//addr = rand & ((32 * (1 << 20)) - 1) & ~((BUF_SIZE << 2) - 1);
	addr = gen_addr(&rand, hit_buf);
	// Write to middle of buffer
	addr = addr + (((BUF_SIZE - 1 - j)/2) << 2);
	// Write buffer to memory
      	hyperram_write_blocking(&g_hram_all[sm], addr, buf, j);
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
      	hyperram_read_blocking(&g_hram_all[sm], addr, rbuf, j);
	for (i = 0; i < j; i++) {
	  if (buf[i] != rbuf[i]) {
	    printf("\nreq addr/size: %08x %04x", addr, j);
	    printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		   (addr + i), buf[i], rbuf[i]);
	    printdataint(&rbuf[i], 8);
	    // 2nd read
	    hyperram_read_blocking(&g_hram_all[sm], addr, rbuf, j);
	    printdataint(&rbuf[i], 8);
	    ret_val--;
	    err_cnt++;
	    if (err_cnt > 3) return ret_val;
	  }
	}
      }    
    }
  } else if (test_sel == 3) {

    // This runs so fast, we do 32 frames 
    // First 96 KB test (i.e. frame buffer)
    // Write 768 buffers of 32 bytes (i.e. one 1024 1bpp scanline)
    for (j = 0; j < 32; j++) {
      // Write a frame of random data
      printf("W");

      // Write data
      rand = seed + (1 << j);
      for (y = 0; y < 768; y++) {
	// Generate a buffer of write data
	for (k = 0; k < 32; k++) {
	  rand = lfsr(rand);
	  buf[k] = rand;
	}

	// 32 bit word address, starting at each scanline
	addr = (y + 40) * _inst.words_per_scanline + 2; 

	// Send 32 bit byte aligned addresses to psram
	//hyperram_write_blocking(&g_hram_all[sm], addr<<2, buf, 8);
	hyperram_write_blocking(&g_hram_all[sm], addr<<2, buf, 32);
      }

      printf("R");
      rand = seed + (1 << j);
      for (y = 0; y < 768; y++) {
	// Get buffer from psram
	addr = (y + 40) * _inst.words_per_scanline + 2; 

	//hyperram_read_blocking(&g_hram_all[sm], addr<<2, buf, 8);
	hyperram_read_blocking(&g_hram_all[sm], addr<<2, buf, 32);
	// Check it
	for (k = 0; k < 32; k++) {
	  rand = lfsr(rand);
	  val = buf[k];
	  if ((val != rand) && (k < 32)) {
	    printf("\nRAM failure at %08x exp/act: %08x %08x\n",
		   (addr + k<<2) , rand, val);
	    printdataint(&buf[k], 8);
	    // 2nd read
	    hyperram_read_blocking(&g_hram_all[sm], addr<<2, buf, 32);
	    printdataint(&buf[k], 8);
	    ret_val--;
	    err_cnt++;
	    if (err_cnt > 3) return ret_val;
	  }
	}
      }
    }
  } else if (test_sel == 4) {
    // Speed test: does a set of streaming reads/writes, with perf report
#if 0
    if (BUF_SIZE != 1024/4) {
      printf("BUF_SIZE is not 1KB\n");
      return -1;
    }
#endif

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
	hyperram_write_blocking(&g_hram_all[sm], addr, &buf[j], 32);
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
	hyperram_read_blocking(&g_hram_all[sm], addr, &rbuf[j], 32);
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

void flip_cursor() {
  for (int i = 0; i < 16; i++) {
  //for (int i = 0; i < 14; i++) {
    cursor_wr_buf_ptr[i] = ~cursor_rd_buf_ptr[i];
  }
}

int main() {
  int clkdiv;
  int pass = 0;

#if 0
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
#endif

  // Setup clks
  hyperram_clk_init();

  // Setup PSRAM
  hyperram_ram_init();

  //	setup_default_uart();
  stdio_init_all();
  sleep_ms(3000);

#if 0
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

  // Setup RAM
  pass = hyperram_init();
  if (pass == 0) {
    printf("Hyperram initialized\n");
  } else {
    printf("FAILED: Did not initialize Hyperram\n");
  }
#endif
  
#if 0
  uint32_t cfg_reg;
  cfg_reg = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_ID0);
  printf("id 0: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_ID1);
  printf("id 1: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_CFG0);
  printf("cfg 0: %08x\n", cfg_reg);
  cfg_reg = hyperram_cfg_read_blocking(&g_hram_all[0], HRAM_REG_CFG1);
  printf("cfg 1: %08x\n", cfg_reg);
#endif


#if 0
  // Setup mono frame buffer
  fb_mono_inst_t inst = {
    .pio = pio1,
    .sm_sync = 2,
    .sm_video = 3,
    .pio_fb = g_hram_all[2].pio,
    .sm_fb = g_hram_all[2].sm,
    .sync_base_pin = VGA_VSYNC_PIN,
    .vsync_offset = 0,
    .hsync_offset = VGA_HSYNC_PIN - VGA_VSYNC_PIN,
    .vpol = V_SYNC_POLARITY,
    .hpol = H_SYNC_POLARITY,
    .vga_green_pin = VGA_GREEN_PIN,
    .pix_clk = PIX_CLK,
    .hfp = H_FP,
    .hactive = H_ACTIVE,
    .hbp = H_BP,
    .hsync = H_SYNC,
    .htotal = H_TOTAL,
    .vfp = V_FP,
    .vactive = V_ACTIVE,
    .vbp = V_BP,
    .vsync = V_SYNC,
    .vtotal = V_TOTAL
  };


#endif

  uint32_t fb_stat = fb_mono_init(PREFERRED_VID_MODE);
  //uint32_t fb_stat = fb_mono_init(0);
  //uint32_t fb_stat = fb_mono_init(0);
  //fb_mono_irq_en(_inst.vactive, 1);
  
  //uint32_t fb_stat = fb_mono_init(-1);

  if ((uint32_t)fb_stat == -1) {
    printf("PSRAM init only\n");
  } else {
    printf("Using %d x %d video format\n", _inst.hactive, _inst.vactive);
  }

  

  uint32_t fb_buf[1024];

  printf("after init\n");

#if 0
  for (int j = 0; j < _inst.vactive; j++) {
     draw_hline(0, j, _inst.hactive, 0);
  }
#endif

  //hyperram_write_blocking(&g_hram_all[0], 0, &fb_buf[0], 1);

#if 0
  //  for (int j = 0; j < _inst.vactive; j++) {
  for (int j = 0; j < 1; j++) {
#if 0
    for (int i = 0; i < _inst.hactive/32; i++) {
      fb_buf[i] = j & 1 ? 0xaaaaaaaaa : 0x55555555;
      //fb_buf[i] = (i & 0x3) == 0 ? -1 : 0;
    }
#endif
    uint32_t addr = j * _inst.hactive/8;
    //hyperram_write_blocking(&g_hram_all[0], addr, fb_buf, _inst.hactive/32);
    //hyperram_write_blocking(&g_hram_all[0], 0, &fb_buf[0], 1);
  }

  printf("after fb clear\n");
#endif

  pass = 0;
  uint32_t start;
  uint32_t start_per;
  uint32_t time_taken;
  uint32_t y = 0;
  uint32_t x = 0;

  //fb_mono_set_cursor_pos(x, y++);
  //while (1) {}

  //fb_mono_irq_en(0, 1);

  // Report memory speed
  memtest(pass, 4, 0);

  while (1) {
    flip_cursor();
    start = time_us_32();
#if 1
    printf("Pass: %4d ", pass);
    if (memtest(pass, 0, 0)) break;
    time_taken = time_us_32() - start;
    printf("\nFull exe time: %f\n", (float)time_taken/(float)1000000.0);
    printf("Short      ", pass);
    start_per = time_us_32();
    if (memtest(pass, 1, 0)) break;
    time_taken = time_us_32() - start_per;
    printf("\nShort exe time: %f\n", (float)time_taken/(float)1000000.0);
    // Needs debugging with smaller BUF_SIZE param
    //printf("Random     ", pass);
    //if (memtest(pass, 2, 0)) break;
    printf("Frame      ", pass);
    if (memtest(pass, 3, 0)) break;
#else
    printf("Pass: %4d ", pass);
    if (memtest(pass, 3, 0)) break;
#endif
    printf("\n");
    time_taken = time_us_32() - start;
    printf("Total execution time: %f\n", (float)time_taken/(float)1000000.0);
    pass++;
  }
}

