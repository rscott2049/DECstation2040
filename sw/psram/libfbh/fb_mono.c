#define FB_MONO_DEBUG
#ifdef FB_MONO_DEBUG
#include <stdio.h>
#endif

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/interp.h"


#include "hyperram.h"
#include "fb_mono.h"
#include "fb_mono.pio.h"
#include "vga_timing.h"

// Note: if PACKED_FB is not defined, then pixels are assumed to be stored in a
// row defined by SCANLINE_POW below. This replaces a multiply with a
// shift left when calculating pixel addresses. Also, for non-SCANLINE_POW
// display sizes, a non-PACKED_FB define will result in wasted FB space
// at the end of a scan line.
// Note further that cursor support requires a non-packed FB
//#define PACKED_FB 1

// Max 2048 single bit pixels per row, or 256 bytes/64 32bit words
#define SCANLINE_POW 8
// Max 1024 single bit pixels per row, or 128 bytes/32 32bit words
//#define SCANLINE_POW 7
#define SCANLINE_BYTES (1 << SCANLINE_POW)
#define SCANLINE_WORDS (SCANLINE_BYTES/sizeof(uint32_t))

// Only change below if memory is tight, and don't need cursor support
#if 1
// Set to accomodate a max of 4096 pixels (512 bytes)
// (double buffered 2048 pixels, and cursor support)
#define LINE_BUF_POW 9
#else
// Set to accomodate a max of 2048 pixels (256 bytes)
// (double buffered 1024 pixels)
#define LINE_BUF_POW 8
#endif

#define LINE_BUF_BYTES (1 << LINE_BUF_POW)
#define LINE_BUF_WORDS (LINE_BUF_BYTES/sizeof(uint32_t))
// Scan line buf - must be byte aligned so that DMA ring buffers work correctly
uint32_t scan_buf[LINE_BUF_WORDS] __attribute__((aligned (LINE_BUF_BYTES)));

// Cursor read/write buffers. Read buffer contains data from framebuffer
// to be composited with the cursor.
// Cursor write buffer contains the composited cursor/fb data to be displayed.
// Allocate 256 bytes so that we can use LSByte of inc DMA channel
// The actual buffer will use the last 16 words 
uint32_t aligned_cur_rd_buf[64] __attribute__((aligned (256)));
uint32_t aligned_cur_wr_buf[64] __attribute__((aligned (256)));

// For use of by cursor update routine
uint32_t *cursor_rd_buf_ptr = &aligned_cur_rd_buf[64 - 16];
uint32_t *cursor_wr_buf_ptr = &aligned_cur_wr_buf[64 - 16];



// PSRAM get next buffer DMA subroutine parameters
uint32_t ps_get_buf_addr;

// Count must follow write address, to match DMA register ordering
typedef struct {
  uint32_t wr_addr;
  uint32_t count;
  uint32_t next;
} ps_get_buf_t;

ps_get_buf_t ps_cursor_get_buf = {
  .wr_addr = (uint32_t)&(aligned_cur_rd_buf[64 - 16]),
  .count = 1,
  .next = 0
};  

ps_get_buf_t ps_get_buf;
ps_get_buf_t ps_get_buf_0;
ps_get_buf_t ps_get_buf_1;
ps_get_buf_t ps_get_buf_blank_0;
ps_get_buf_t ps_get_buf_blank_1;

// DMA command offsets for cursor/scan out subroutines
uint32_t cursor_out_ptr;
uint32_t scan_out_ptr;

// DMA command offsets for cursor/scan out parameters
uint32_t scan_out_buf_cmd_ptr; // Location in PSRAM of start of frame buffer
uint32_t cursor_rd_cmd_ptr;
uint32_t cursor_wr_cmd_ptr;


// Address of routine
//uint32_t scan_out_addr;
// Address of send to video SM block read address
// Write to this location to set scan out buffer starting address
uint32_t scan_out_buf_cmd_addr;

// Jump to when done address
//uint32_t scan_out_next;
// Jump to when cursor loop finished
uint32_t cursor_next;

uint32_t cursor_cond_load_ptr;


// Buffer 0/1 return addresses
uint32_t scan_out_next_0;
uint32_t scan_out_next_1;

// Used for scan line buffer->video out
uint32_t fb_rd_cmd_ptr;

// PSRAM frame buffer base address. Used to offset drawing and refresh.
// Write this to change where FB starts in PSRAM
uint32_t fb_base_addr = 0;

// Video horizontal events dma buffer
typedef struct {
	uint32_t bp;
	uint32_t active;
        uint32_t fp;
	uint32_t sync;
} h_events_t;

// DMA control blocks
typedef struct {
  uint32_t raddr;
  uint32_t waddr;
  uint32_t count;
  dma_channel_config cnfg;
} dma_ctl_blk_t;

typedef struct {
  dma_channel_config cnfg;
  uint32_t count;
  uint32_t raddr;
  uint32_t waddr;
} dma_ctl_blk_alias_2_t;


// Control blocks: one DMA register set per line for 
// Data blocks, per line: video timing block, psram setup, scan buffer read
// Data blocks, per screen: DMA control reload block
//#define DS_VIDEO_MEM
#ifdef MIN_VIDEO_MEM
// For minimum memory usage: 640x480
#define MAX_LINES 525
#define MAX_ACTIVE_LINES 480
#elif defined DS_VIDEO_MEM
#define MAX_LINES 901
#define MAX_ACTIVE_LINES 864
#else
// Accomodate screen formats up to 1125 (1080P) vertical total lines
#define MAX_LINES 1125
#define MAX_ACTIVE_LINES 1080
#endif

#define HEVENT_LEN (sizeof(h_events_t)/sizeof(uint32_t))

// Video timing blocks
h_events_t virq_buf    __attribute__((aligned (32)));
h_events_t vfp_buf     __attribute__((aligned (32)));
h_events_t vbp_buf     __attribute__((aligned (32)));
h_events_t vsync_buf   __attribute__((aligned (32)));
h_events_t vactive_buf __attribute__((aligned (32)));
h_events_t vtest_buf __attribute__((aligned (32)));

h_events_t vpat_buf[8] __attribute__((aligned (32 * 8)));

// Two commands per active line, four per frame, two cursor dma channels
//#define CMD_BUF_SIZE (MAX_ACTIVE_LINES * 2 + 4 + 2 + 8) 

// Using active line command loops
//#define CMD_BUF_SIZE 100
//#define CMD_BUF_SIZE 60
#define CMD_BUF_SIZE 150
dma_ctl_blk_t dma_ctl[CMD_BUF_SIZE];


// PSRAM command block
// Accomodate up to one PSRAM read per scan line (i.e. read to scan line buf)
// Optionally double buffered, for tear freee FB starting address updates
#if 0
hyperram_cmd_t ps_cmd[MAX_ACTIVE_LINES+1];
hyperram_cmd_t *curr_ps_cmd_ptr = &ps_cmd[0];
#ifdef ENA_DOUBLE_BUF_PS_CMD_LIST
hyperram_cmd_t next_ps_cmd[MAX_ACTIVE_LINES+1];
hyperram_cmd_t *next_ps_cmd_ptr = &next_ps_cmd[0];
#else
hyperram_cmd_t *next_ps_cmd_ptr = &ps_cmd[0];
#endif
#endif

hyperram_cmd_t ps_cmd_buf_reset;
hyperram_cmd_t ps_cmd_buf_curr;
hyperram_cmd_t* ps_cmd_buf_curr_ptr = &ps_cmd_buf_curr;
hyperram_cmd_t* ps_cmd_buf_reset_ptr = &ps_cmd_buf_reset;

hyperram_cmd_t ps_cursor_cmd_buf[16];
hyperram_cmd_t ps_test_cmd_buf;

// DMA channels
uint32_t ctrl_dma_chan;
uint32_t data_dma_chan;
uint32_t ps_read_dma_chan;
uint32_t inc_dma_chan;
uint32_t cur_inc_dma_chan;

uint32_t inc_data;
uint32_t inc_reload_val;

uint32_t cur_inc_data;

volatile uint32_t rewrite_data = 0;
uint32_t rewrite_reload_val = 0;

uint32_t init_rewrite;  // Initialization value
uint32_t curr_rewrite;  // working value
uint32_t next_rewrite;  // branch successful value

uint32_t tmp_start;
uint32_t tmp_curr;
uint32_t tmp_addr;


// Loop values
// Start of loop
uint32_t active_start;
// Where to go when loop ends
uint32_t active_next;
// Current branch value - reloaded at vsync time
volatile uint32_t active_curr;

// Loop values
// Contains the DMA command address for the conditional DMA channel reload
uint32_t trigger_cmd_addr;
// Contains the default channel count trigger address
uint32_t trigger_cmd_default;


// For the DMA control reload block:
// Points to the start of the per line control blocks
uint32_t cmd_reload_read_addr = (uint32_t)&dma_ctl[0];

// Trigger PSRAM command channel value
uint32_t ps_cmd_trigger;
volatile uint32_t tmp_trigger;
uint32_t tmp2_trigger;

