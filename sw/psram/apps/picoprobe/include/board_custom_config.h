/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef BOARD_CUSTOM_H_
#define BOARD_CUSTOM_H_

/* Direct connection - SWCLK/SWDIO on two GPIOs */
#define PROBE_IO_RAW

/* Include CDC interface to bridge to target UART. Omit if not used. */
#define PROBE_CDC_UART

#define PROBE_SM 0
// Use emu board 3v3 I/Os for SWD: J7, select GPIO16/17 via jumpers 1 & 2
//#define PROBE_PIN_OFFSET 16

// Use emu board 3v3 I/Os for SWD: GPIO13/14 (aka: SD_CMD1, SD_CMD2)
//#define PROBE_PIN_OFFSET 13

// Use emu board 1v8 I/Os for SWD: GPIO8/9
#define PROBE_PIN_OFFSET 8

/* PIO config for PROBE_IO_RAW */
#if defined(PROBE_IO_RAW)
#define PROBE_PIN_SWCLK (PROBE_PIN_OFFSET + 0)
#define PROBE_PIN_SWDIO (PROBE_PIN_OFFSET + 1)
#endif

#if defined(PROBE_CDC_UART)
#define PICOPROBE_UART_TX 16
#define PICOPROBE_UART_RX 17

#define PICOPROBE_UART_INTERFACE uart0
#define PICOPROBE_UART_BAUDRATE 115200
#endif

/* LED config - some or all of these can be omitted if not used */
//#define PICOPROBE_USB_CONNECTED_LED 2
//#define PICOPROBE_DAP_CONNECTED_LED 15
//#define PICOPROBE_DAP_RUNNING_LED 16
//#define PICOPROBE_UART_RX_LED 7
//#define PICOPROBE_UART_TX_LED 8

#define PROBE_PRODUCT_STRING "Custom board as Pico Probe"

#endif
