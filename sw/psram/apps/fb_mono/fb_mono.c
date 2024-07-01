#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "hyperram.h"
#include "fb_mono.h"
#include "fb_mono.pio.h"
#include "vga_timing.h"

// DMA routines

// Set buf to maximum video format
// Other formats will us proportionally smaller sections
#define DMA_BUF_LENGTH (768*(1024/32))
uint32_t dma_buf[DMA_BUF_LENGTH] __attribute__((aligned (16)));
uint32_t address_pointer[1];

uint32_t fb_address_pointer[1];

// Video horizontal events dma buffer
typedef struct {
	uint32_t bp;
	uint32_t active;
        uint32_t fp;
	uint32_t sync;
} h_events_t;

// Accomodate screen formats up to 1024 total lines
#define FORMAT_BUF_LEN (1024)
h_events_t h_events_dma_buf[FORMAT_BUF_LEN] __attribute__((aligned (16)));
uint32_t h_events_address_pointer[1];


uint32_t data_dma_chan;
uint32_t ctrl_dma_chan;

// DMA control blocks
typedef struct {
  uint32_t raddr;
  uint32_t waddr;
  uint32_t count;
  dma_channel_config cnfg;
} dma_ctl_blk;

// Up to one sync block, one PSRAM setup command
dma_ctl_blk dma_sync_ctl[FORMAT_BUF_LEN*2];

// For the control reload block:
uint32_t reload_read_addr = (uint32_t)&dma_sync_ctl[0];

// PSRAM command block
// Accomodate up to one PSRAM read per scan line
hyperram_cmd_t ps_fb_read[FORMAT_BUF_LEN];


void dump_dma_reg(uint32_t chan) {

  printf("DMA chan: %d\n", chan);
  printf("raddr: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].read_addr,
	 dma_hw->ch[chan].read_addr);
  printf("waddr: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].write_addr,
	 dma_hw->ch[chan].write_addr);
  printf("count: %08x %08x\n", (uint32_t)&dma_hw->ch[chan].transfer_count,
	 dma_hw->ch[chan].transfer_count);
  printf("ctl:   %08x %08x\n", (uint32_t)&dma_hw->ch[chan].ctrl_trig,
	 dma_hw->ch[chan].ctrl_trig);
}

void clear_dma(uint32_t dma_chan_transfer, uint32_t dma_chan_loop) {
    dma_channel_hw_addr(dma_chan_transfer)->al1_ctrl = 0;
    dma_channel_hw_addr(dma_chan_loop)->al1_ctrl = 0;
}

// Setup repeating dma for reading from scan line buffer and writing to
// PIO SM video output machine.
void dma_init_single(PIO pio, uint sm, uint32_t buf_size, uint32_t *ctl_ptr,
		     uint32_t data_ptr) {
  dma_channel_config ctrl_config;
  dma_channel_config data_config;
  uint32_t data_dma_chan;
  uint32_t ctrl_dma_chan;
  
  // Allocate DMA channels
  ctrl_dma_chan = dma_claim_unused_channel(true);
  data_dma_chan = dma_claim_unused_channel(true);

  clear_dma(ctrl_dma_chan, data_dma_chan);
  
  // Control channel setup
  ctrl_config = dma_channel_get_default_config(ctrl_dma_chan);

  // Don't increment read address
  channel_config_set_read_increment(&ctrl_config, false); 

  // Don't increment write address
  channel_config_set_write_increment(&ctrl_config, false); 

  // Set to trigger transfer channel
  channel_config_set_chain_to(&ctrl_config, data_dma_chan);

  // Make list of addresses to transfer from
  *ctl_ptr = data_ptr;

  dma_channel_configure(ctrl_dma_chan, &ctrl_config, 
			&dma_hw->ch[data_dma_chan].read_addr,
			ctl_ptr,
			1, // Number of transfers
			false);

  // Data channel setup
  data_config = dma_channel_get_default_config(data_dma_chan);

  // Tell DMA to let the PIO request data (i.e. data to the TX FIFO)
  channel_config_set_dreq(&data_config, pio_get_dreq(pio, sm, true));

  // Set to trigger looping channel
  channel_config_set_chain_to(&data_config, ctrl_dma_chan);

  dma_channel_configure(data_dma_chan, &data_config,
  			&pio->txf[sm],
			(uint32_t *)data_ptr,
			buf_size,
			false);

  // start the control channel
  // This will kickoff the data transfer, and chain to the control channel
  dma_start_channel_mask(1u << ctrl_dma_chan);

}