// For PS start DMA chan:
// Value to write to transfer count register
// Assume we always write PSRAM command packets
uint32_t ps_reload_count_val = sizeof(hyperram_cmd_t)/sizeof(uint32_t);

// For the DMA control reload block:
// Points to the start of the per line control blocks
//uint32_t ps_reload_read_addr = (uint32_t)&ps_cmd[0];

// For PS read DMA chan:
// Value to write to read address register
uint32_t ps_reload_scan_addr = (uint32_t)&scan_buf[0];

// Sniffer accumulator reset value
uint32_t sniffer_reset_val = 0;
uint32_t sniffer_inc_val = 1;
uint32_t sniffer_inc_tmp;
uint32_t sniffer_tmp;

// Incrementer reset value
uint32_t inc_reset_val = 0;
uint32_t inc_count_val = 1;


// Loop address storage
// 0: start of loop
// 1: branch to at end of loop
// Four sections: setup/before cursor/cursor/after cursor
uint32_t loop_addr[8]  __attribute__((aligned (8*sizeof(uint32_t))));

// Loop control structure
typedef struct {
  // These three have to be in this order, so that we can load next loop params
  uint32_t count;      // Number of times to execute loop
  uint32_t next_loop;  // Next loop structure to load
  // These three have to be in this order, so that we can do DMA loops
  uint32_t start;      // Address of subroutine to execute
  uint32_t next;       // Return to caller address
  uint32_t bump;       // Loop end handler address
} loop_ctl_t;

// Reset values for top of screen
loop_ctl_t loop_ctl_reset;

// Current values of active video loop engine - must be aligned!!
loop_ctl_t loop_ctl_curr __attribute__((aligned (4*sizeof(uint32_t))));

// Cursor control loop result (temporary)
uint32_t cur_ctl_next;

// Current values of cursor loop engine - must be aligned!!
loop_ctl_t cur_ctl_curr __attribute__((aligned (8*sizeof(uint32_t))));

// Cursor loop control blocks
// Block 0 is loaded at top of screen time

loop_ctl_t cur_ctl[4] = {
  {.start = 0, .next = 0, .bump = 0, .count = 0,
   .next_loop = (uint32_t)&cur_ctl[1]},
  {.start = 0, .next = 0, .bump = 0, .count = 0,
   .next_loop = (uint32_t)&cur_ctl[2]},
  {.start = 0, .next = 0, .bump = 0, .count = 0,
   .next_loop = (uint32_t)&cur_ctl[0]},
  {.start = 0, .next = 0, .bump = 0, .count = 0,
   .next_loop = 0}
};

uint32_t cur_inc_count_val = 1;

void dump_loop_ctl(char *type, loop_ctl_t *loop) {
  printf("%s c %08x, st %08x nxt %08x bmp %08x lp %08x\n", type,
	 loop->count, loop->start, loop->next, loop->bump, loop->next_loop);
}


// For vsync external routines
void (*fb_mono_cb_addr)() = NULL;

fb_mono_inst_t _inst;

#ifdef FB_MONO_DEBUG
void dump_dma_cfg(uint32_t cfg, uint32_t chan) {
  uint32_t chain_bits =  (cfg & DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS) >>
    DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB;

  if (chain_bits != chan) {
    printf("chain to: %02x\n", chain_bits);
  } else {
    printf("no chain\n");
  }

}

uint32_t dump_dma_offset(char* type, uint32_t chan, uint32_t addr) {
  uint32_t offset = dma_hw->ch[chan].read_addr - addr;
  printf("%s addr/offset/index: %08x %08x %d\n", type, addr, offset,
	 offset/(sizeof(dma_ctl_blk_t)));
  //sleep_ms(1000);
  return offset;
}

void dump_dma_reg(char* type, uint32_t chan) {

  printf("%s DMA chan: %d\n", type, chan);
  printf("raddr: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].read_addr,
	 dma_hw->ch[chan].read_addr);
  printf("waddr: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].write_addr,
	 dma_hw->ch[chan].write_addr);
  printf("count: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].transfer_count,
	 dma_hw->ch[chan].transfer_count);

  printf("next : %08x %08x\n", (uint32_t)&dma_debug_hw->ch[chan].tcr,
	 dma_debug_hw->ch[chan].tcr);

  printf("ctl:   %08x %08x ", (uint32_t)&dma_hw->ch[chan].ctrl_trig,
	 dma_hw->ch[chan].ctrl_trig);
  dump_dma_cfg(dma_hw->ch[chan].ctrl_trig, chan);
}
void dump_dma_ctl(char* type, dma_ctl_blk_t *blk_addr, uint32_t chan) {

  printf("%s\n", type);
  printf("raddr: %08x %08x\n", (uint32_t)&(blk_addr->raddr),
	 blk_addr->raddr);
  printf("waddr: %08x %08x\n", (uint32_t)&(blk_addr->waddr),
	 blk_addr->waddr);
  printf("count: %08x %08x\n", (uint32_t)&(blk_addr->count),
	 blk_addr->count);
  printf("ctl:   %08x %08x ", (uint32_t)&(blk_addr->cnfg),
	 blk_addr->cnfg);

  dump_dma_cfg(blk_addr->cnfg.ctrl, chan);
  printf("Final read addr: %08x\n", 
	 blk_addr->raddr + 4 * blk_addr->count);

}

void dump_dma_data(char* type, uint32_t *dat_buf, uint32_t count) {

  printf("%s\n", type);
  for (int i = 0; i < count; i++) {
    int j = i * 4;
    printf("%d addr/data{0:3} %08x  %08x %08x %08x %08x\n",
	   i, (uint32_t)&dat_buf[j],
	   dat_buf[j], dat_buf[j+1], dat_buf[j+2], dat_buf[j+3]);
  }
}

#endif

void clear_dma(uint32_t dma_chan_transfer, uint32_t dma_chan_loop) {
    dma_channel_abort(dma_chan_transfer);
    dma_channel_abort(dma_chan_loop);
    dma_channel_hw_addr(dma_chan_transfer)->al1_ctrl = 0;
    dma_channel_hw_addr(dma_chan_loop)->al1_ctrl = 0;
}

// Per line video event generator
// Events are (in order):
// 0) front porch (before sync assertion
// 1) sync
// 2) back porch (after sync deassertion)
// 3) active (display pixels)
//
void h_line(h_events_t *t, const fb_mono_inst_t *inst, uint32_t active, uint32_t vs) {
  uint32_t vsync, hsync_assert, hsync_deassert;
  uint16_t jmp_inst, set_sync_inst, clr_sync_inst, nop_inst;

  // Determine actual vsync, hsync assertion levels for commands
  // Input vsync is always asserted high
  vsync = (~inst->vpol ^ vs) & 0x01;
  hsync_assert = inst->hpol & 0x01;
  hsync_deassert = (~inst->hpol) & 0x01;
  
  // Position syncs in the out instruction immediate value
  vsync = vsync << inst->vsync_offset;
  // Combine vsync with hsync
  hsync_assert   = (hsync_assert   << inst->hsync_offset) | vsync;
  hsync_deassert = (hsync_deassert << inst->hsync_offset) | vsync;

  // Generate PIO instructions
  // Note: jump instruction signals pixels to follow
  jmp_inst = pio_encode_jmp(inst->prog_offset_video + fb_video_offset_vidout);
  set_sync_inst = pio_encode_set(pio_pins, hsync_assert);
  clr_sync_inst = pio_encode_set(pio_pins, hsync_deassert);
  nop_inst = pio_encode_nop();

  // Generate hsync front porch/assert/active/back porch FIFO commands
  // See fb_mono.pio for explanation of timing and FIFO formatting
  t->sync   = (set_sync_inst << 16) | ((inst->hsync - 2)  * 2);

  if (active == 0) {
    t->bp     = (clr_sync_inst << 16) | ((inst->hbp - 2) * 2 );
    t->active = (nop_inst << 16) | ((inst->hactive - 2) * 2);
    t->fp     = (nop_inst << 16) | ((inst->hfp - 2) * 2);
  } else {
    t->bp     = (clr_sync_inst << 16) | ((inst->hbp - 2) * 2 - 1);
    t->active = (jmp_inst << 16) | inst->hactive - 1;
    t->fp     = (nop_inst << 16) | ((inst->hfp - 2) * 2 - 2);
  }    
}


