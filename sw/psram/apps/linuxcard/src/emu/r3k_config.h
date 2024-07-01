#ifndef _R3K_CONFIG_H
#define _R3K_CONFIG_H

/******************/
/* Emulator config
/******************/

// RAM size in megabytes
#define EMULATOR_RAM_MB 32

// Image filename
#define IMAGE_FILENAME "0:linux.wheezy"

// Enable UART console
#define CONSOLE_UART 1

// Enable USB CDC console
#define CONSOLE_CDC 0

#if CONSOLE_UART

/******************/
/* UART config
/******************/

// UART instance
#define UART_INSTANCE uart1

// UART Baudrate (if enabled)
#define UART_BAUD_RATE 115200

// Pins for the UART (if enabled)
#define UART_TX_PIN 8
#define UART_RX_PIN 9

#endif

/****************/
/* SD card config
/***************/

// Set to 1 to use SDIO interface for the SD. Set to 0 to use SPI.
#define SD_USE_SDIO 1

#if SD_USE_SDIO

/****************/
/* SDIO interface
/****************/

// Pins for the SDIO interface (if used)
// CLK will be D0 - 2,  D1 = D0 + 1,  D2 = D0 + 2,  D3 = D0 + 3
#ifdef ORIG_CONFIG
#define SDIO_PIN_CMD 18
#define SDIO_PIN_D0 19
#endif
#define SDIO_PIN_CMD 11
#define SDIO_PIN_D0 12

#else

/*******************/
/* SD SPI interface
/******************/

// SPI instance used for SD (if used)
#define SD_SPI_INSTANCE spi1

// Pins for the SD SPI interface (if used)
#define SD_SPI_PIN_MISO 12
#define SD_SPI_PIN_MOSI 11
#define SD_SPI_PIN_CLK 10
#define SD_SPI_PIN_CS 15

#endif // SD_USE_SDIO

#endif // _R3K_CONFIG_H
