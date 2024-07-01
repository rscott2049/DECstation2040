#ifndef _FB_MONO_H
#define _FB_MONO_H

#include "hardware/pio.h"

typedef struct {
  PIO pio;
  uint32_t sm_sync;
  uint32_t sm_video;
  PIO pio_fb;     // PSRAM PIO
  uint32_t sm_fb; // PSRAM frame buffer SM
  uint32_t prog_offset_sync;
  uint32_t prog_offset_video;
  uint32_t sync_base_pin;
  uint32_t vsync_offset;  // Number of pins from sync base pin
  uint32_t hsync_offset;  // Number of pins from sync base pin
  uint32_t vsync_assert_polarity;
  uint32_t hsync_assert_polarity;
  uint32_t vga_green_pin;
  uint32_t pix_clk;
  uint32_t hfp;
  uint32_t hactive;
  uint32_t hbp;
  uint32_t hsync;
  uint32_t htotal;
  uint32_t vfp;
  uint32_t vactive;
  uint32_t vbp;
  uint32_t vsync;
  uint32_t vtotal;
} fb_mono_inst_t;

uint32_t *fb_mono_init(fb_mono_inst_t *inst);

void fb_mono_irq_en(uint32_t line, uint32_t enable);

void fb_mono_sync_wait(uint32_t line);

//void fb_mono_calib(fb_mono_inst_t *inst);

// Output a scan line to screen
// Returns next scan line index
uint32_t fb_mono_scanline(const fb_mono_inst_t *inst, uint32_t curr_line, uint32_t *pixels);

#endif