// Output horizontal events for an all white line
void h_line_test(h_events_t *t, const fb_mono_inst_t *inst, uint32_t active, uint32_t vs) {
  uint32_t vsync, hsync_assert, hsync_deassert;
  uint16_t jmp_inst, set_sync_inst, clr_sync_inst, nop_inst;
  uint16_t set_white_inst, set_black_inst;

  // Determine actual vsync, hsync assertion levels for commands
  // Input vsync is always asserted high
  vsync = (~inst->vpol ^ vs) & 0x01;
  hsync_assert = inst->hpol & 0x01;
  hsync_deassert = (~inst->hpol) & 0x01;
  
  // Position syncs in the out instruction immediate value
  vsync = vsync << inst->vsync_offset;
  // Combine vsync with hsync
  hsync_assert   = (hsync_assert   << inst->hsync_offset) | vsync;
  hsync_deassert = (hsync_deassert << inst->hsync_offset) | vsync;

  // Generate PIO instructions
  // Note: jump instruction signals pixels to follow
  jmp_inst = pio_encode_jmp(inst->prog_offset_video + fb_video_offset_vidout);
  set_sync_inst = pio_encode_set(pio_pins, hsync_assert);
  clr_sync_inst = pio_encode_set(pio_pins, hsync_deassert);
  nop_inst = pio_encode_nop();

  set_white_inst = nop_inst | pio_encode_sideset_opt(1, 1);
  set_black_inst = nop_inst | pio_encode_sideset_opt(1, 0);

  // Generate hsync front porch/assert/active/back porch FIFO commands
  // See fb_mono.pio for explanation of timing and FIFO formatting
  t->sync   = (set_sync_inst << 16) | ((inst->hsync - 2)  * 2);

  if (active == 0) {
    t->bp     = (clr_sync_inst << 16) | ((inst->hbp - 2) * 2 );
    t->active = (nop_inst << 16) | ((inst->hactive - 2) * 2);
    t->fp     = (nop_inst << 16) | ((inst->hfp - 2) * 2);
  } else {
    t->bp     = (clr_sync_inst << 16) | ((inst->hbp - 2) * 2 );
    t->active = (set_white_inst << 16) | ((inst->hactive - 2) * 2);
    t->fp     = (set_black_inst << 16) | ((inst->hfp - 2) * 2);
 
  }    
}

// Setup PSRAM command to read a scan line from PSRAM
// PSRAM command takes byte addresses and length in words
#if 0
void psram_hline(fb_mono_inst_t *inst, hyperram_cmd_t *fb_read,
		 uint32_t active_line, uint32_t start) {

  // 1 bit per pixel
#ifdef PACKED_FB
  uint32_t bytes_per_scanline = inst->hactive/8;
#else
  uint32_t bytes_per_scanline = SCANLINE_BYTES;
#endif

  uint32_t words_per_scanline = inst->hactive/32;

  // Use PSRAM controller for frame buffer reads
  // Start at address zero
  _hyperram_cmd_init(fb_read, &g_hram_all[inst->sm_fb], HRAM_CMD_READ,
		     ((active_line) * bytes_per_scanline) + start,
		     words_per_scanline);
}
#endif
// Setup PSRAM command to read a scan line from PSRAM
// PSRAM command takes byte addresses and length in words
void psram_hline(fb_mono_inst_t *inst, hyperram_cmd_t *fb_read,
		 uint32_t active_line, uint32_t start, uint32_t len) {

  // 1 bit per pixel
#ifdef PACKED_FB
  uint32_t bytes_per_scanline = inst->hactive/8;
#else
  uint32_t bytes_per_scanline = SCANLINE_BYTES;
#endif

  // Use PSRAM controller for frame buffer reads
  _hyperram_cmd_init(fb_read, &g_hram_all[inst->sm_fb], HRAM_CMD_READ,
		     ((active_line) * bytes_per_scanline) + start, len);
}

// Generate DMA commands for video output