// Setup repeating DMA from PSRAM output FIFO to scan line buffer
void dma_init_psram(PIO pio, uint sm, uint32_t buf_size, uint32_t *ctl_ptr, uint32_t data_ptr) {

  dma_channel_config ctrl_config;
  dma_channel_config data_config;
  uint32_t data_dma_chan;
  uint32_t ctrl_dma_chan;
  
  // Allocate DMA channels
  ctrl_dma_chan = dma_claim_unused_channel(true);
  data_dma_chan = dma_claim_unused_channel(true);

  clear_dma(ctrl_dma_chan, data_dma_chan);
  
  // Control channel setup
  ctrl_config = dma_channel_get_default_config(ctrl_dma_chan);

  // Don't increment read address
  channel_config_set_read_increment(&ctrl_config, false); 

  // Don't increment write address
  channel_config_set_write_increment(&ctrl_config, false); 

  // Set to trigger transfer channel
  channel_config_set_chain_to(&ctrl_config, data_dma_chan);

  // Make list of addresses to transfer from
  *ctl_ptr = data_ptr;

  dma_channel_configure(ctrl_dma_chan, &ctrl_config, 
			&dma_hw->ch[data_dma_chan].read_addr,
			ctl_ptr,
			1, // Number of transfers
			false);

  // Data channel setup
  data_config = dma_channel_get_default_config(data_dma_chan);

  // Tell DMA to let the SM send data (i.e. data from the RX FIFO)
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

  // Set to trigger looping channel
  channel_config_set_chain_to(&data_config, ctrl_dma_chan);

  dma_channel_configure(data_dma_chan, &data_config,
			(uint32_t *)data_ptr,
  			&pio->rxf[sm],
			buf_size,
			false);

  // start the control channel
  // This will kickoff the data transfer, and chain to the control channel
  dma_start_channel_mask(1u << ctrl_dma_chan);
}


// Setup dma channels
// This sets up a general purpose DMA machine: 4 word control blocks, and
// arbitrary data blocks
// The data channel will transfer from the read address to the write
// address specified in the control block until it completes.
// The control channel will then program a new set of addresses/length/
// control words, and re-triggers the data channel when the control word
// is written.
//
// Note that the data channel blocks must contain the trigger to the
// control dma channel in the DMA control register contents.


void dma_init(uint32_t ctrl_chan, uint32_t data_chan, uint32_t *ctrl_ptr) {
  dma_channel_config ctrl_config;
  
  // The control channel transfers four words into the data channel's control
  // registers, then halts. The control channel write address wraps on a
  // four-word (16-byte) boundary, so that the control channel writes the
  // same four registers when it is next triggered.

  dma_channel_config c = dma_channel_get_default_config(ctrl_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, true);
  channel_config_set_ring(&c, true, 4); // 1 << 4 byte boundary on write ptr

  dma_channel_configure
    (
     ctrl_chan,
     &c,
     &dma_hw->ch[data_chan].read_addr,      // Initial write address
     ctrl_ptr,                              // Initial read address
     4,                                     // Halt after each block
     true                                   // fire it off
     );

}


// Horizontal timing events generator - formats commmands
// for the sync timing generator PIO SM
// Events are (in order):
// 0) back porch (after sync deassertion)
// 1) active (display pixels)
// 2) front porch (before sync assertion
// 3) sync

// PIO instructions to set/clear IRQ5 (irq nowait 5)
#define SET_IRQ5_INST 0xC005

// PIO NOP instruction (mov Y, Y)
#define NOP_INST 0xA042

