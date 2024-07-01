#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "hyperram.h"
#include "fb_mono.h"

// For setting 1.8v threshold
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

// VGA pins
unsigned int VGA_VSYNC_PIN = 13;
unsigned int VGA_HSYNC_PIN = 14;
unsigned int VGA_GREEN_PIN = 16;

// Select one video format from below
//#define USE_1024x768
#define USE_1024x768_70
//#define USE_640x480

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

void put_pix(uint32_t *fb_addr, uint32_t x, uint32_t y, uint32_t color) {
  uint32_t pixels;

  // Position pixel within 32 bit word
  uint32_t pix_mask = 1 << (x & 0x1f);

  // Calculate pixel address
  uint32_t pix_addr = (y << 5) + (x >> 5);

  // Do RMW to set/clear pixel
  hyperram_read_blocking(&g_hram_all[0], pix_addr<<2, &pixels, 1);
  pixels = (pixels & ~pix_mask) | (color << (x & 0x1f));
  hyperram_write_blocking(&g_hram_all[0], pix_addr<<2, &pixels, 1);

}


void draw_box(uint32_t *fb_addr, uint32_t x, uint32_t y, uint32_t size, uint32_t color) {
  uint32_t i, j;
  uint32_t next_x;

  for (i = 0; i < size; i++) {
    next_x = x;
    for (j = 0; j < size; j++) {
      put_pix(fb_addr, next_x++, y, color);
    }
    y++;
  }
}

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


int main() {
  int clkdiv;
  int pass;

  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);
  pass = set_sys_clock_khz(300 * 1000, true);

  //	setup_default_uart();
  stdio_init_all();
  sleep_ms(3000);

  if (pass) {
    printf("Set sysclk to 300 MHz\n");
  } else {
    printf("FAILED: Did not set sysclk to 300 MHz\n");
  }


  // Set 1.8v threshold for I/O pads
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;

  printf("pad bank0 voltage select addr/data: %08x %08x\n", (uint32_t)addr,
	 (uint32_t)*addr);



  // Setup RAM
  pass = hyperram_init();
  if (pass == 0) {
    printf("Hyperram initialized\n");
  } else {
    printf("FAILED: Did not initialize Hyperram\n");
  }

  // Setup mono frame buffer
  fb_mono_inst_t inst = {
    .pio = pio1,
    .sm_sync = 2,
    .sm_video = 3,
    .pio_fb = g_hram_all[1].pio,
    .sm_fb = g_hram_all[1].sm,
    .sync_base_pin = VGA_VSYNC_PIN,
    .vsync_offset = 0,
    .hsync_offset = VGA_HSYNC_PIN - VGA_VSYNC_PIN,
    .vsync_assert_polarity = V_SYNC_POLARITY,
    .hsync_assert_polarity = H_SYNC_POLARITY,
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

  printf("Using %d x %d video format\n", inst.hactive, inst.vactive);

  uint32_t *fb_addr = fb_mono_init(&inst);
  
  printf("Initialzing frame buffer\n");

#if 0
  printf("FB addr: %08x\n", (uint32_t)fb_addr);
  for (int j = 0; j < inst.vactive; j++) {
    for (int i = 0; i < inst.hactive/32; i++) {
      uint32_t pix_addr = (j * inst.hactive/32) + i;
      fb_addr[pix_addr] = (j & 1) ? 0xaaaaaaaa : 0x55555555;
      //fb_addr[pix_addr] = 0;
    }
  }
#endif

  uint32_t line_buf[32];
  uint32_t pix_addr;

  for (int j = 0; j < inst.vactive; j++) {
    for (int i = 0; i < inst.hactive/32; i++) {
      line_buf[i] =  (j & 1) ? 0xaaaaaaaa : 0x55555555;
      //line_buf[i] =  ((i % 4) == 0) ? -1 : 0;
    }
    pix_addr = (j * inst.hactive/32);
    hyperram_write_blocking(&g_hram_all[0], pix_addr<<2, line_buf, 32);
  }

  while (0) {
    int j = 0;
    for (int i = 0; i < inst.hactive/32; i++) {
      line_buf[i] = -1;
    }
    pix_addr = (j * inst.hactive/8);
    hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);
    pix_addr = inst.vactive * inst.hactive/8;
    hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);

    for (int i = 0; i < inst.hactive/32; i++) {
      line_buf[i] = 0;
      if (i == 0) line_buf[i] = 0x1;
      if (i == 31) line_buf[i] = 0x80000000;
    }
    for (j = 1; j < inst.vactive - 2; j++) {
      pix_addr = (j * inst.hactive/8);
      hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);
    }
    draw_box(fb_addr, 0, 0, 10, 1);
    draw_box(fb_addr, (1024/4)+2, 0, 10, 1);
    sleep_ms(1000);
  }

  int32_t x, old_x;
  int32_t y, old_y;
  int32_t x_inc, y_inc;
  uint32_t size = 10;

  x_inc = 3;
  y_inc = 3;
  x = 0;
  y = 0;