uint32_t gen_dma_buf(fb_mono_inst_t *inst,
		     uint32_t cmd_chan,
		     uint32_t dat_chan,
		     uint32_t ps_read_chan,
		     uint32_t inc_chan,
		     uint32_t cur_inc_chan,
		     dma_ctl_blk_t *cmd_buf,
		     uint32_t *scan,
		     uint32_t *cmd_reload
		     ) {
		     
  h_events_t h;
  uint32_t vs;
  uint32_t dat_ptr = 0;
  uint32_t cmd_ptr = 0;
  uint32_t ps_ptr = 0;


  // 0) Setup DMA channel to send to the video sm:
  dma_channel_config cfg_vid = dma_channel_get_default_config(dat_chan);

  // Increment read address, since we're writing to a SM input FIFO
  channel_config_set_read_increment(&cfg_vid, true); 

  // Don't increment write address 
  channel_config_set_write_increment(&cfg_vid, false); 

  // Tell DMA to let the SM request data
  channel_config_set_dreq(&cfg_vid,
			  pio_get_dreq(inst->pio_vid, inst->sm_video, true));

  // Chain to command channel to fetch the next set of commands
  channel_config_set_chain_to(&cfg_vid, cmd_chan);

  // 0a) Setup DMA channel to send to the video sm, with wrap:
  dma_channel_config cfg_vid_wrap = cfg_vid;

  // Wrap read address on four word boundary (16 bytes)
  channel_config_set_ring(&cfg_vid_wrap, false, 4);

  // 0b) Setup DMA channel to send to the video sm, then stop chain
  dma_channel_config cfg_vid_stop = cfg_vid;
  channel_config_set_chain_to(&cfg_vid_stop, dat_chan);

  // 0c) Setup DMA channel to send to the video sm, then stop chain
  dma_channel_config cfg_vid_wrap_stop = cfg_vid_wrap;
  channel_config_set_chain_to(&cfg_vid_wrap_stop, dat_chan);

  // 1) Setup DMA channel to send to the frame buffer PSRAM channel
  // We use the defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  dma_channel_config cfg_ps_cmd = dma_channel_get_default_config(dat_chan);

  // Increment read address, since we're writing to a SM input FIFO
  channel_config_set_read_increment(&cfg_ps_cmd, true); 

  // Don't increment write address 
  channel_config_set_write_increment(&cfg_ps_cmd, false); 

  // Tell DMA to let the SM request data
  channel_config_set_dreq(&cfg_ps_cmd,
			  pio_get_dreq(inst->pio_vid, inst->sm_fb, true));

  // Chain to command channel to fetch next DMA cmd packet
  channel_config_set_chain_to(&cfg_ps_cmd, cmd_chan);

  // 2) Setup DMA datachannel to get the next command
  // We start with the defaults:
  // 32 bit xfer, unpaced, no write inc, read inc, no chain
  dma_channel_config cfg_next = dma_channel_get_default_config(dat_chan);

  // Chain to the command channel to get next command
  channel_config_set_chain_to(&cfg_next, cmd_chan);

  // 2a) Setup DMA data channel to wait
  dma_channel_config cfg_wait = dma_channel_get_default_config(dat_chan);

  // 2b) Turn on sniffer for the given block
  dma_channel_config cfg_next_sniff_en = cfg_next;
  channel_config_set_sniff_enable(&cfg_next_sniff_en, 1);

  // 2c) Do byte copy
  dma_channel_config cfg_next_byte = cfg_next;

  // Increment write pointer (to do multi-byte copies)
  channel_config_set_write_increment(&cfg_next_byte, true); 

  // Set size of datum to byte
  channel_config_set_transfer_data_size(&cfg_next_byte, DMA_SIZE_8);

  // 2d) Enable multiple reads, for conditional jumps 
  //dma_channel_config cfg_next_rd_wrap = cfg_next;

  // Wrap read address on two word boundary (8 bytes) (for potential bad counts)
  //channel_config_set_ring(&cfg_next_rd_wrap, false, 3);

  // 2e) Enable byte swap
  dma_channel_config cfg_next_swap = cfg_next;
  channel_config_set_bswap(&cfg_next_swap, true);

  // 2f) Enable write address increment
  dma_channel_config cfg_next_wr_inc = cfg_next;

  // Increment write pointer (to do multi-word copies)
  channel_config_set_write_increment(&cfg_next_wr_inc, true); 

  // Generate virq, vfp/vbp, v sync, v active timing events
  h_line(&vfp_buf, inst, 0, 0);
  h_line(&virq_buf, inst, 0, 0);
  h_line(&vbp_buf, inst, 0, 0);
  h_line(&vsync_buf, inst, 0, 1);
  h_line(&vactive_buf, inst, 1, 0);

  // Setup ps command buffer defaults
  // PSRAM read command to read current line, at PSRAM offset 0
  psram_hline(inst, &ps_cmd_buf_reset, 0, 0, inst->hactive/32);


  // Generate scan line address increment
  psram_hline(inst, &ps_cmd_buf_curr, 1, 0, inst->hactive/32);

  // LSB of address is in cmd1 (big endian)
  uint32_t cmd1_be = ps_cmd_buf_curr.cmd1 - ps_cmd_buf_reset.cmd1;

  // Make inc value little endian
  sniffer_inc_val = (((cmd1_be >> 24) & 0xff) |
		     (((cmd1_be >> 16) & 0xff) << 8) |
		     (((cmd1_be >> 8) & 0xff) << 16) |
		     (((cmd1_be >> 0) & 0xff) << 24));

  // Align sniffer value with cmd0/cmd1 fetched values (see below)
  // So that we can increment the full address (split across cmd0/1)
  sniffer_inc_val = sniffer_inc_val >> 8;

  // Start generating DMA command list
  // Timing wise, we're just after the last active line 
  cmd_ptr = 0;

  // Send vertical front porch timing to video SM
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vfp_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = inst->vfp * sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++;

  // Send vertical sync timing to video SM 
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vsync_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = (inst->vsync) * sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++; 

  // Ensure PSRAM cursor command buffer has valid contents at reset
  fb_base_addr = 0;
  fb_mono_set_cursor_pos(0, 0);

  // Reset the PS read channel write address/length, and start it
  ps_cursor_get_buf.count = 16;

  // Reset cursor write DMA channel address/length, and enable it
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_cursor_get_buf.wr_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(dma_hw->ch[ps_read_chan].al1_write_addr);
  cmd_buf[cmd_ptr].count = 2; 
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Execute PS read commands to fill cursor read buffer
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_cursor_cmd_buf[0]);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(inst->pio_mem->txf[inst->sm_fb]);
  cmd_buf[cmd_ptr].count = sizeof(hyperram_cmd_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_ps_cmd;
  cmd_ptr++;

  // Send a vbp line to allow PSRAM cmd to complete
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vbp_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid;
  cmd_ptr++;

  // Now we can read the rest of the cursor data
  for (int i = 1; i < 16; i++) {
    // Send PSRAM cursor data read commands
    cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_cursor_cmd_buf[i]);
    cmd_buf[cmd_ptr].waddr = (uint32_t)&(inst->pio_mem->txf[inst->sm_fb]);
    cmd_buf[cmd_ptr].count = sizeof(hyperram_cmd_t)/sizeof(uint32_t);
    cmd_buf[cmd_ptr].cnfg = cfg_ps_cmd;
    cmd_ptr++;

    // Send a vbp line to allow PSRAM cmd to complete
    cmd_buf[cmd_ptr].raddr = (uint32_t)&vbp_buf;
    cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
    cmd_buf[cmd_ptr].count = sizeof(h_events_t)/sizeof(uint32_t);
    cmd_buf[cmd_ptr].cnfg = cfg_vid;
    cmd_ptr++;
  }

  // Send the vertical interrupt for one line, taking one away from
  // vertical back porch
  cmd_buf[cmd_ptr].raddr = (uint32_t)&virq_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid;
  cmd_ptr++;

  // Send vertical back porch timing to video SM, except for 3 + 16 lines
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vbp_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = (inst->vbp-3-16) * sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++;

  // Setup active lines loop control
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(loop_ctl_reset);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(loop_ctl_curr);
  cmd_buf[cmd_ptr].count = sizeof(loop_ctl_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Setup cursor loop, starting with first control block
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl[0]);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cur_ctl_curr);
  cmd_buf[cmd_ptr].count = sizeof(loop_ctl_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Get big-endian PSRAM starting address value split across cmd0 and cmd1
  cmd_buf[cmd_ptr].raddr = (uint32_t)(&ps_cmd_buf_reset.cmd0) + 3;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&sniffer_tmp;
  cmd_buf[cmd_ptr].count = 4;
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Load sniffer with word aligned byte swapped starting address value
  // This transforms big-endian address to little endian for incrementing
  cmd_buf[cmd_ptr].raddr = (uint32_t)&sniffer_tmp;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->sniff_data;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next_swap;
  cmd_ptr++;
  
  // Reset loop incrementer starting value
  cmd_buf[cmd_ptr].raddr = (uint32_t)&loop_ctl_curr.count;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[inc_chan].read_addr;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Reset cursor loop incrementer starting value
  cmd_buf[cmd_ptr].raddr = (uint32_t)&cur_ctl_curr.count;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cur_inc_chan].read_addr;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Reset PSRAM read command to scan line 0
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_cmd_buf_reset);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_cmd_buf_curr);
  cmd_buf[cmd_ptr].count = sizeof(hyperram_cmd_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // We double buffer the scan lines, so that we have a full line time
  // to fetch and modify them, prior to display. Thus, in vblank time,
  // we need to fill two buffers.
  // Fill first buffer with scan line 0
  ps_get_buf_blank_0.wr_addr = (uint32_t)&(scan_buf[0]);
  ps_get_buf_blank_0.count = inst->hactive/32;
  ps_get_buf_blank_0.next = (uint32_t)&(cmd_buf[cmd_ptr + 2].raddr); // next cmd
  
  // Swap in parameters
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_blank_0);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_get_buf);
  cmd_buf[cmd_ptr].count = sizeof(ps_get_buf_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Jump to scan line fetch subroutine
  // Subroutine will jump to address contained in ps_get_buf.next when done
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Send vertical back porch line to video SM
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vbp_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++;

  // Fill second buffer with scan line 1
  ps_get_buf_blank_1.wr_addr = (uint32_t)&(scan_buf[LINE_BUF_WORDS/2]);
  ps_get_buf_blank_1.count = inst->hactive/32;
  ps_get_buf_blank_1.next = (uint32_t)&(cmd_buf[cmd_ptr + 2].raddr); // next cmd

  // Swap in parameters
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_blank_1);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_get_buf);
  cmd_buf[cmd_ptr].count = sizeof(ps_get_buf_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Jump to scan line fetch subroutine
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Send last line of vertical back porch timing to video SM
  // Enables buffer fetch to complete before active
  cmd_buf[cmd_ptr].raddr = (uint32_t)&vbp_buf;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = sizeof(h_events_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++;

  // Save current buffer pointer for active video loop start
  loop_ctl_reset.start = (uint32_t)&(cmd_buf[cmd_ptr]);

  // Now do active video 

  // Build scan out parameter block
  // Switch scan out pixel source
  uint32_t scan_src_0_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_0.wr_addr);
  cmd_buf[cmd_ptr].waddr = 0; // Will be over written at scan out creation time
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;
  
  // Switch out return address (see below)
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(scan_out_next_0);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cur_ctl_curr.next);
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Jump to scan out/cursor out subroutine
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl_curr.start);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Since we've stuffed timing info into video pipe, we now have maximum time
  // to prepare next buffer

  // Get next scan line from PSRAM
  // Parameters for get ps buf subroutine:
  // Which buffer to write to
  // Where we are going to jump to when complete

  // Continue here after scan out (see above)
  scan_out_next_0 = (uint32_t)&(cmd_buf[cmd_ptr].raddr);

  // Setup subroutine parameter block
  // When finished, jump to next DMA command block
  ps_get_buf_0.wr_addr = (uint32_t)&scan_buf[0];
  ps_get_buf_0.count = inst->hactive/32;
  ps_get_buf_0.next = (uint32_t)&(cmd_buf[cmd_ptr + 2].raddr);

  // Swap in parameters
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_0);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_get_buf);
  cmd_buf[cmd_ptr].count = sizeof(ps_get_buf_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Jump to sub routine
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Bump loop counter
  inc_count_val = 1;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&inc_count_val;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(dma_hw->ch[inc_chan].al1_transfer_count_trig);  
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Setup for scan out DMA subroutine, next line
  // Switch scan out source
  uint32_t scan_src_1_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_1.wr_addr);
  cmd_buf[cmd_ptr].waddr = 0; // Will be over written at scan out creation time
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;
  
  // Switch out return address
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(scan_out_next_1);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cur_ctl_curr.next);
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Jump to scan out routine
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl_curr.start);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Continue here after scan out
  scan_out_next_1 = (uint32_t)&(cmd_buf[cmd_ptr].raddr);

  // Setup get next scan line subroutine parameter block
  // When finished, jump to next DMA command block
  ps_get_buf_1.wr_addr = (uint32_t)&scan_buf[LINE_BUF_WORDS/2];
  ps_get_buf_1.count = inst->hactive/32;
  ps_get_buf_1.next = (uint32_t)&(cmd_buf[cmd_ptr + 2].raddr);

  // Setup for scan out DMA subroutine
  // Continue here after scan out
  scan_out_next_1 = (uint32_t)&(cmd_buf[cmd_ptr].raddr);

  // Swap in parameters
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_1);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_get_buf);
  cmd_buf[cmd_ptr].count = sizeof(ps_get_buf_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Jump to get PSRAM scan line
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Generate count value for reload of loop address: 1 or 2
  // Write to loop jump cmd packet counter value
  uint32_t inc_target_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = ((uint32_t)&(dma_hw->ch[inc_chan].read_addr)) + 2;
  cmd_buf[cmd_ptr].waddr = 0; // Will be filled in below
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Location of reload count value
  // We either have 1 or 2, so that we can conditionally load a new address
  uint32_t loop_cnt_addr = (uint32_t)&(cmd_buf[cmd_ptr].count);

  // Now fill in the address for writing the increment target to
  cmd_buf[inc_target_ptr].waddr = loop_cnt_addr;

  // Generate next cmd ptr, conditionally
  // The count below selects which loop_addr array entry to use
  // This is loop jump command packet
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(loop_ctl_curr.start);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&active_curr;
  cmd_buf[cmd_ptr].count = 1;  // Will be overwritten by conditional step above
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Bump loop counter
  inc_count_val = 1;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&inc_count_val;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(dma_hw->ch[inc_chan].al1_transfer_count_trig);  
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Go to top of loop
  // Writing to the read address (alias 3) will restart the control DMA sequence
  cmd_buf[cmd_ptr].raddr = (uint32_t)&active_curr;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Save address for loop exit
  loop_ctl_reset.next = (uint32_t)&(cmd_buf[cmd_ptr]);

  // Write control DMA restart
  // Writing to the read address (alias 3) will restart the control DMA sequence
  cmd_buf[cmd_ptr].raddr = (uint32_t)cmd_reload;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;


  // Fill scan buffer DMA command subroutine
  // Generates PSRAM command packet, sends to PSRAM, writes result to buffer
  // Needs:
  //   ps_get_buf.wr_addr: Buffer address to write PSRAM data to
  //   ps_get_buf.count: Number of words to fetch from PSRAM
  //   ps_get_buf.next: Address of next DMA command to execute
  // Uses:
  //   DMA sniffer
  //   ps_cmd_buf_curr
  //

  // Save current cmd ptr as entry point
  uint32_t ps_get_buf_ptr = cmd_ptr;

  // Reset the PS read channel write address/length, and start it
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf.wr_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(dma_hw->ch[ps_read_chan].al1_write_addr);
  cmd_buf[cmd_ptr].count = 2; 
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Increment sniffer accumulator for next scan line
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(sniffer_inc_val);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(sniffer_inc_tmp);
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next_sniff_en;
  cmd_ptr++;

  // Save byte swapped sniffer accumulator as big endian PSRAM address
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(dma_hw->sniff_data);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(sniffer_tmp);
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next_swap;
  cmd_ptr++;

  // Send the PSRAM command
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_cmd_buf_curr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(inst->pio_mem->txf[inst->sm_fb]);
  cmd_buf[cmd_ptr].count = sizeof(hyperram_cmd_t)/sizeof(uint32_t);
  cmd_buf[cmd_ptr].cnfg = cfg_ps_cmd;
  cmd_ptr++;

  // Write new address into psram CMD0/1 for next PSRAM fetch
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(sniffer_tmp);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(ps_cmd_buf_curr.cmd0) + 3;
  cmd_buf[cmd_ptr].count = 4;
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Return from subroutine
  // Writing to the read address (alias 3) will fetch the next DMA command
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(ps_get_buf.next);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;

  // Do fixups for forward refs
  ps_get_buf_addr = (uint32_t)&(cmd_buf[ps_get_buf_ptr].raddr);


  // Scan out DMA command subroutine
  // Sends out timing packet, followed by video
  // Note: To eliminate keeping track of odd/even scanlines,
  // we copy the scan out address, and overwrite the LSB with
  // the cursor offset LSB. This requires scan line buffer to be 256 bytes
  // long.
  // Needs:
  //   vactive_buf: contains horizontal timing information
  //   cmd_buf[scan_out_buf_ptr].raddr: Buffer address fetch pixels from
  //   scan_out_next: Return address, written to cmd ptr when done
  // Uses:
  //

  // Save current cmd ptr as scan out starting point
  cursor_out_ptr = cmd_ptr;

  // Start cursor composition process
  // Generate current offset into cursor compositing buffer
  // Note that cursor buffer must be 256 byte aligned, 16 away from the end

#ifdef READ_CURSOR_ACTIVE_TIME
  // First, read current cursor increment and write as offset into save buffer
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(dma_hw->ch[cur_inc_chan].read_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cmd_buf[cmd_ptr + 2].waddr);
  cmd_buf[cmd_ptr].count = 1; // LSB only
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Overwrite cursor read address MSBs with MSBs of current scan buf addr
  uint32_t cursor_rd_overwrite_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = 0; // Will be scan out address from below
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cmd_buf[cmd_ptr + 1].raddr) + 1; 
  cmd_buf[cmd_ptr].count = 3; // Upper three MSBs
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Save cursor pixels for compositing
  cursor_rd_cmd_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = 0; // LSB will be over-written by set_cursor_pos
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(aligned_cur_rd_buf[64-16]);
  cmd_buf[cmd_ptr].count = 4; // Max 16 pixels, unaligned, but fill all 32 bits
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;
#endif

  // Generate current offset into cursor compositing buffer
  // Note that cursor buffer must be 256 byte aligned, 16 away from the end
  // First, read current cursor increment to get buffer offset
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(dma_hw->ch[cur_inc_chan].read_addr);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cmd_buf[cmd_ptr + 2].raddr);
  cmd_buf[cmd_ptr].count = 1; // LSB only
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;
  
  // Overwrite cursor write address MSBs with MSBs of current scan buf addr
  uint32_t cursor_wr_overwrite_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = 0; // Will be scan out address from below
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cmd_buf[cmd_ptr + 1].waddr) + 1;
  cmd_buf[cmd_ptr].count = 3; // MSBs
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Write composited pixels
  cursor_wr_cmd_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(aligned_cur_wr_buf[64 - 16]);
  cmd_buf[cmd_ptr].waddr = 0; // LSB will be overwritten by set cursor_pos
  cmd_buf[cmd_ptr].count = 3; // Max 16 pixels, unaligned, so we need 24 bits
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;

  // Save current cmd ptr as scan out entry point, skipping cursor compositing
  scan_out_ptr = cmd_ptr;

  // Now do active video 
  // Start first line
  // First, put the video SM into receive pixel mode by sending bp/active events
  // from events buffer
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(vactive_buf.bp);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = 2;
  cmd_buf[cmd_ptr].cnfg = cfg_vid;
  cmd_ptr++;

  // Send pixels
  scan_out_buf_cmd_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = 0; // will be over written
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = inst->hactive/32; //words_per_scanline;
  cmd_buf[cmd_ptr].cnfg = cfg_vid;
  cmd_ptr++;

  // Finish up active video by sending horizontal front porch/sync events
  // and start next line
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(vactive_buf.fp);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&inst->pio_vid->txf[inst->sm_video];
  cmd_buf[cmd_ptr].count = 2;
  cmd_buf[cmd_ptr].cnfg = cfg_vid_wrap;
  cmd_ptr++;

  // Bump cursor loop counter
  cur_inc_count_val = 1;
  cmd_buf[cmd_ptr].raddr = (uint32_t)&cur_inc_count_val;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(dma_hw->ch[cur_inc_chan].al1_transfer_count_trig);  
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Determine next DMA command block
  // Three different phases:
  //   1) before cursor: skip compositing if loop count not met
  //   2) during cursor: do compositing if loop count not met
  //   3) after cursor: skip compositing if loop count not met
  // In all phases, jump to next loop load when count met,
  // to setup for next phase

  // Generate count value for reload of loop address: 1 or 2
  // Write to cursor jump cmd packet counter value
  uint32_t cur_inc_target_ptr = cmd_ptr;
  cmd_buf[cmd_ptr].raddr = ((uint32_t)&(dma_hw->ch[cur_inc_chan].read_addr)) + 2;
  cmd_buf[cmd_ptr].waddr = 0; // Will be filled in below
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next_byte;
  cmd_ptr++;
  
  // Location of cursor reload count value
  // We either have 1 or 2, so that we can conditionally load a new address
  uint32_t cur_cnt_addr = (uint32_t)&(cmd_buf[cmd_ptr].count);

  // Now fill in the address for writing the increment target to
  cmd_buf[cur_inc_target_ptr].waddr = cur_cnt_addr;

  // Save as branch point
  cursor_cond_load_ptr = cmd_ptr;

  // Generate next cmd ptr, conditionally
  // The count below selects which loop_addr array entry to use
  // This is the cursor loop jump command packet
  // We'll either return to caller, or bump to next cursor out ctl block
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl_curr.next);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cur_ctl_next);
  cmd_buf[cmd_ptr].count = 1;  // Will be overwritten by conditional step above
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Writing to the read address (alias 3) will restart the control DMA sequence
  cmd_buf[cmd_ptr].raddr = (uint32_t)&cur_ctl_next;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;
    
  // Here when cursor control loop count reached
  uint32_t cursor_bump_ptr = cmd_ptr;

  // Reload cursor control parameters
  // Get address of next block, write to copy command below
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl_curr.next_loop);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cmd_buf[cmd_ptr + 1].raddr);
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Copy new commands to current (count, next_loop, start addr)
  // Note: do not overwrite next/bump - already filled in
  cmd_buf[cmd_ptr].raddr = 0; // Filled in by above
  cmd_buf[cmd_ptr].waddr = (uint32_t)&(cur_ctl_curr.count);
  cmd_buf[cmd_ptr].count = 3;
  cmd_buf[cmd_ptr].cnfg = cfg_next_wr_inc;
  cmd_ptr++;

  // Reset cursor loop incrementer starting value
  cmd_buf[cmd_ptr].raddr = (uint32_t)&cur_ctl_curr.count;
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cur_inc_chan].read_addr;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_next;
  cmd_ptr++;

  // Return to caller
  // Write control DMA restart
  // Writing to the read address (alias 3) will restart the control DMA sequence
  cmd_buf[cmd_ptr].raddr = (uint32_t)&(cur_ctl_curr.next);
  cmd_buf[cmd_ptr].waddr = (uint32_t)&dma_hw->ch[cmd_chan].al3_read_addr_trig;
  cmd_buf[cmd_ptr].count = 1;
  cmd_buf[cmd_ptr].cnfg = cfg_wait;
  cmd_ptr++;


  // Do scan out fixups 
  // Set odd/even scan line buffer addresses for reading pixels
  scan_out_buf_cmd_addr = (uint32_t)&(cmd_buf[scan_out_buf_cmd_ptr].raddr);
  cmd_buf[scan_src_0_ptr].waddr = scan_out_buf_cmd_addr;
  cmd_buf[scan_src_1_ptr].waddr = scan_out_buf_cmd_addr;

  // Do cursor fixups
  // Update the cursor read/write scan out address overwrites
