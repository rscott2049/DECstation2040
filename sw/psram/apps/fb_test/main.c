#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hyperram.h"
#include "hardware/dma.h"

#if 0
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"



// For setting 1.8v threshold
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

// VGA pins
unsigned int VGA_VSYNC_PIN = 13;
unsigned int VGA_HSYNC_PIN = 14;
unsigned int VGA_GREEN_PIN = 16;

// Select one video format from below
//#define USE_1024x768
//#define USE_1024x768_70
//#define USE_640x480
#endif

#include "fb_mono.h"

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

// Video horizontal events dma buffer
typedef struct {
	uint32_t bp;
	uint32_t active;
        uint32_t fp;
	uint32_t sync;
} h_events_t;


//fb_mono_inst_t inst;

#if 0
void put_pix(uint32_t *fb_addr, uint32_t x, uint32_t y, uint32_t color) {
  uint32_t pixels;

  // Position pixel within 32 bit word
  uint32_t pix_mask = 1 << (x & 0x1f);

  // Calculate pixel address
#ifdef PACKED_FB
  uint32_t pix_addr = (y * _inst.hactive/32) + (x >> 5);
#else
  uint32_t pix_addr = (y << 5) + (x >> 5);
#endif

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

/*
 * Bresenham algorithm
 */

#define abs(a) (a) > 0 ? (a) : -(a)
void draw_line (uint32_t *fb_addr,
		int x0, int y0, int x1, int y1,
		uint32_t color) {

  int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
  int err = dx + dy, e2; /* error value e_xy */
 
  for (;;){  /* loop */
    put_pix(fb_addr, x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
    if (e2 <= dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
  }
}

void draw_hline(uint32_t *fb_addr, uint32_t x, uint32_t y, uint32_t size, uint32_t color) {
  uint32_t i, j;
  uint32_t next_x = x;

  for (i = 0; i < size; i++) {
    put_pix(fb_addr, next_x++, y, color);
  }
}
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

// Horizontal timing events generator - formats commmands
// for the video sm
// Events are (in order):
// 0) back porch (after sync deassertion)
// 1) active (display pixels)
// 2) front porch (before sync assertion
// 3) sync
//
// Format of video PIO input:
// <15:0> - parameter or number of instructions to wait (-3)
// <31:16> - PIO SM instruction to execute
//
// When parameter is wait time, this is in units of (pixels * 2) - 2.
// When parameter is pixel count, this is number of pixels to transmit
//

// PIO NOP instruction (mov Y, Y)
#define NOP_INST 0xA042

// PIO SET instruction for sync pins (the last 5 bits are the pin values)
#define SET_INST 0xE000

// PIO JMP instruction to start pixel output (vidout routine starts at loc 0)
#define JMP_INST 0x0000

void test_h_line(h_events_t *t, const fb_mono_inst_t *inst, uint32_t active, uint32_t vs) {
  uint32_t active_cmd;
  uint32_t vsync, hsync_assert, hsync_deassert;
  uint32_t sync_lo_inst;
  uint32_t sync_hi_inst;

  // Determine actual vsync, hsync assertion levels for commands:
  // Input vsync is always asserted high
  vsync = ((~inst->vpol ^ vs) & 0x01) << inst->vsync_offset;
  hsync_assert = (inst->hpol & 0x01) << inst->hsync_offset;
  hsync_deassert = (~inst->hpol & 0x01) << inst->hsync_offset;
  
  // Generate PIO instructions for horizontal sync events
  sync_lo_inst = SET_INST | hsync_deassert | vsync;
  sync_hi_inst = SET_INST | hsync_assert   | vsync;

  // Setup back porch (i.e. event just after hsync assertion)
  // Wait time is in units of pixels * 2, with 2 pixel overhead
  t->bp = (sync_lo_inst << 16) | ((inst->hbp * 2) - 2);

  // Setup send pixel command or wait until active time is done
  if (active) {
    // Parameter in this case is number of active pixels - 1
    t->active = (JMP_INST << 16) | (inst->hactive - 1);
  } else {
    t->fp = (NOP_INST << 16) | ((inst->hactive * 2) - 2);
  }

  // Setup front porch command
  t->fp = (sync_lo_inst << 16) | ((inst->hfp * 2) - 2);

  // Setup sync command
  t->sync = (sync_hi_inst << 16) | ((inst->hsync * 2) - 2);

}

uint32_t fb_mono_scanline(const fb_mono_inst_t *inst, uint32_t curr_line, uint32_t *pixels) {
  uint32_t active_line_start;
  uint32_t active_line_end;
  uint32_t active;
  uint32_t sync_line_start;
  uint32_t sync_line_end;
  uint32_t sync;
  uint32_t next_line;
  uint32_t pix_addr;
  h_events_t t;

  // Setup scan line
  // Determine if we're in active portion of the screen or not
  active_line_start = inst->vbp;
  active_line_end = inst->vbp + inst->vactive;
  active = (curr_line >= active_line_start) && (curr_line < active_line_end);
  // Determine if we're in vertical sync interval or not
  sync_line_start = active_line_end + inst->vfp;
  sync_line_end = sync_line_start + inst->vsync;
  sync = (curr_line >= sync_line_start) && (curr_line < sync_line_end);

  //if ((first_act == 0) && (active)) first_act = curr_line;

  test_h_line(&t, inst, active, sync);

#if 0
  printf("fp: %08x\n", t.fp);
  printf("at: %08x\n", t.active);
  printf("bp: %08x\n", t.bp);
  printf("sy: %08x\n", t.sync);
#endif    

  // Send commands to sync/blank SM
  pio_sm_put_blocking(inst->pio_vid, inst->sm_video, t.bp);
  pio_sm_put_blocking(inst->pio_vid, inst->sm_video, t.active);
  pio_sm_put_blocking(inst->pio_vid, inst->sm_video, t.fp);
  pio_sm_put_blocking(inst->pio_vid, inst->sm_video, t.sync);

#if 0
  if ((frame == 10) && (curr_line == 10)) {
    printf("Initialzing frame buffer\n");
    printf("FB addr: %08x\n", (uint32_t)&dma_buf[0]);
    for (int j = 0; j < inst->vactive; j++) {
      for (int i = 0; i < inst->hactive/32; i++) {
	pix_addr = (j * inst->hactive/32) + i;
	//dma_buf[pix_addr] = (i == 0) ? -1 : 0;
	dma_buf[pix_addr] = (j & 1) ? 0xaaaaaaaa : 0x55555555;
      }
    }
  }
#endif

  // Copy pixels to frame buffer
#if 0
  if ((frame == (5 * 60)) && (active)) {
    pix_addr = (curr_line - inst->vbp) * inst->hactive/32;
    for (int i = 0; i < inst->hactive/32; i++) {
      dma_buf[pix_addr + i] = pixels[i];
    }      
  }
#endif

  // Send pixels to video SM
#if 0
  if (active) {
    for (int i = 0; i < inst->hactive/32; i++) {
      pio_sm_put_blocking(inst->pio_vid, inst->sm_video, pixels[i]);
    }
  }
#endif

#if 0
  // Check to see if pixels can be pushed to video SM
  while (pio_sm_is_tx_fifo_full(inst->pio_vid, inst->sm_video) == false) {
    pio_sm_put(inst->pio_vid, inst->sm_video, pixels[curr_pix_count++]);
    // Advance to next pixel group
    if (report > 0) {
      printf("first/act/frame/line/count: %d %d %d %d %d\n", first_act, active, frame, curr_line, curr_pix_count);
      report--;
    }
    if (curr_pix_count == inst->hactive/32) curr_pix_count = 0;
  }
#endif
  
#if 0
  // Compute next scan line
  next_line = curr_line + 1;
  if (next_line >= sync_line_end) {
    curr_pix_count = 0;
    next_line = 0;
    frame++;
    if ((report == 0) && (frame < 2)) report = 16;

  }
#endif
  return next_line;
  
}

uint32_t dbg_count = 10;

void fb_debug() {
  if (dbg_count > 0) {
    dbg_count--;
    printf("dbg: %d\n", dbg_count);
  }

}

int main() {
  int clkdiv;
  int pass;

  // Setup clks
  _inst.sysclk = hyperram_clk_init();

  // Setup PSRAM
  hyperram_ram_init();

  //	setup_default_uart();
  stdio_init_all();
  sleep_ms(3000);

  uint32_t fb_addr;
  //fb_addr = fb_mono_init(-1);
  //fb_addr = fb_mono_init(0);
  //fb_addr = fb_mono_init(1);
  //fb_addr = fb_mono_init(2);
  //fb_addr = fb_mono_init(PREFERRED_VID_MODE);
  //fb_addr = fb_mono_init(4);
  fb_addr = fb_mono_init(5);
  //fb_addr = fb_mono_init(6);
  //fb_addr = fb_mono_init(DECW_VID_MODE);
  
  if ((uint32_t)fb_addr == -1) {
    printf("PSRAM init only\n");
  } else {
    printf("Using %d x %d video format\n", _inst.hactive, _inst.vactive);
  }

  //fb_mono_cb_addr = &fb_debug;

  while(0) {
    draw_hline(0, 0, _inst.hactive, 0);
    draw_hline(0, 1, _inst.hactive, 1);
    draw_hline(0, _inst.vactive - 1, _inst.hactive, 1);

    for (int i = 0; i < (_inst.hactive - 4); i++) {
      draw_hline(i, 0, 4, 1);
      draw_hline(i, 1, 4, 0);
      draw_hline(i, _inst.vactive - 1, 4, 0);

      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
    }
  }

  printf("Initialzing frame buffer\n");
#if 0
  for (int i = 0; i < _inst.vactive; i++) {
    if (i == (_inst.vactive - 2))  draw_hline(0, i, _inst.hactive, 0);
    else if (i == (_inst.vactive - 1))  draw_hline(0, i, _inst.hactive, 1);
    else if (i == 0)  draw_hline(0, i, _inst.hactive, 1);
    else draw_hline(0, i, _inst.hactive, 0);
  }
  //  while(1) {}
#endif
  

  uint32_t buffer_0[32], buffer_1[32];
  uint32_t buffer_2[32], buffer_3[32];
  for (int i = 0; i < 32; i++) {
#if 1
    buffer_0[i] = 0x55555555;
    buffer_1[i] = 0xaaaaaaaa;
    buffer_2[i] = 0x00ffff00;
    //    buffer_3[i] = 0xffff0000;
    buffer_3[i] = 0;
#endif
  }

  for (int i = 0; i < _inst.vactive; i++) {
#if 1
    if ((i & 1) == 1) {
      hyperram_write_blocking(&g_hram_all[0], i * 64 * 4, &buffer_0[0], 32);
    } else {
      hyperram_write_blocking(&g_hram_all[0], i * 64 * 4, &buffer_1[0], 32);
    }
#else
    switch (i & 7) {
    case(0): 
      hyperram_write_blocking(&g_hram_all[0], i * 64 * 4, &buffer_0[0], 32);
      break;
#if 0
   case(1): 
      hyperram_write_blocking(&g_hram_all[0], i * 64 * 4, &buffer_1[0], 32);
      break;
#endif
    default: 
      hyperram_write_blocking(&g_hram_all[0], i * 64 * 4, &buffer_3[0], 32);
      break;
    }

#endif
  }

  for (int i = 0; i < 16; i++) {
    cursor_wr_buf_ptr[i] = i & 0x1 ? 0x55555555 : 0xaaaaaaaa;
  }


#if 1
  for (int i = 0; i < 16; i++) {
    if (i == 0) cursor_planeA[i] = 0xffff;
    else if (i == 15) cursor_planeA[i] = 0xffff;
    else cursor_planeA[i] = 0x8001;
    if (i == 0) cursor_planeB[i] = 0x0;
    else if (i == 1) cursor_planeB[i] = 0xffff;
    else if (i == 14) cursor_planeB[i] = 0xffff;
    else if (i == 15) cursor_planeB[i] = 0x0;
    else cursor_planeB[i] = 0x4002;
  }
#endif
  fb_mono_set_cursor_pos(0, _inst.vactive);


  uint32_t switch_wait = 2;

  // Setup second core 
  //multicore_reset_core1();
  //multicore_fifo_drain();
  uint32_t core_in_use = 0;

  while(0) {
    for (int i = 0; i < _inst.vactive; i++) {
    //for (int i = 0; i < 10; i++) {
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
#if 1
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
      fb_mono_sync_wait(_inst.vactive);
#endif
      //fb_mono_set_cursor_pos(_inst.vactive, i);
      //fb_mono_set_cursor_pos(100, _inst.vactive - 8 + (i & 0xfffff3));
      //fb_mono_set_cursor_pos(100, _inst.vactive - 10 - i);
      //fb_mono_set_cursor_pos(_inst.vactive - i, i);
      fb_mono_set_cursor_pos(i, i);
      //fb_mono_set_cursor_pos(i, i&0xfffffe);
      //fb_mono_set_cursor_pos(i & 0xfffff7, 100);
      //fb_mono_set_cursor_pos(i, 100);
      //sleep_ms(500);
      //fb_mono_sync_wait(_inst.vactive);
      //fb_mono_sync_wait(_inst.vactive);
      //fb_mono_set_cursor_pos(128+8, 128);
      //sleep_ms(500);
      //fb_mono_sync_wait(_inst.vactive);
      //fb_mono_sync_wait(_inst.vactive);
      //fb_mono_sync_wait(_inst.vactive);
      //fb_mono_set_cursor_pos(129, 128+18);
    }
#if 0
    switch_wait--;
    if (switch_wait == 0) {
      switch_wait = -1;
      if (core_in_use == 0) {
	fb_mono_irq_remove();
	multicore_reset_core1();
	multicore_fifo_drain();
	multicore_launch_core1(fb_mono_irq_install);
	core_in_use = 1;
      } else {
	multicore_launch_core1(fb_mono_irq_remove);
	//multicore_reset_core1();
	//multicore_fifo_drain();
	fb_mono_irq_install();
	core_in_use = 0;
      }
    }
#endif
  }


#if 0
  uint32_t buffer[16];
  buffer[0] = 0x5555aaaa;
  hyperram_write_blocking(&g_hram_all[0],0x10, &buffer[0], 8);
  hyperram_read_blocking(&g_hram_all[0], 0x10, &buffer[0], 8);
  printf("%08x %08x %08x %08x\n", buffer[0], buffer[1], buffer[2], buffer[3]);
  buffer[0] = ~buffer[0];
  hyperram_write_blocking(&g_hram_all[0],0x10, &buffer[0], 8);
  hyperram_read_blocking(&g_hram_all[0], 0x10, &buffer[0], 8);
  printf("%08x %08x %08x %08x\n", buffer[0], buffer[1], buffer[2], buffer[3]);

  while(1) {}

  hyperram_cmd_t cmd;
  
  _hyperram_cmd_init(&cmd, &g_hram_all[_inst.sm_fb], HRAM_CMD_READ, 0x80, 32);

  printf("cmd: %08x %08x %08x\n", cmd.cmd0, cmd.cmd1, cmd.cmd2);


  cmd.cmd0 = 0xa0ff02;
  cmd.cmd1 = 0x800;
  cmd.cmd2 = 0x1200003f;

  printf("cmd: %08x %08x %08x\n", cmd.cmd0, cmd.cmd1, cmd.cmd2);

  for (int i = 0; i < 5; i++) {
    //if (i > 0) cmd.cmd2 = 0x1200003;

    pio_sm_put_blocking(_inst.pio_fb, _inst.sm_fb, cmd.cmd0);
    pio_sm_put_blocking(_inst.pio_fb, _inst.sm_fb, cmd.cmd1);
    printf("put 1\n");
    pio_sm_put_blocking(_inst.pio_fb, _inst.sm_fb, cmd.cmd2);
    printf("put 2\n");
#if 0
    sleep_ms(1);
    printf("rx empty: %d %d %d %d\n",
	   pio_sm_is_rx_fifo_empty(_inst.pio_fb, 0),
	   pio_sm_is_rx_fifo_empty(_inst.pio_fb, 1),
	   pio_sm_is_rx_fifo_empty(_inst.pio_fb, 2),
	   pio_sm_is_rx_fifo_empty(_inst.pio_fb, 3));
#endif
    int dma_count = 0;
    do {
      printf("%08x ", pio_sm_get_blocking(_inst.pio_fb, _inst.sm_fb));
      dma_count++;
    } while(pio_sm_is_rx_fifo_empty(_inst.pio_fb, _inst.sm_fb) == false);
    printf("\n%d count: %d\n", i, dma_count);
  }


  while(1) {}
#endif

  uint32_t dly = 0;

  uint32_t fp = (NOP_INST << 16) | 1;
  uint32_t on  = ((SET_INST | 0x03) << 16) | 3;
  uint32_t bp = ((SET_INST | 0x00) << 16) | 3;

  while (0) {
    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, on);
    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, bp);
  }


  uint32_t pixels[32];

  while (0) {
    for (int i = 0; i < _inst.vtotal; i++) {
      fb_mono_scanline(&_inst, i, pixels);
    }
  }

#if 0
  uint32_t buffer[16];
  hyperram_read_blocking(&g_hram_all[0], 0x10, &buffer[0], 8);
  buffer[0] = ~buffer[0];
  hyperram_write_blocking(&g_hram_all[0],0x10, &buffer[0], 8);

  while (1) {}
#endif

  int x = 300;
  int y = 2;
  //draw_box(x, y, 1, 0);
  put_pix(x, y, 1);

  while (0) {}

  uint32_t line_buf[32];
  uint32_t pix_addr;

#if 0
  printf("Before initialization\n");
  for (int j = 0; j < _inst.vactive; j++) {
    for (int i = 0; i < _inst.hactive/32; i++) {
      line_buf[i] =  (j & 1) ? 0xaaaaaaaa : 0x55555555;
      //line_buf[i] =  ((i % 4) == 0) ? -1 : 0;
      //line_buf[i] = ((j == 1) && ((i % 4) == 0)) ? -1 : 0;
    }
    pix_addr = (j * _inst.hactive/32);
    hyperram_write_blocking(&g_hram_all[0], pix_addr<<2, line_buf, 32);
  }
#endif

  //line_buf[0] = -1;
  //hyperram_write_blocking(&g_hram_all[0], 0, line_buf, 32);
  

  //while(1) {};

  while (0) {
    int j = 0;
    for (int i = 0; i < _inst.hactive/32; i++) {
      line_buf[i] = -1;
    }
    pix_addr = (j * _inst.hactive/8);
    hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);
    pix_addr = _inst.vactive * _inst.hactive/8;
    hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);

    for (int i = 0; i < _inst.hactive/32; i++) {
      line_buf[i] = 0;
      if (i == 0) line_buf[i] = 0x1;
      if (i == 31) line_buf[i] = 0x80000000;
    }
    for (j = 1; j < _inst.vactive - 2; j++) {
      pix_addr = (j * _inst.hactive/8);
      hyperram_write_blocking(&g_hram_all[0], pix_addr, line_buf, 32);
    }
    draw_box(0, 0, 10, 1);
    draw_box((1024/4)+2, 0, 10, 1);
    //sleep_ms(1000);
  }

  int32_t old_x;
  int32_t old_y;
  int32_t x_inc, y_inc;
  uint32_t size = 10;

  //x_inc = 2;
  x_inc = 1;
  y_inc = 3;
  x = 0;
  y = 0;

