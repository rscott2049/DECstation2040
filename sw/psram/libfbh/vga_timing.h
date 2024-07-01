#ifndef _VGA_TIMING_H
#define _VGA_TIMING_H
//
// Select one video format from below
//
typedef struct {
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
} vga_timing_t;

#define NUM_TIMING_MODES 7

static const vga_timing_t _vga_timing[NUM_TIMING_MODES] =
  {
    // 0: 640 x 480 @ 60 Hz
    {
      .pix_clk = 25175000,
      .htotal = 800,
      .hactive = 640,
      .hfp = 16,
      .hsync = 96,
      .hbp = 48,
      .hpol = 0,
      .vtotal = 525,
      .vactive = 480,
      .vfp = 10,
      .vsync = 2,
      .vbp = 33,
      .vpol = 0
    },
    // 1: 800 x 600 @ 60 Hz
    {
      .pix_clk = 40000000,
      .htotal = 1056,
      .hactive = 800,
      .hfp = 40,
      .hsync = 128,
      .hbp = 88,
      .hpol = 1,
      .vtotal = 628,
      .vactive = 600,
      .vfp = 1,
      .vsync = 4,
      .vbp = 23,
      .vpol = 1
    },
    // 2: 1024 x 768 @ 60 Hz
    {
      .pix_clk = 65000000,
      .htotal = 1344,
      .hactive = 1024,
      .hfp = 24,
      .hsync = 136,
      .hbp = 160,
      .hpol = 0,
      .vtotal = 806,
      .vactive = 768,
      .vfp = 3,
      .vsync = 6,
      .vbp = 29,
      .vpol = 0
    },
    // 3: 1024 x 768 @ 70 Hz
    {
      .pix_clk = 75000000,
      .htotal = 1328,
      .hactive = 1024,
      .hfp = 24,
      .hsync = 136,
      .hbp = 144,
      .hpol = 0,
      .vtotal = 806,
      .vactive = 768,
      .vfp = 3,
      .vsync = 6,
      .vbp = 29,
      .vpol = 0
    },
    // 4: 1280 x 720 @ 60 Hz
    {
      .pix_clk = 74250000,
      .htotal = 1650,
      .hactive = 1280,
      .hfp = 110,
      .hsync = 40,
      .hbp = 220,
      .hpol = 0,
      .vtotal = 750,
      .vactive = 720,
      .vfp = 5,
      .vsync = 20,
      .vbp = 5,
      .vpol = 0
    },
    // 5: 1920 x 1080 @ 60 Hz
    {
      .pix_clk = 148500000,
      .htotal = 2200,
      .hactive = 1920,
      .hfp = 88,
      .hsync = 44,
      .hbp = 148,
      .hpol = 0,
      .vtotal = 1125,
      .vactive = 1080,
      .vfp = 4,
      .vsync = 5,
      .vbp = 36,
      .vpol = 0
    },
    // 6: 1024 x 864 @ 60Hz (not actually a VGA standard)
    {
      .pix_clk = 69196800,
      .htotal = 1280,
      .hactive = 1024,
      .hfp = 12,
      .hsync = 128,
      .hbp = 116,
      .hpol = 1,
      .vtotal = 901,
      .vactive = 864,
      .vfp = 1,
      .vsync = 3,
      .vbp = 33,
      .vpol = 1
    }
  };   


#endif // _VGA_TIMING_H