#ifdef READ_CURSOR_ACTIVE_TIME
  cmd_buf[cursor_rd_overwrite_ptr].raddr = scan_out_buf_cmd_addr + 1;
#endif
  cmd_buf[cursor_wr_overwrite_ptr].raddr = scan_out_buf_cmd_addr + 1;

  // Do cursor control fixups
  // Skip compositing until count reached
  cur_ctl[0].start = (uint32_t)&(cmd_buf[scan_out_ptr]);
  cur_ctl[0].bump = (uint32_t)&(cmd_buf[cursor_bump_ptr]);
  cur_ctl[0].count = 0x20020000 - ((inst->vactive/2 - 4) << 2);

  // Cursor display until count reached
  cur_ctl[1].start = (uint32_t)&(cmd_buf[cursor_out_ptr]);
  cur_ctl[1].bump = (uint32_t)&(cmd_buf[cursor_bump_ptr]);
  cur_ctl[1].count = 0x20020000 - (16 << 2);

  // Skip compositing for the rest of the screen
  cur_ctl[2].start = (uint32_t)&(cmd_buf[scan_out_ptr]);
  cur_ctl[2].bump = (uint32_t)&(cmd_buf[cursor_bump_ptr]);
  cur_ctl[2].count = 0x20020000 - ((inst->vactive) << 2);

  printf("Last cmd ptr: %d\n", cmd_ptr);
  return cmd_ptr;
}