#if 0
  for (int i = 0; i < 32; i++) {
    draw_box(x+(i*size), y+(i*size), size, 1);
    fb_mono_sync_wait(_inst.vactive);
  }

  for (int i = 0; i < 16; i++) {
    draw_box(x+(i*size), y+(i*size), size, 0);
    for (int j = 0; j < 70; j++) {
      fb_mono_sync_wait(_inst.vactive);
    }
  }
#endif

  printf("after draw\n");
  
  // upper right
  draw_box(0, 0, size, 1);
  // upper left
  //  draw_box(_inst.hactive-size, 0, size, 1);
  draw_box(_inst.hactive-size, 0, 20, 1);

  // middle right
  draw_box(0, _inst.vactive/2-size, size, 1);

  // middle left
  draw_box(_inst.hactive-size, _inst.vactive/2-size, size, 1);
  
  // lower right
  draw_box(0, _inst.vactive-size, size, 1);
  // lower left
  draw_box(_inst.hactive-size, _inst.vactive-size, size, 1);


#if 1
  printf("before draw line\n");

  int x0, y0;
  int x1, y1;
#if 1
  x0 = 10;
  y0 = 10;
  x1 = 20;
  y1 = 15;

  draw_line(x0, y0, x1, y1, 1);

  x1 = 0;
  y1 = 0;
  x0 = 10;
  y0 = 5;

  draw_line(x0, y0, x1, y1, 1);

  x0 = 0;
  y0 = 0;
  x1 = _inst.hactive - 1;
  y1 = x1;

  draw_line(x0, y0, x1, y1, 1);