void h_line(h_events_t *t, const fb_mono_inst_t *inst, uint32_t active, uint32_t vs) {
  uint32_t active_cmd;
  uint32_t vsync, hsync_assert, hsync_deassert;

  // Determine actual vsync, hsync assertion levels for commands:
  // Input vsync is always asserted high
  vsync = (~inst->vsync_assert_polarity ^ vs) & 0x01;
  hsync_assert = inst->hsync_assert_polarity & 0x01;
  hsync_deassert = (~inst->hsync_assert_polarity) & 0x01;
  
  // Position syncs in PIO command word:
  vsync = vsync << (inst->vsync_offset + 16);
  hsync_assert = hsync_assert << (inst->hsync_offset + 16);
  hsync_deassert = hsync_deassert << (inst->hsync_offset + 16);

  // If active, signal video SM to output pixels
  if (active == 1) {
    active_cmd = SET_IRQ5_INST;
  } else {
    active_cmd = NOP_INST;
  }

  // Setup back porch (i.e. event just after hsync assertion)
  t->bp = NOP_INST | vsync | hsync_deassert | ((inst->hbp - 2) << 18);

  // Setup active command
  t->active = active_cmd | vsync | hsync_deassert | ((inst->hactive - 2) << 18);

  // Setup front porch command
  t->fp = NOP_INST | vsync | hsync_deassert | ((inst->hfp - 2) << 18);

  // Setup sync command
  t->sync = NOP_INST | vsync | hsync_assert | ((inst->hsync - 2) << 18);

}


void fb_mono_pio_sync_init(const fb_mono_inst_t *inst) {

  // initialize vga sync pins
  pio_gpio_init(inst->pio, inst->sync_base_pin);
  pio_gpio_init(inst->pio, inst->sync_base_pin + 1);

  // Make them outputs
  pio_sm_set_consecutive_pindirs(inst->pio, inst->sm_sync, inst->sync_base_pin,
				 2, true);


  pio_sm_config c = fb_sync_program_get_default_config(inst->prog_offset_sync);
  sm_config_set_out_pins(&c, inst->sync_base_pin, 2);
  sm_config_set_set_pins(&c, inst->vga_green_pin, 1);
  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  // Release the SM
  pio_sm_init(inst->pio, inst->sm_sync, inst->prog_offset_sync, &c);
  pio_sm_set_enabled(inst->pio, inst->sm_sync, true);

}

void fb_mono_pio_video_init(const fb_mono_inst_t *inst) {

  // Video output pin
  uint32_t pin = inst->vga_green_pin;

  // Init video out pin 
  pio_gpio_init(inst->pio, pin);

  // Set slew rate/drive strength
#if 0
  gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_8MA);
#endif
  
  // Make it an output
  pio_sm_set_consecutive_pindirs(inst->pio, inst->sm_video, pin, 1, true);

  pio_sm_config c = fb_video_program_get_default_config(inst->prog_offset_video);
  sm_config_set_out_pins(&c, pin, 1);
  // shift left, no autopull, 32 bit threshold
  sm_config_set_out_shift(&c, true, false, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  // Release the SM
  pio_sm_init(inst->pio, inst->sm_video, inst->prog_offset_video, &c);
  pio_sm_set_enabled(inst->pio, inst->sm_video, true);

}

volatile uint32_t irq_flag = 0;
io_rw_32 *irq_addr;

void fb_mono_irq_isr(void) {
  // Clear request bit
  *irq_addr = 1;
  // Set flag, should be cleared by user code
  irq_flag = 1;
}

