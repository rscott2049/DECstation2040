#include "hw_config.h"
#include "ff.h" /* Obtains integer types */

#include "r3k_config.h"

// Hardware Configuration of SPI "objects"
// Note: multiple SD cards can be driven by one SPI if they use different slave
// selects (or "chip selects").
static spi_t spis[] = {  // One for each RP2040 SPI component used
    {   // spis[0]
        .hw_inst = spi1,  // RP2040 SPI component
        .sck_gpio = 10,    // GPIO number (not Pico pin number)
        .mosi_gpio = 11,
        .miso_gpio = 12,
        .set_drive_strength = true,
	.mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
	.sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        //.no_miso_gpio_pull_up = true,
	.no_miso_gpio_pull_up = false,
        .DMA_IRQ_num = DMA_IRQ_0,
        // .baud_rate = 125 * 1000 * 1000 / 10 // 12500000 Hz
        // .baud_rate = 125 * 1000 * 1000 / 8  // 15625000 Hz
        // .baud_rate = 125 * 1000 * 1000 / 6  // 20833333 Hz
        // .baud_rate = 125 * 1000 * 1000 / 4  // 31250000 Hz
	//.baud_rate = 300 * 1000 * 1000 / 10  // 30 000 000Hz
	.baud_rate = 300 * 1000 * 1000 / 8  // 37 500 000 Hz
    },
};

/* SPI Interfaces */
static sd_spi_if_t spi_ifs[] = {
    {   // spi_ifs[0]
        .spi = &spis[0],  // Pointer to the SPI driving this card
        .ss_gpio = 15,     // The SPI slave select GPIO for this SD card
        .set_drive_strength = true,
        .ss_gpio_drive_strength = GPIO_DRIVE_STRENGTH_2MA
    },
};

/* SDIO Interfaces */
/*
Pins CLK_gpio, D1_gpio, D2_gpio, and D3_gpio are at offsets from pin D0_gpio.
The offsets are determined by sd_driver\SDIO\rp2040_sdio.pio.
    CLK_gpio = (D0_gpio + SDIO_CLK_PIN_D0_OFFSET) % 32;
    As of this writing, SDIO_CLK_PIN_D0_OFFSET is 30,
        which is -2 in mod32 arithmetic, so:
    CLK_gpio = D0_gpio -2.
    D1_gpio = D0_gpio + 1;
    D2_gpio = D0_gpio + 2;
    D3_gpio = D0_gpio + 3;
*/
static sd_sdio_if_t sdio_ifs[] = {
    {   // sdio_ifs[0]
        .CMD_gpio = 11,
        .D0_gpio = 12,
        //.CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
        .CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .CMD_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D0_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D1_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D2_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D3_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .SDIO_PIO = pio1,
        .DMA_IRQ_num = DMA_IRQ_0,
        //.baud_rate = 125 * 1000 * 1000 / 8  // 15625000 Hz
        //.baud_rate = 125 * 1000 * 1000 / 7  // 17857143 Hz
        //.baud_rate = 125 * 1000 * 1000 / 6  // 20833333 Hz
        //.baud_rate = 125 * 1000 * 1000 / 5  // 25000000 Hz
        //.baud_rate = 125 * 1000 * 1000 / 4  // 31 250 000 Hz

	//.baud_rate = 300 * 1000 * 1000 / 30  // 10 000 000Hz
	//.baud_rate = 300 * 1000 * 1000 / 15  // 20 000 000Hz
	//.baud_rate = 300 * 1000 * 1000 / 10  // 30 000 000Hz
	.baud_rate = 300 * 1000 * 1000 / 8  // 37 500 000Hz
    },
};

/* Hardware Configuration of the SD Card "objects"
    These correspond to SD card sockets
*/
static sd_card_t sd_cards[] = {  // One for each SD card
    {   // sd_cards[0]: Socket sd0
#if 0
        .type = SD_IF_SDIO,
        .sdio_if_p = &sdio_ifs[0], // Pointer to the interface driving this card
#else
        .type = SD_IF_SPI,
        .spi_if_p = &spi_ifs[0],  // Pointer to the SPI interface driving this c
#endif
        // SD Card detect:
        .use_card_detect = false,
    },
};

/* ********************************************************************** */

size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    assert(num < sd_get_num());
    if (num < sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}

/* [] END OF FILE */