#if 0
  for (int i = 0; i < 32; i++) {
    draw_box(fb_addr, x+(i*size), y+(i*size), size, 1);
    fb_mono_sync_wait(inst.vactive);
  }

  for (int i = 0; i < 16; i++) {
    draw_box(fb_addr, x+(i*size), y+(i*size), size, 0);
    for (int j = 0; j < 70; j++) {
      fb_mono_sync_wait(inst.vactive);
    }
  }
#endif
  printf("after draw\n");

  //while(1) {}

  while (1) {
    draw_box(fb_addr, x, y, size, 1);
    fb_mono_sync_wait(inst.vactive);
    //sleep_ms(10);
    draw_box(fb_addr, x, y, size, 0);
    x = x + x_inc;
    y = y + y_inc;

    if (x < 0) {
      x_inc = -x_inc;
      x = x + x_inc;
    }    

    if (x >= (inst.hactive - size)) {
      x_inc = -x_inc;
      x = inst.hactive - size - 1;
    }

    if (y < 0) {
      y_inc = -y_inc;
      y = y + y_inc;
    }    

    if (y >= (inst.vactive - size)) {
      y_inc = -y_inc;
      y = inst.vactive - size - 1;
    }
  }


  // 


  uint32_t curr_line = 0;
  uint32_t next_line = 0;
  uint32_t pixels[1024];

  for (int i = 0; i < 32; i++) {
    pixels[i] = 0; //0xaaaaaaaa;
  }
  pixels[0] = -1;

  while(1) {
#if 0
    for (int i = 0; i < 32; i++) {
      if ((curr_line == 10) || (curr_line == 20)) pixels[i] = -1;
      else 
	if (curr_line == 102) {
	  if (i == 4) pixels[] = -1;
	}
      else pixels[i] = 0;


      //pixels[i] = ((curr_line % 32) < i) ? -1 : 0;
      //pixels[i] = 0;
    }
    //pixels[0] = -1;
    //pixels[31] = -1;
    //pixels[16] = -1;
    //    pixels[8] = -1;
    //pixels[4] = -1;

#endif
#if 0
    //    pixels[curr_line/32] = 1 << (curr_line % 32);
    for (int i = 0; i < 32; i++) {
      pixels[i] = (curr_line == (inst.vfp + 1)) ? -1 : 0;
      pixels[i] |= (i == 0) ? 0x1 : 0;
      pixels[i] |= (i == ((inst.hactive/32) - 1)) ? 1 << (inst.hactive%32) : 0;
      pixels[i] = (curr_line == (inst.vfp + inst.vactive - 1)) ? -1 : 0;
    }
#endif

#if 0
    for (int i = 0; i < 32; i++) {
      pixels[i] = 0;
      //pixels[0] = -1;

      if (curr_line == inst.vbp) pixels[i] = -1;
      else if (curr_line == (inst.vbp + inst.vactive - 1)) pixels[i] = -1;
      else {
	pixels[0] = 1;
	pixels[(inst.hactive/32)-1] = 0x80000000;
	pixels[(curr_line-inst.vbp)/32] |= (1 << (curr_line-inst.vbp)%32);
      }
    }
#endif

#if 0
    next_line = fb_mono_scanline(&inst, curr_line, pixels);
    curr_line = next_line;
#endif
  }


}

