#include <stdio.h>
//
#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "pico/stdlib.h"
//
#include "command.h"
#include "crash.h"
#include "f_util.h"
#include "hw_config.h"
#include "rtc.h"
#include "tests.h"
#include "sd_card.h"
//
#include "diskio.h" /* Declarations of disk functions */

#ifndef USE_PRINTF
#error This program is useless without standard input and output.
#endif

static volatile bool card_det_int_pend;
static volatile uint card_det_int_gpio;

static void process_card_detect_int() {
    card_det_int_pend = false;
    for (size_t i = 0; i < sd_get_num(); ++i) {
        sd_card_t *sd_card_p = sd_get_by_num(i);
        if (!sd_card_p)
            continue;
        if (sd_card_p->card_detect_gpio == card_det_int_gpio) {
            if (sd_card_p->state.mounted) {
                DBG_PRINTF("(Card Detect Interrupt: unmounting %s)\n", sd_get_drive_prefix(sd_card_p));
                FRESULT fr = f_unmount(sd_get_drive_prefix(sd_card_p));
                if (FR_OK == fr) {
                    sd_card_p->state.mounted = false;
                } else {
                    printf("f_unmount error: %s (%d)\n", FRESULT_str(fr), fr);
                }
            }
            sd_card_p->state.m_Status |= STA_NOINIT;  // in case medium is removed
            sd_card_detect(sd_card_p);
        }
    }
}

// If the card is physically removed, unmount the filesystem:
static void card_detect_callback(uint gpio, uint32_t events) {
    // This is actually an interrupt service routine!
    card_det_int_gpio = gpio;
    card_det_int_pend = true;
}

int main() {
    crash_handler_init();

#if 1
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);

  uint32_t pass = set_sys_clock_khz(300 * 1000, true);
  sleep_ms(1000);

  stdio_init_all();
  sleep_ms(3000);
  if (pass) {
    printf("Set sysclk to 300 MHz\n");
  } else {
    printf("Did not set sysclk to 300 MHz\n");
  }
#else
  stdio_init_all();  
  sleep_ms(3000);
#endif

  // Set 1.8v threshold for I/O pads

  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  printf("before set: pad bank0 voltage select data: %08lx\n", (uint32_t)*addr);
#if 1 
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;

  printf("after  set: pad bank0 voltage select data: %08lx\n", (uint32_t)*addr);
#endif


    setvbuf(stdout, NULL, _IONBF, 1);  // specify that the stream should be unbuffered
    time_init();

    //printf("\033[2J\033[H");  // Clear Screen

    // Check fault capture from RAM:
    crash_info_t const *const pCrashInfo = crash_handler_get_info();
    if (pCrashInfo) {
        printf("*** Fault Capture Analysis (RAM): ***\n");
        int n = 0;
        do {
            char buf[256] = {0};
            n = dump_crash_info(pCrashInfo, n, buf, sizeof(buf));
            if (buf[0]) printf("\t%s", buf);
        } while (n != 0);
    }
    printf("\n> ");
    stdio_flush();

    // Implicitly called by disk_initialize,
    // but called here to set up the GPIOs
    // before enabling the card detect interrupt:
    sd_init_driver();

    for (size_t i = 0; i < sd_get_num(); ++i) {
        sd_card_t *sd_card_p = sd_get_by_num(i);
        if (!sd_card_p) 
            continue;
        if (sd_card_p->use_card_detect) {
            // Set up an interrupt on Card Detect to detect removal of the card
            // when it happens:
            gpio_set_irq_enabled_with_callback(
                sd_card_p->card_detect_gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                true, &card_detect_callback);
        }
    }

    for (;;) {  // Super Loop
        if (logger_enabled &&
            absolute_time_diff_us(get_absolute_time(), next_log_time) < 0) {
            if (!process_logger()) logger_enabled = false;
            next_log_time = delayed_by_ms(next_log_time, period);
        }
        if (card_det_int_pend)
            process_card_detect_int();
        int cRxedChar = getchar_timeout_us(0);
        /* Get the character from terminal */
        if (PICO_ERROR_TIMEOUT != cRxedChar)
            process_stdio(cRxedChar);
    }
    return 0;
}