// Generate DMA command blocks per horizontal line
// If line is not active, this is just the hsync event block
// If line is active, add the PSRAM read and subsequent copy to SRAM events
// We do the first active PSRAM fetch during the last vertical bp line
// Returns next dma block
uint32_t setup_dma_hline(fb_mono_inst_t *inst,
			 uint32_t curr_line, uint32_t curr_blk) {

  // For 1 bpp:
  uint32_t words_per_scanline = inst->hactive/32;
  uint32_t bytes_per_scanline = inst->hactive/8;

  // Determine when to start PSRAM fetches
  uint32_t active_line_start = inst->vbp;
  uint32_t active_line_end = inst->vbp + inst->vactive;
  int32_t pre_line = curr_line - 1;
  uint32_t pre_active = ((pre_line >= active_line_start) && 
			 (pre_line < active_line_end));

  int32_t active_line = pre_line - active_line_start;

  
  // Determine when the DMA list is done
  uint32_t last_blk = (curr_line == (inst->vtotal - 1));

  // For the hsync event setup:
  // We will be writing to the data channel's control register
  // via the control DMA engine
  dma_channel_config c = dma_channel_get_default_config(data_dma_chan);

  // Increment read address, since we're writing to a SM input FIFO
  channel_config_set_read_increment(&c, true); 

  // Don't increment write address (it's the timing SM FIFO input)
  channel_config_set_write_increment(&c, false); 

  // Tell DMA to let the SM request data
  channel_config_set_dreq(&c, pio_get_dreq(inst->pio, inst->sm_sync, true));

  // Set to trigger the control channel
  channel_config_set_chain_to(&c, ctrl_dma_chan);

  dma_channel_config to_fifo = c;

  // Write a PSRAM read control block sequence
  if (pre_active) {
    // Setup PSRAM read one scan line command (note that length is in words)
    // Address is in bytes, length in words
    _hyperram_cmd_init(&ps_fb_read[active_line], &g_hram_all[1], HRAM_CMD_READ,
		       active_line * bytes_per_scanline, words_per_scanline);

    dma_sync_ctl[curr_blk].raddr = (uint32_t)&ps_fb_read[active_line];
    dma_sync_ctl[curr_blk].waddr = (uint32_t)&inst->pio_fb->txf[inst->sm_fb];
    dma_sync_ctl[curr_blk].count = sizeof(hyperram_cmd_t)/sizeof(uint32_t);
    dma_sync_ctl[curr_blk].cnfg = to_fifo;
    curr_blk++;
  }

  // Now write the hevents control block:
  dma_sync_ctl[curr_blk].raddr = (uint32_t)&h_events_dma_buf[curr_line];
  dma_sync_ctl[curr_blk].waddr = (uint32_t)&inst->pio->txf[inst->sm_sync];
  dma_sync_ctl[curr_blk].count = sizeof(h_events_t)/sizeof(uint32_t);
  dma_sync_ctl[curr_blk].cnfg = to_fifo;
  curr_blk++;

  // Setup the reload control block
  // Write to the read address (alias 3) to restart the control DMA sequence
  // Writing to the controller's read address (alias 3) will trigger
  // the first data DMA
  // Defaults to 32 bit wide xfer, unpaced, no write inc, read inc, no chain
  if (last_blk) {
    c = dma_channel_get_default_config(data_dma_chan);
    // Write the reload control block
    dma_sync_ctl[curr_blk].raddr = (uint32_t)&reload_read_addr;
    dma_sync_ctl[curr_blk]. waddr = (uint32_t)&dma_hw->ch[ctrl_dma_chan].al3_read_addr_trig;
    dma_sync_ctl[curr_blk].count = 1;
    dma_sync_ctl[curr_blk].cnfg = c;
    curr_blk++;
  }

  return curr_blk;
}



  dma_channel_config c;

    // Config for copying from PSRAM to memory
    c = dma_channel_get_default_config(psram_data_dma_chan);

    // Don't increment read address
    channel_config_set_read_increment(&c, false); 

    // Increment write address
    channel_config_set_write_increment(&c, true); 


    // Set to trigger the control channel
    channel_config_set_chain_to(&c, ctrl_dma_chan);

    uint32_t dest_line = (pre_line & 0x01) *  words_per_scanline;
    dma_sync_ctl[curr_blk].raddr = (uint32_t)&inst->pio_fb->rxf[inst->sm_fb];
    dma_sync_ctl[curr_blk].waddr = (uint32_t)&dma_buf[dest_line];
    dma_sync_ctl[curr_blk].count = words_per_scanline;
    dma_sync_ctl[curr_blk].cnfg = c;
    curr_blk++;




