#include <pico/stdlib.h>
#include "pico/sync.h"
#include <stdint.h>
#include "usart.h"
#include <stdio.h>

struct repeating_timer timerChar;

bool checkchar_callback(struct repeating_timer *t) {

  uint32_t c = getchar_timeout_us(0);

  if (c != PICO_ERROR_TIMEOUT) {
    usartExtRx(c);
  }

  return true;
}

void usartInit(void) {
  stdio_init_all();
  //sleep_ms(3000);

  // -50MS so that the timer will repeat 500 per sec, regardless of how
  // long the callback takes to execute
  add_repeating_timer_ms(-50, checkchar_callback, NULL, &timerChar);
}  

void usartSetBuadrate(uint32_t baud) {

}

void usartTx(uint8_t ch) {
  putchar(ch);
}

void usartTxEx(uint8_t channel, uint8_t ch) {
}