#endif

  while(0) {

  x0 = 10;
  y0 = 10;
  x1 = 20;
  y1 = 15;

  draw_line(x0, y0, x1, y1, 1);
  //sleep_ms(1000);
  }

  fb_mono_irq_en(0, 1);

  int color = 1;
  int done = 1;
  int printed = 0;
  extern uint32_t frame_count;

#if 0
  for (int i = 0; i < 16; i++) {
    uint32_t start_count = frame_count;
    sleep_ms(1000);
    printf("%d: frame count: %d\n", i, frame_count - start_count);
  }
#endif

#if 0
  for (int i = 0; i < 8; i++) {
    uint32_t start_count = frame_count;
    for (int i = 0; i < 60; i++) { fb_mono_sync_wait(0);fb_mono_sync_wait(0);}
    printf("%d: frame count: %d\n", i, frame_count - start_count);
  }
#endif

#if 0
  for (int i = 0; i < 16; i++) {
    printf("%d lo hi: %08x %08x\n", i,
	   cursor_rd_buf[i * 2], cursor_rd_buf[(i * 2) + 1]);
    //cursor_wr_buf[i * 2] = -1;
    //cursor_wr_buf[(i * 2) + 1] = -1;
  }
#endif

  uint32_t cursor_buf[16];
#if 0  
  for (int i = 0; i < 16; i++) {
    if (i == 0) cursor_buf[i] = 0xffff;
    else if (i == 15) cursor_buf[i] = 0xffff;
    else cursor_buf[i] = 0x8001;
    //    printf("cursor_wr_buf addr/data: %08x %08x\n",
    //(uint32_t)&cursor_wr_buf_ptr[i], cursor_wr_buf_ptr[i]);
  }