uint32_t *fb_mono_init(fb_mono_inst_t *inst) {
  uint32_t buf_size;
  uint32_t blk_num;
  struct pio_program modified_video_program;
  uint16_t modified_video_instructions[32];
  
  inst->prog_offset_sync = pio_add_program(inst->pio, &fb_sync_program);
  fb_mono_pio_sync_init(inst);
  // compute clock divider - note that we use 4 pio clks/pixel
  float clk_div = (float) (300000000.0/(4.0 * (float)inst->pix_clk));
  pio_sm_set_clkdiv(inst->pio, inst->sm_sync, clk_div);
  
  // Setup timing DMA control blocks
  dma_channel_config config;

  // Allocate DMA channels for the DMA machine
  ctrl_dma_chan = dma_claim_unused_channel(true);
  data_dma_chan = dma_claim_unused_channel(true);

  clear_dma(ctrl_dma_chan, data_dma_chan);

  // Generate a screen's worth of video timing state machine commands
  // and PSRAM fetch/copy to scan line buffer
  // Vertical events:
  uint32_t active_line_start = inst->vbp;
  uint32_t active_line_end = inst->vbp + inst->vactive;
  uint32_t sync_line_start = active_line_end + inst->vfp;
  uint32_t sync_line_end = sync_line_start + inst->vsync;
  uint32_t curr_blk = 0;

  for (int i = 0; i < inst->vtotal; i++) {
    // Determine if we're in active portion of the screen or not
    uint32_t active = (i >= active_line_start) && (i < active_line_end);

    // Determine if we're in vertical sync interval or not
    uint32_t sync = (i >= sync_line_start) && (i < sync_line_end);

    // Generate sync block
    h_line(&h_events_dma_buf[i], inst, active, sync);

    // Generate PSRAM command and put together with sync blk
    // into DMA request stream
    curr_blk = setup_dma_hline(inst, i, curr_blk);
  }


  // Add handler for video interrupt
  // We use IRQ 0
  if (inst->pio == pio0) {
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

  // Setup timing DMA control blocks
  // Block 0: Do for all scanlines:
  //   Write to timing SM
  //   Read from timing buffer
  // Block 1: reset control DMA
  //   Write to control DMA
  //   Read from control blocks




  // Fire off the sync DMA:
  dma_init(ctrl_dma_chan, data_dma_chan, (uint32_t *)&dma_sync_ctl[0]);

#if 0
  printf("ctrl chan after\n");
  dump_dma_reg(ctrl_dma_chan);
  printf("data chan after\n");
  dump_dma_reg(data_dma_chan);
  sleep_ms(16);
  printf("sleep ctrl chan after\n");
  dump_dma_reg(ctrl_dma_chan);
  printf("sleep data chan after\n");
  dump_dma_reg(data_dma_chan);

  //  while (!(dma_hw->intr & 1u << data_dma_chan)) {
  printf("wait: %08x\n", dma_hw->ch[data_dma_chan].ctrl_trig & 0x01000000);
  while ((dma_hw->ch[data_dma_chan].ctrl_trig & 0x01000000) == 0x01000000) {
    tight_loop_contents();
  }
  printf("wait: %08x\n", dma_hw->ch[data_dma_chan].ctrl_trig & 0x01000000);

  printf("wait ctrl chan after\n");
  dump_dma_reg(ctrl_dma_chan);
  printf("wait data chan after\n");
  dump_dma_reg(data_dma_chan);
#endif

  // Have to copy assember output in order to do active time modification
  // Apparently, casting away constness is a bad idea
  for (int i = 0; i < fb_video_program.length; i++) {
    modified_video_instructions[i] = fb_video_program_instructions[i];
  }

  // Overwrite active time with appropriate number of 32 pixel groups per line
  // Original PIO instruction is: mov X, <value> or 0xe020 <value = 0>
  modified_video_instructions[fb_video_offset_set_active] =  0xe020 | ((inst->hactive/32 - 1) & 0x1f);

  // Finalize updated PIO program structure
  modified_video_program.instructions = (const uint16_t *)modified_video_instructions;
  modified_video_program.length = fb_video_program.length;
  modified_video_program.origin = fb_video_program.origin;

  // Setup video PIO state machine
  //  inst->prog_offset_video = pio_add_program(inst->pio, &fb_video_program);
  inst->prog_offset_video = pio_add_program(inst->pio, &modified_video_program);
  fb_mono_pio_video_init(inst);
  pio_sm_set_clkdiv(inst->pio, inst->sm_video, clk_div);

  // Setup PSRAM to scan line buffer DMA
  buf_size = (inst->hactive * 2)/32;
  dma_init_psram(inst->pio_fb, inst->sm_fb, buf_size,
		  fb_address_pointer,
		  (uint32_t)&dma_buf[0]);


  // Setup video DMA. Video will display when timing SM requests pixels.
  // Full screen
  //buf_size = (inst->hactive * inst->vactive)/32;
  // two scan line buffer
  buf_size = (inst->hactive * 2)/32;
  dma_init_single(inst->pio, inst->sm_video, buf_size,
		  address_pointer,
		  (uint32_t)&dma_buf[0]);

  printf("Video SM initialized\n");

  return dma_buf;
}

// Assert/deassert system IRQ at given scanline
// PIO instructions to set/clear IRQ7 (irq nowait 0)
#define SET_IRQ0_INST 0xC000
#define CLR_IRQ0_INST 0xC040

void fb_mono_irq_en(uint32_t line, uint32_t enable) {
  
  // Get non-instruction portion of events
  uint32_t mod_bp = h_events_dma_buf[line].bp & 0xffff0000;
  uint32_t mod_fp = h_events_dma_buf[line].fp & 0xffff0000;

  if (enable) {
    // Change PIO NOP horizontal event commands to set/clear system IRQ
    // IRQ asserted at beginning of line, and deasserted at end of active pixels
    h_events_dma_buf[line].bp = mod_bp | SET_IRQ0_INST;
    h_events_dma_buf[line].fp = mod_fp | CLR_IRQ0_INST;
  } else {
    // Reset events
    h_events_dma_buf[line].bp = mod_bp | NOP_INST;
    h_events_dma_buf[line].fp = mod_fp | NOP_INST;
  }    
}

// Wait for sync irq
void fb_mono_sync_wait(uint32_t line) {

  // Enable interrupt at requested line
  fb_mono_irq_en(line, 1);
  // Reset flag
  irq_flag = 0;

  // Stall until flag goes true
  while (irq_flag == 0) {
  }
}

uint32_t curr_pix_count = 0;
uint32_t report = 32;
uint32_t frame = 0;
uint32_t first_act = 0;
uint32_t s_report = 32;



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

  if ((first_act == 0) && (active)) first_act = curr_line;

  h_line(&t, inst, active, sync);

#if 0
  printf("fp: %08x\n", t.fp);
  printf("at: %08x\n", t.active);
  printf("bp: %08x\n", t.bp);
  printf("sy: %08x\n", t.sync);
#endif    


  // Send commands to sync/blank SM
  pio_sm_put_blocking(inst->pio, inst->sm_sync, t.bp);
  pio_sm_put_blocking(inst->pio, inst->sm_sync, t.active);
  pio_sm_put_blocking(inst->pio, inst->sm_sync, t.fp);
  pio_sm_put_blocking(inst->pio, inst->sm_sync, t.sync);

#if 1
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
      pio_sm_put_blocking(inst->pio, inst->sm_video, pixels[i]);
    }
  }
