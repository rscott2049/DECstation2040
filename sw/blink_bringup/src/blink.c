/**
 * LED blink test
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/interp.h"

// Test pins for input
//#define INPUT
// Which pin to use for stimulus output
// J5, pin 6 - HSYNC pin on rev 1.3 and above
#define TOGGLE_PIN 7

// Test output only
#define OUTPUT
// Just run with blink delays, but no pins
//uint LED_PIN = 0;
//uint NUM_PINS = 0;

// Random pin testing
//uint LED_PIN = 13;
//uint NUM_PINS = 2;

// Test the SD GPIOs
//uint LED_PIN = 10;
//uint NUM_PINS = 6;

// Test the Enet GPIOs (except for RETCLK)
//uint LED_PIN = 0;
//uint NUM_PINS = 8;

// Test RETCLK
uint LED_PIN = 21;
uint NUM_PINS = 1;

// Test UART pins
//uint LED_PIN = 16;
//uint NUM_PINS = 2;

// Test video header pins
//uint LED_PIN = 7;
//uint NUM_PINS = 3;

// Test J6 header GPIO pins
//uint LED_PIN = 8;
//uint NUM_PINS = 2;


// Test PSRAM data pins
//uint LED_PIN = 22;
//uint NUM_PINS = 8;

// Test PSRAM ctl pins
//uint LED_PIN = 18;
//uint NUM_PINS = 3;

// Test all pins (no PSRAM installed)
//uint LED_PIN = 0;
//uint NUM_PINS = 30;


void setup() {
  for (int i = 0; i < NUM_PINS; i++) {
    gpio_init(LED_PIN + i);
    gpio_set_dir(LED_PIN + i, GPIO_OUT);
  }
}

void input_setup() {
  gpio_init(TOGGLE_PIN);
  gpio_set_dir(TOGGLE_PIN, GPIO_OUT);

  for (int i = 0; i < NUM_PINS; i++) {
    gpio_init(LED_PIN + i);
    gpio_set_dir(LED_PIN + i, GPIO_IN);
    gpio_set_pulls(LED_PIN + i, false, true);
  }
}


void blink() {
  for (int i = 0; i < NUM_PINS; i++) {
      gpio_put(LED_PIN + i, i & 1);
  }
  sleep_ms(1);
  for (int i = 0; i < NUM_PINS; i++) {
    gpio_put(LED_PIN + i, !(i & 1));
  }
  sleep_ms(1);
}

uint32_t input_scan() {
  uint32_t result_one = 0;
  uint32_t result_zero = 0;

  // Set stimulus pin
  gpio_put(TOGGLE_PIN, 1);
  sleep_ms(1);
  for (int i = 0; i < NUM_PINS; i++) {
    result_one |= (gpio_get(LED_PIN + i) & 0x01) << i;
  }

  // Clear stimulus pin
  gpio_put(TOGGLE_PIN, 0);
  sleep_ms(1);
  for (int i = 0; i < NUM_PINS; i++) {
    result_zero |= (!gpio_get(LED_PIN + i) & 0x01) << i;
  }

  //  return (result_one & result_zero);
  return(result_one);
}
 
int main() {
  uint32_t tick;
  uint32_t count = 0;

  stdio_init_all();
  sleep_ms(1000);

#ifdef OUTPUT
  setup();
  tick = 500;

  while (1) {
    blink();
    if (--tick == 0) {
      printf("Hello, world! %08x\n", count++);
      tick = 500;
    }
  }
#endif

#ifdef INPUT
  uint32_t results = 0;

  printf("Input testing, starting at pin: %d\n", LED_PIN);
  input_setup();
  tick = 250;

  while (1) {
    results = input_scan();
    if (--tick == 0) {
      if (results) {
	printf("%08d Results: %08x\n", count++, results);
	tick = 250;
      }
    }
  }
#endif


  return 0;
}