#endif

  fb_mono_set_overlay_color(1, 0);  // plane B active color
  fb_mono_set_overlay_color(2, 1);  // plane A active color
  fb_mono_set_overlay_color(3, 1);  // Both active

#if 0
  for (int i = 0; i < 16; i++) {
    cursor_planeA[i] = 0xffff;
    cursor_planeB[i] = 0xffff;
  }
#endif

#if 0
  fb_mono_set_cursor_pos(0, 1);
  for (int q = 0; q < 16; q++) {
    printf("%02d rd %08x %08x pix: %08x tmp: %08x\n", q,
	   (uint32_t)&cursor_rd_buf_ptr[q], cursor_rd_buf_ptr[q],
	   get_fb(0, 1 + q), scan_tmp_ptr[q]);
    
  }

  while(1) {}
#endif


#if 0  
  for (int i = 0; i < 16; i++) {
    cursor_rd_buf_ptr[i] = 0xffff << i;
    printf("cursor_rd_buf addr/data: %08x %08x\n",
	   (uint32_t)&cursor_rd_buf_ptr[i], cursor_rd_buf_ptr[i]);
  }
  sleep_ms(17 * 10);
  for (int i = 0; i < 16; i++) {
    printf("cursor_rd_buf addr/data: %08x %08x\n",
	   (uint32_t)&cursor_rd_buf_ptr[i], cursor_rd_buf_ptr[i]);
  }