// Seven DMA channels:
// 0) Video command channel - sets up video data channel
// 1) Video data channel - transfers data to video SM; end of screen reloads
// 2) PS start - when triggered, starts PSRAM read chain
// 3) PS command channel - sets up PSRAM reads
// 4) PS read channel - transfers data from PSRAM to scan line buffer
// 5) Cursor read channel - reads from scan line buffer, writes to buffer
// 6) Cursor write channel - reads from buffer, writes to scan line buffer
//
// Video command DMA writes DMA parameter blocks to video DMA channel
// Video data DMA does actual data movement
// PS start writes to PS command channel count register, triggers Vid Cmd
// PS command writes PSRAM command blocks to PSRAM SM, triggers PS read
// PS read transfers from PSRAM output FIFO to scan buf memory
//
// We setup the Video Command, PS start/command/read channels;
// the Video data channel will be setup by the Video command channel.
//
//
void dma_init_chain(fb_mono_inst_t *inst,
		    uint32_t cmd_chan,
		    uint32_t data_chan,
		    uint32_t ps_read,
		    uint32_t inc_chan,
		    uint32_t cur_inc_chan,
		    dma_ctl_blk_t *cmd_buf,
		    uint32_t *scan
		    ) {

  dma_channel_config cmd_config;
  dma_channel_config ps_read_config;
  dma_channel_config inc_chan_config;
  dma_channel_config cur_inc_chan_config;
  
  // 0) Command channel setup
  cmd_config = dma_channel_get_default_config(cmd_chan);

  // Increment read address
  channel_config_set_read_increment(&cmd_config, true); 

  // Increment write address
  channel_config_set_write_increment(&cmd_config, true); 

  // Limit writes to 4 command packet words
  channel_config_set_ring(&cmd_config, true, 4);

  // Set up the command DMA channel with first command from cmd buf
  dma_channel_configure(cmd_chan, &cmd_config, 
			&dma_hw->ch[data_chan].read_addr, // Read/wr/count/cfg
			(uint32_t *)cmd_buf,
			sizeof(dma_ctl_blk_t)/sizeof(uint32_t),
			false);


  // 1) PSRAM read data channel setup
  ps_read_config = dma_channel_get_default_config(ps_read);

  // Don't increment read address
  channel_config_set_read_increment(&ps_read_config, false); 

  // Increment write address
  channel_config_set_write_increment(&ps_read_config, true); 
  
  // Tell DMA to transfer from FIFO to memory
  channel_config_set_dreq(&ps_read_config,
			  pio_get_dreq(inst->pio_mem, inst->sm_fb, false));

  // Transfer a single scan line from PSRAM to memory
  dma_channel_configure(ps_read, &ps_read_config,
			&scan[0],
  			&inst->pio_mem->rxf[inst->sm_fb],
			inst->hactive/32,
			false);

  
  // 2) Enable sniffer on dma data channel, set to just add
  dma_sniffer_enable(data_chan, 0x0f, 0);


  // 3) Scan line incrementer channel config
  inc_chan_config = dma_channel_get_default_config(inc_chan);

  // Increment read address
  channel_config_set_read_increment(&inc_chan_config, true); 

  // Do not increment write address
  channel_config_set_write_increment(&inc_chan_config, false); 

  // Write to inc_data when triggered, increasing the read addr count
  loop_ctl_reset.count = 0x20020000 - ((inst->vactive - 1) << 2);

  dma_channel_configure(inc_chan, &inc_chan_config,
			&inc_data,
			(const volatile void *)loop_ctl_reset.count,
			1,
			false);

  // 3) Cursor incrementer channel config
  cur_inc_chan_config = dma_channel_get_default_config(cur_inc_chan);

  // Increment read address
  channel_config_set_read_increment(&cur_inc_chan_config, true); 

  // Do not increment write address
  channel_config_set_write_increment(&cur_inc_chan_config, false); 

  // Write to inc_data when triggered, increasing the read addr count
  // Disable cursor initially by putting count outside screen
  //cur_ctl[0].count = 0x20020000 - ((inst->vactive + 1) << 2);

  dma_channel_configure(cur_inc_chan, &cur_inc_chan_config,
			&cur_inc_data,
			(const volatile void *)cur_ctl[0].count,
			1,
			false);

  // Start command channel
  dma_hw->multi_channel_trigger = (1 << cmd_chan);

}

int32_t fb_mono_pio_init(fb_mono_inst_t *inst) {
  pio_sm_config c;

  // initialize vga sync pins
  pio_gpio_init(inst->pio_vid, inst->sync_base_pin);
  pio_gpio_init(inst->pio_vid, inst->sync_base_pin + 1);

  // Make them outputs
  pio_sm_set_consecutive_pindirs(inst->pio_vid, inst->sm_video, 
				 inst->sync_base_pin, 2, true);
  // Video output pin
  uint32_t pin = inst->vga_green_pin;

  // Init video out pin 
  pio_gpio_init(inst->pio_vid, pin);

  // Set slew rate/drive strength
#if 0
  gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_8MA);
#endif
  
  // Make it an output
  pio_sm_set_consecutive_pindirs(inst->pio_vid, inst->sm_video, pin, 1, true);

  // Conditionally load the video PIO program
  if (pio_can_add_program(inst->pio_vid, &fb_video_program)) {
    // Install PIO program
    inst->prog_offset_video = pio_add_program(inst->pio_vid, &fb_video_program);
  } else {
    printf("Unable to load video PIO program - video not enabled\n");
    return -1;
  }

  c = fb_video_program_get_default_config(inst->prog_offset_video);

  // Set up pin allocations
  sm_config_set_set_pins(&c, inst->sync_base_pin, 2);
  sm_config_set_out_pins(&c, inst->vga_green_pin, 1);
  sm_config_set_sideset_pins(&c, inst->vga_green_pin);

  // shift right, autopull, 32 bit threshold
  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  printf("fb_mono_init line: %d\n", __LINE__);

  // Load SM
  pio_sm_init(inst->pio_vid, inst->sm_video,
	      inst->prog_offset_video + fb_video_offset_start, &c);

  // compute clock divider - note that we use 2 pio clks/pixel
  float clk_div = (float)inst->sysclk/(2.0 * (float)inst->pix_clk);
  pio_sm_set_clkdiv(inst->pio_vid, inst->sm_video, clk_div);

  //printf("clk_div = %f\n", clk_div);

  // Release the SM
  pio_sm_set_enabled(inst->pio_vid, inst->sm_video, true);

}


volatile uint32_t irq_flag = 0;
// Contains the address of PIO_IRQ register (could be either PIO0 or PIO1)
io_rw_32 *irq_addr;