#endif

#if 0
  // Check to see if pixels can be pushed to video SM
  while (pio_sm_is_tx_fifo_full(inst->pio, inst->sm_video) == false) {
    pio_sm_put(inst->pio, inst->sm_video, pixels[curr_pix_count++]);
    // Advance to next pixel group
    if (report > 0) {
      printf("first/act/frame/line/count: %d %d %d %d %d\n", first_act, active, frame, curr_line, curr_pix_count);
      report--;
    }
    if (curr_pix_count == inst->hactive/32) curr_pix_count = 0;
  }
#endif
  
  // Compute next scan line
  next_line = curr_line + 1;
  if (next_line >= sync_line_end) {
    curr_pix_count = 0;
    next_line = 0;
    frame++;
    if ((report == 0) && (frame < 2)) report = 16;

  }

  return next_line;
  
}

#if 0
// Deprecated - interferes with DMA driven FB
// Output a 100 kHz square wave on hsync
void fb_mono_calib(fb_mono_inst_t *inst) {
  uint32_t half_period;
  uint32_t hsync_high;
  uint32_t hsync_low;

  // Calcuate 100 kHz half period in pixel intervals
  half_period = (inst->pix_clk /100000) / 2;

  printf("Calib half period: %d\n", half_period);
  
  half_period -= 2;

  printf("Adjusted half period: %d\n", half_period);

  // Set pixel clock
  fb_mono_init(inst);

  // Get position of hsync, make high bit
  hsync_high = 1 << (inst->hsync_offset + 16);

  // Make hsync high for half a period
  hsync_high |= (half_period << 18) | NOP_INST;

  // Make hsync low for half a period
  hsync_low = (half_period << 18) | NOP_INST;

  while(1) {
    pio_sm_put_blocking(inst->pio, inst->sm_sync, hsync_high);
    pio_sm_put_blocking(inst->pio, inst->sm_sync, hsync_low);
  }  
}
#endif
