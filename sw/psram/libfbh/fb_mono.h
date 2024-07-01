#ifndef _FB_MONO_H
#define _FB_MONO_H

#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
// For setting 1.8v threshold
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

#include "hyperram.h"

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

typedef struct {
  uint32_t sysclk;
  uint32_t words_per_scanline;
  PIO pio_vid; // Video controller pio
  uint32_t sm_sync;
  uint32_t sm_video;
  PIO pio_mem;     // PSRAM PIO
  uint32_t sm_fb; // PSRAM frame buffer SM for video refresh
  uint32_t sm_proc; // PSRAM frame buffer SM for processor access
  uint32_t prog_offset_sync;
  uint32_t prog_offset_video;
  uint32_t sync_base_pin;
  uint32_t vsync_offset;  // Number of pins from sync base pin
  uint32_t hsync_offset;  // Number of pins from sync base pin
  uint32_t vga_green_pin;
  uint32_t pix_clk;
  uint32_t htotal;
  uint32_t hactive;
  uint32_t hfp;
  uint32_t hsync;
  uint32_t hbp;
  uint32_t hpol;
  uint32_t vtotal;
  uint32_t vactive;
  uint32_t vfp;
  uint32_t vsync;
  uint32_t vbp;
  uint32_t vpol;

} fb_mono_inst_t;

extern fb_mono_inst_t _inst;
extern void (*fb_mono_cb_addr)();

extern uint32_t *cursor_rd_buf_ptr;
extern uint32_t *cursor_wr_buf_ptr;

extern uint32_t *scan_tmp_ptr;

extern uint32_t cursor_planeA[16];
extern uint32_t cursor_planeB[16];

extern hyperram_cmd_t ps_cursor_cmd_buf[16];


void fb_mono_set_overlay_color(uint32_t entry, uint32_t color);

void fb_mono_set_cursor_pos(int32_t x, int32_t y);

uint32_t fb_mono_init(uint32_t vid_mode);

void fb_mono_irq_en(uint32_t line, uint32_t enable);

void fb_mono_sync_wait(uint32_t line);

void fb_mono_set_fb_start(uint32_t start_addr);

void fb_mono_irq_install(void);

void fb_mono_irq_remove(void);

uint32_t get_fb(uint32_t x, uint32_t y);

void set_fb(uint32_t x, uint32_t y, uint32_t pixels);

void draw_hline(uint32_t x, uint32_t y, uint32_t size, uint32_t color);

void draw_line (uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
		uint32_t color);

void draw_box(uint32_t x, uint32_t y, uint32_t size, uint32_t color);

void put_pix(uint32_t x, uint32_t y, uint32_t color);

//#define FB_PACKED
#define PREFERRED_VID_MODE 3
#define DECW_VID_MODE 6
#endif