volatile uint32_t irq_count = 1;
volatile uint32_t irq_fb_addr = 0;

volatile uint32_t frame_count = 0;


// Contains cursor pattern
uint32_t cursor_planeA[16];
uint32_t cursor_planeB[16];
uint32_t fb_mono_cursor_x;
uint32_t fb_mono_cursor_y;
uint32_t old_cursor_x = -1;
uint32_t old_cursor_y = -1;
uint32_t aligned_overlay_color[4];
uint32_t cursor_sel_mask[16][4];
uint32_t new_fb_contents[16];

// Four overlay colors:
//   0 - transparent
//   1 - black/white
//   2 - black/white
//   3 - black/white
// Color select masks

#ifdef DO_COLOR_ALIGN
uint32_t cursor_overlay_color[4];
#endif
void fb_mono_set_overlay_color(uint32_t entry, uint32_t color) {
  if (entry == 0) return;
#ifdef DO_COLOR_ALIGN
  // Make overlay colors 16 bit, so that we can align them with the cursor
  cursor_overlay_color[entry] = color ? 0xffff : 0;
#else
  // Make overlay colors 32 bit, so we don't have to align them
  aligned_overlay_color[entry] = color ? 0xffffffff : 0;
#endif
}

void fb_mono_irq_isr(void) {
  uint32_t fb, pat;
  uint32_t result;
  uint32_t planeA, planeB;

  // Clear PIO request 0 bit
  *irq_addr = 1;

  // Set flag, should be cleared by user code
  irq_flag = 1;

  // Bump frame count;
  frame_count++;

  //sleep_us(7);

  // Setup for cursor compositing
  if (old_cursor_x != fb_mono_cursor_x) {
#ifdef DO_COLOR_ALIGN
    // Align colors to current offset
    for (uint32_t i = 1; i < 4; i++) {
      aligned_overlay_color[i] = (cursor_overlay_color[i] <<
				  (fb_mono_cursor_x & 0x7));
    }
#endif

    for (uint32_t i = 0; i < 16; i++) {
      // Get cursor pattern, align to current x bit offset
      planeA = cursor_planeA[i] << (fb_mono_cursor_x & 0x7);
      planeB = cursor_planeB[i] << (fb_mono_cursor_x & 0x7);
   
      // Generate cursor select masks
      cursor_sel_mask[i][0] = ~planeA & ~planeB;
      cursor_sel_mask[i][1] = ~planeA &  planeB;
      cursor_sel_mask[i][2] =  planeA & ~planeB;
      cursor_sel_mask[i][3] =  planeA &  planeB;
    }

    // Set cursor x to 8 pixel granularity
    // (i.e. write the LSB of the offset into the scan out buffer commands)
    *(uint8_t *)&(dma_ctl[cursor_wr_cmd_ptr].waddr) = fb_mono_cursor_x >> 3;
#ifdef READ_CURSOR_ACTIVE_TIME
    *(uint8_t *)&(dma_ctl[cursor_rd_cmd_ptr].raddr) = fb_mono_cursor_x >> 3;
#endif
  }

  // Do cursor compositing - must always do this, to reflect changes
  // in frame buffer contents
  for (uint32_t i = 0; i < 16; i++) {
#if 0
    // Use new fb contents, if cursor position has changed
    if ((old_cursor_x != fb_mono_cursor_x) ||
	(old_cursor_y != fb_mono_cursor_y)) {
      //fb = new_fb_contents[i];
      //fb = get_fb(fb_mono_cursor_x, fb_mono_cursor_y + i);
      // Do byte alignment
      //fb = fb >> (fb_mono_cursor_x & 0x8);
      fb = 0;
    } else {
      // Get fb contents from previous frame
      // Already byte aligned
      fb = cursor_rd_buf_ptr[i];
    }
#else
    fb = cursor_rd_buf_ptr[i];
    fb = fb >> (fb_mono_cursor_x & 0x8);
#endif

    // Update transparent color overlay value
    aligned_overlay_color[0] = fb;

    // Do cursor select masking
    result = 0;
    for (uint32_t j = 0; j < 4; j++) {
      result = result | (aligned_overlay_color[j] & cursor_sel_mask[i][j]);
    }  

    // update cursor overlay
    cursor_wr_buf_ptr[i] = result;

  }

  // Set cursor y
  // If y is zero, then must immediately start with cursor out,
  // then do scan out for the rest of the screen
  if (old_cursor_y != fb_mono_cursor_y) {
    if (fb_mono_cursor_y == 0) {
      cur_ctl[0].start = (uint32_t)&(dma_ctl[cursor_out_ptr]);
      cur_ctl[0].count = 0x20020000 - (16 << 2);
      cur_ctl[1].start = (uint32_t)&(dma_ctl[scan_out_ptr]);
    } else {
      cur_ctl[0].start = (uint32_t)&(dma_ctl[scan_out_ptr]);
      cur_ctl[0].count = 0x20020000 - ((fb_mono_cursor_y) << 2);
      cur_ctl[1].start = (uint32_t)&(dma_ctl[cursor_out_ptr]);
    }
  }

  // Update saved cursor pos
  old_cursor_x = fb_mono_cursor_x;
  old_cursor_y = fb_mono_cursor_y;

  // Call external routine, if active
  if (fb_mono_cb_addr != NULL) {
    (*fb_mono_cb_addr)();
  }


#if 0
  irq_count--;
  if (irq_count == 0) {
    irq_count = 2;
    set_fb_start(irq_fb_addr);
    irq_fb_addr += 128 * 20;
    if (irq_fb_addr > (32*1024*1024)) irq_fb_addr = 0;
  }
#endif
}


// Change starting address of frame buffer in PSRAM
void fb_mono_set_fb_start(uint32_t start_addr) {

  // Save address
  fb_base_addr = start_addr;

  // Change the reset value for the PS command buffer
  psram_hline(&_inst, &ps_cmd_buf_reset, 0, start_addr, _inst.hactive/32);

}


void fb_mono_set_cursor_pos(int32_t x_pos, int32_t y_pos) {
  uint32_t len = 16;

  // Don't set beyond screen boundaries
  if (y_pos < 0) {
    y_pos = 0;
  }

  if (x_pos < 0) {
    x_pos = 0;
  }

  if (y_pos >= _inst.vactive) {
    y_pos = _inst.vactive - 1;
  }

  if (x_pos >= _inst.hactive) {
    x_pos = _inst.hactive - 1;
  }

  fb_mono_cursor_x = (uint32_t)x_pos;
  fb_mono_cursor_y = (uint32_t)y_pos;

  // Generate cursor read PSRAM command buffer
#ifdef PACKED_FB
  uint32_t scanline_size = _inst.hactive/8;
#else
  uint32_t scanline_size = 1 << SCANLINE_POW;
#endif

  uint32_t cursor_rd_addr = ((fb_mono_cursor_y * scanline_size +
			      (fb_mono_cursor_x  >> 3)));

  // Offset by base addr
  cursor_rd_addr = cursor_rd_addr + fb_base_addr;
  
  // Setup PSRAM commands to read cursor data from FB
  for (int i = 0; i < 16; i++) {
    _hyperram_cmd_init(&ps_cursor_cmd_buf[i], 
		       &g_hram_all[_inst.sm_fb],
		       HRAM_CMD_READ,
		       cursor_rd_addr + (i * scanline_size), 1);
  }

}

// Set cursor to 0, 0 with default pattern
void fb_mono_cursor_en(void) {

}
  
// Set video paramters
void set_timing(uint32_t vid_mode) {

  if (vid_mode >= NUM_TIMING_MODES) {
#ifdef FB_MONO_DEBUG
    printf("Bad timing mode: %d, using default\n", vid_mode);
#endif
    vid_mode = PREFERRED_VID_MODE;
  }

  _inst.words_per_scanline = SCANLINE_WORDS;
  _inst.sysclk = hyperram_get_sysclk();
  _inst.pix_clk = _vga_timing[vid_mode].pix_clk;
  _inst.htotal = _vga_timing[vid_mode].htotal;
  _inst.hactive = _vga_timing[vid_mode].hactive;
  _inst.hfp = _vga_timing[vid_mode].hfp;
  _inst.hsync = _vga_timing[vid_mode].hsync;
  _inst.hbp = _vga_timing[vid_mode].hbp;
  _inst.hpol = _vga_timing[vid_mode].hpol;
  _inst.vtotal = _vga_timing[vid_mode].vtotal;
  _inst.vactive = _vga_timing[vid_mode].vactive;
  _inst.vfp = _vga_timing[vid_mode].vfp;
  _inst.vsync = _vga_timing[vid_mode].vsync;
  _inst.vbp = _vga_timing[vid_mode].vbp;
  _inst.vpol = _vga_timing[vid_mode].vpol;

}