#endif

  while(0) {
    for (int j = 0; j < _inst.vactive; j++) {
      fb_mono_sync_wait(_inst.vbp + 5);
      fb_mono_set_cursor_pos(2, j);
    }
  }

  while(0) {
    for (int j = 0; j < _inst.vactive; j++) {
      for (int i = 0; i < 8; i++) fb_mono_sync_wait(0);
      fb_mono_set_cursor_pos(j, j);
    }
  }

  while(0) {
    for (int j = 0; j < _inst.vactive; j++) {
      fb_mono_set_cursor_pos(j, j);
      for (int i = 0; i < 8; i++) fb_mono_sync_wait(0);
      for (int q = 0; q < 16; q++) {
	printf("%02d rd %08x %08x\n", q,
	       (uint32_t)&cursor_rd_buf_ptr[q], cursor_rd_buf_ptr[q]);
      }
    }
  }

  while(0) {
    for (int i = 0; i < 16; i++) fb_mono_sync_wait(0);
    fb_mono_set_cursor_pos(9, 0);
    for (int i = 0; i < 16; i++) fb_mono_sync_wait(0);
    fb_mono_set_cursor_pos(6, 0);
  }
  
  while(0) {
    for (int j = 0; j < _inst.hactive; j++) {
      for (int q = 0; q < 16; q++) {
	//cursor_buf[q] = ~cursor_rd_buf_ptr[q];
	//cursor_buf[q] = ~get_fb(j+q, j+q);
      }
      fb_mono_set_cursor_pos(j, 0);
      for (int i = 0; i < 8; i++) fb_mono_sync_wait(0);
    
    }
  }

  uint32_t under_buf[16];

  while(0) {
    for (int j = 0; j < _inst.vactive; j++) {
      for (int q = 0; q < 16; q++) {
	//under_buf[q] = ~get_fb(j, j+q);
      }
      fb_mono_set_cursor_pos(j, j);
      for (int i = 0; i < 8; i++) fb_mono_sync_wait(0);
    }
  }


  while(0) {
    for (int j = 0; j < _inst.vactive; j++) {
      for (int i = 0; i < _inst.hactive; i++) {
	fb_mono_set_cursor_pos(i, j);
      }
    }
  }


  while(0) {
    cursor_wr_buf_ptr[0] = ~cursor_rd_buf_ptr[0];
    cursor_wr_buf_ptr[1] = ~cursor_rd_buf_ptr[1];
  }

  //while (1) {}
  while (0) {
    fb_mono_sync_wait(_inst.vbp + 5);
    //fb_mono_sync_wait(_inst.vtotal/2);
#if 1
    for (int i = 0; i < 16; i++) {
      if ((frame_count & 0x3f) == 0) {
	cursor_wr_buf_ptr[i * 2] = cursor_rd_buf_ptr[i * 2];
	cursor_wr_buf_ptr[(i * 2) + 1] = cursor_rd_buf_ptr[(i * 2) + 1];
      }
      if ((frame_count & 0x3f) == 0x1f) {
	cursor_wr_buf_ptr[i * 2] = ~cursor_rd_buf_ptr[i * 2];
	cursor_wr_buf_ptr[(i * 2) + 1] = ~cursor_rd_buf_ptr[(i * 2) + 1];
      }
    }
#endif
  }

#if 0
  while (1) {
    fb_mono_sync_wait(_inst.vbp + 5);
    if ((frame_count & 0x1f) == 0) {
      for (int i = 0; i < 16; i++) {
	printf("%d lo hi: %08x %08x\n", i,
	       cursor_rd_buf[i * 2], cursor_rd_buf[(i * 2) + 1]);
	cursor_wr_buf[i * 2] = -1;
	cursor_wr_buf[(i * 2) + 1] = -1;
      }
    }
  }
#endif

  uint32_t cursor = 0;
  //while (done) {
  while (1) {
    fb_mono_set_cursor_pos(cursor, cursor);
    cursor += 10;
    if (cursor > (_inst.vactive + 15)) cursor = 0;
    color = !color;
    for (int i = 0; i < _inst.hactive; i++) {
      x0 = i;
      y0 = 0;
      x1 = (_inst.hactive - 1) - i;
      y1 = _inst.vactive - 1;
      color = !color;
      draw_line(x0, y0, x1, y1, color);
      }
    for (int i = 0; i < _inst.vactive; i++) {
      x0 = 0;
      y0 = i;
      x1 = _inst.hactive - 1;
      y1 = (_inst.vactive - 1) - i;
      color = !color;
      draw_line(x0, y0, x1, y1, color);
    }
    done--;
  }

  printf("After pattern draw\n");