#if 0
void cpu_frame() {
  h_events_t h;
  uint32_t active;
  uint32_t vs;
  uint32_t pix;
  io_rw_32 *stat = &_inst.pio_vid->fstat;


  for (int i = 0; i < _inst.vtotal; i++) {
    if (i < _inst.vfp + _inst.vsync + _inst.vbp) {
      active = 0;
    } else {
      active = 1;
    }

    vs = ((i > _inst.vfp) && (i < (_inst.vfp + _inst.vsync)));

    h_line(&h, &_inst, active, vs);

    //printf("line/stat: %d %08x\n", i, (uint32_t)*stat);
    
    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, h.bp);
    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, h.active);

    if (active) {
      for (int j = 0; j < (_inst.hactive/32); j++) {
	pix = i&1 ? 0xaaaaaaaa : 0x55555555;
	//pix = i&1 ? 0xcccccccc : 0x33333333;
	//pix = -1;
	pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, pix);
      }
    }

    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, h.fp);
    pio_sm_put_blocking(_inst.pio_vid, _inst.sm_video, h.sync);
  }
}
#endif

uint32_t fb_mono_init(uint32_t vid_mode) {

  // Skip initialization
  if (vid_mode == -1) {
    return -1;
  }

  // Set up hardware values
  // PIO 0 used by PSRAM
  // Get an SM to be used by video timing generator
  _inst.pio_vid = pio1;
  _inst.sm_video = pio_claim_unused_sm(pio1, false);

  // Fail if no SMs available
  if (_inst.sm_video == -1) {
#ifdef FB_MONO_DEBUG
    printf("No state machines available\n");
#endif
    return -1;
  }

  // PSRAM channel allocated for FB refresh
  _inst.pio_mem = g_hram_all[1].pio;
  _inst.sm_fb = g_hram_all[1].sm;

  // PSRAM channel allocated for processor access
  _inst.sm_proc = g_hram_all[0].sm;

  _inst.sync_base_pin = VGA_HSYNC_PIN;
  _inst.vsync_offset = 1;
  _inst.hsync_offset = 0;
  _inst.vga_green_pin = VGA_GREEN_PIN;

  // Test to see if PSRAM has been initialized
  if (hyperram_get_sysclk() == 0) {
    printf("Hyperram not initialized\n");
    return -1;
  }

  // Setup video timing values
  set_timing(vid_mode);

  // Setup video state machine, quit if can't setup
  if (fb_mono_pio_init(&_inst) == -1) return -1;

  ctrl_dma_chan = dma_claim_unused_channel(true);
  data_dma_chan = dma_claim_unused_channel(true);
  ps_read_dma_chan = dma_claim_unused_channel(true);
  inc_dma_chan = dma_claim_unused_channel(true);
  cur_inc_dma_chan = dma_claim_unused_channel(true);

  clear_dma(ctrl_dma_chan, data_dma_chan);
  clear_dma(ps_read_dma_chan, inc_dma_chan);
  clear_dma(cur_inc_dma_chan, cur_inc_dma_chan);

#ifdef FB_MONO_DEBUG
  printf("dma chan: %d %d %d %d %d\n",
	 ctrl_dma_chan, data_dma_chan,
	 ps_read_dma_chan, inc_dma_chan,
	 cur_inc_dma_chan);
#endif

  uint32_t cmd_ptr;
  cmd_ptr = gen_dma_buf(&_inst,
			ctrl_dma_chan,
			data_dma_chan,
			ps_read_dma_chan,
			inc_dma_chan,
			cur_inc_dma_chan,
			dma_ctl,
			scan_buf,
			&cmd_reload_read_addr);
		
  dma_init_chain(&_inst,
		 ctrl_dma_chan, 
		 data_dma_chan, 
		 ps_read_dma_chan,
		 inc_dma_chan, 
		 cur_inc_dma_chan,
		 dma_ctl, 
		 scan_buf);
		 

  // Add handler for video interrupt
  // We use PIO IRQ 0, which maps to system IRQ 7
  if (_inst.pio_vid == pio0) {
    irq_set_exclusive_handler(PIO0_IRQ_0, fb_mono_irq_isr);
    pio0_hw->inte0 = PIO_IRQ0_INTE_SM0_BITS;
    irq_set_enabled(PIO0_IRQ_0, true);
    // For interrupt handler
    irq_addr = (io_rw_32 *)(PIO0_BASE + PIO_IRQ_OFFSET);
  } else {
    irq_set_exclusive_handler(PIO1_IRQ_0, fb_mono_irq_isr);
    pio1_hw->inte0 = PIO_IRQ0_INTE_SM0_BITS;
    irq_set_enabled(PIO1_IRQ_0, true);
    // For interrupt handler
    irq_addr = (io_rw_32 *)(PIO1_BASE + PIO_IRQ_OFFSET);
  }


  return vid_mode;
}

// Assert/deassert system IRQ at given scanline
void fb_mono_irq_en(uint32_t line, uint32_t enable) {

  // Generate PIO instructions to set/clear system IRQ0 (i.e. irq nowait 0)
  uint32_t set_irq_inst = pio_encode_irq_set(0, 0);
  uint32_t nop_inst = pio_encode_nop();

  // Enable or disable IRQ generation
  uint32_t final_inst = enable ? set_irq_inst : nop_inst;

  // Modify vertical interrupt request fp instruction
  virq_buf.fp = virq_buf.fp & 0x0000ffff | (final_inst << 16);
}

// Wait for sync irq
void fb_mono_sync_wait(uint32_t line) {
  
  // Disable interrupt
  fb_mono_irq_en(line, 0);

  // Reset flag
  irq_flag = 0;

  // Enable interrupt at requested line
  fb_mono_irq_en(line, 1);

  // Stall until flag goes true
  while (irq_flag == 0) {
  }


#if 0
  // Turn off interrupt
  fb_mono_irq_en(line, 0);

  // Reset flag
  irq_flag = 0;

  // Enable interrupt at requested line
  fb_mono_irq_en(line, 1);

  // Stall until flag goes true
  while (irq_flag == 0) {
  }

  sleep_us(1);

  // Turn off interrupt
  fb_mono_irq_en(line, 0);
#endif
}

uint32_t fb_mono_vsync_callback(void *cb_addr) {
}


void put_pix(uint32_t x, uint32_t y, uint32_t color) {
  uint32_t pixels;

  // Position pixel within 16 bit word
  uint32_t pix_mask = 1 << (x & 0xf);

  // Color to write at position
  uint32_t exp_color = color ? pix_mask : 0;

  // Calculate pixel byte address (LSB is forced to zero when accessing PSRAM)
#ifdef PACKED_FB
  uint32_t pix_addr = (y * _inst.hactive/8) + (x >> 3);
#else
  uint32_t pix_addr = (y << (SCANLINE_POW)) + (x >> 3);
#endif

  // Offset by base addr
  pix_addr = pix_addr + fb_base_addr;

  // Do RMW to set/clear pixel
  // Note: we get 16 bit aligned 32 bit words from PSRAM
  hyperram_read_blocking(&g_hram_all[_inst.sm_proc], pix_addr, &pixels, 1);
  pixels = (pixels & ~pix_mask) | exp_color;
  hyperram_write_blocking(&g_hram_all[_inst.sm_proc], pix_addr, &pixels, 1);

}

// Note: uses video PSRAM port, so should use only during blanking time
uint32_t get_fb(uint32_t x, uint32_t y) {
  uint32_t pixels;
  hyperram_cmd_t cmd;

  // Calculate pixel byte address
#ifdef PACKED_FB
  uint32_t pix_addr = (y * _inst.hactive/8) + (x >> 3);
#else
  uint32_t pix_addr = (y << (SCANLINE_POW)) + (x >> 3);
#endif

  // Offset by base addr
  pix_addr = pix_addr + fb_base_addr;

  // Get pixel data
  hyperram_read_blocking(&g_hram_all[_inst.sm_proc], pix_addr, &pixels, 1);

  return pixels;
}

void draw_box(uint32_t x, uint32_t y, uint32_t size, uint32_t color) {
  uint32_t i, j;
  uint32_t next_x;

  for (i = 0; i < size; i++) {
    next_x = x;
    for (j = 0; j < size; j++) {
      put_pix(next_x++, y, color);
    }
    y++;
  }
}

/*
 * Bresenham algorithm
 */

#define abs(a) (a) > 0 ? (a) : -(a)
void draw_line (uint32_t x0in, uint32_t y0in, 
		uint32_t x1in, uint32_t y1in,
		uint32_t color) {

  // Convert to signed, so that we can add/subtract deltas
  int x0 = (int)x0in;
  int y0 = (int)y0in;
  int x1 = (int)x1in;
  int y1 = (int)y1in;

  int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
  int err = dx + dy, e2; /* error value e_xy */
 
  for (;;) {  /* loop */
    put_pix(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
    if (e2 <= dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
  }
}

void draw_hline(uint32_t x, uint32_t y, uint32_t size, uint32_t color) {
  uint32_t i, j;
  uint32_t next_x = x;

  for (i = 0; i < size; i++) {
    put_pix(next_x++, y, color);
  }
}