#endif

  for (int i = 0; i < _inst.vactive; i++) {
    if ((i & 0xf) == 0) {
      draw_hline(0, i, _inst.hactive, 1);
    }
  }
  //draw_hline(0, _inst.vactive-2, _inst.hactive, 1);
  draw_hline(0, _inst.vactive-1, _inst.hactive, 1);

  extern uint32_t frame_count;
  uint32_t display_addr = 0;
  printf("Before draw_box\n");
  x = 0;
  y = 0;

  while (1) {
    draw_box(x, y, size, 1);
    draw_line(x, 0, x, _inst.vactive/2, 1);
    //fb_mono_sync_wait(_inst.vbp + 5);
    //fb_mono_sync_wait(_inst.vactive - 7);
    //printf("fc: %d\n", frame_count % 70);
    //if ((frame_count & 0x3f) == 1) printf("frame count: %d\n", frame_count);
    fb_mono_sync_wait(x % (_inst.vactive/2));
    //fb_mono_set_fb_start(display_addr);
    //display_addr += 1024/32;
    display_addr = (display_addr + 2048/8) % (_inst.vtotal * 2048/8);

    //if ((frame_count % 70) == 11) printf("frame count: %d\n", frame_count);
    //if ((frame_count & 0x3f) == 2) printf("frame count: %d\n", frame_count);
    //printf("FC mask %08x\n", (frame_count & 0x3f));
    //sleep_ms(14);
    draw_box(x, y, size, 0);
    draw_line(x, 0, x, _inst.vactive/2, 0);
    x = x + x_inc;

    if (x > (_inst.hactive - size)) {
      x = 0;
      if (y >= (_inst.vactive - 2 - size)) {
	draw_hline(0, 1, _inst.hactive/2, 1);
	y = 0;
      } else {
	//y = y + size;
	y = _inst.vactive - size;
	draw_hline(_inst.hactive/2 + 10, _inst.vactive - 1,
		   (_inst.hactive/2 - 11), 1);
	draw_hline(_inst.hactive/4 + 20, _inst.vactive - 2,
		   _inst.hactive/2 - 21, 1);
      }
    }
  }


  while (0) {
    draw_box(x, y, size, 1);
    fb_mono_sync_wait(_inst.vactive);
    fb_mono_set_fb_start(y * _inst.hactive);
    draw_box(x, y, size, 0);
    x = x + x_inc;
    y = y + y_inc;

    if (x < 0) {
      x_inc = -x_inc;
      x = x + x_inc;
    }    

    if (x >= (_inst.hactive - size)) {
      x_inc = -x_inc;
      x = _inst.hactive - size - 1;
    }

    if (y < 0) {
      y_inc = -y_inc;
      y = y + y_inc;
    }    

    if (y >= (_inst.vactive - size)) {
      y_inc = -y_inc;
      y = _inst.vactive - size - 1;
    }
  }

}

