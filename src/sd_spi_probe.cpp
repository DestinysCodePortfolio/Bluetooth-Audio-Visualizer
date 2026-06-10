#include "sd_spi_probe.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SD_SPI_PORT spi1

#define SD_MISO_PIN 12
#define SD_CS_PIN   13
#define SD_SCK_PIN  14
#define SD_MOSI_PIN 15

static uint8_t spi_txrx(uint8_t data) {
    uint8_t rx = 0xFF;
    spi_write_read_blocking(SD_SPI_PORT, &data, &rx, 1);
    return rx;
}

static void sd_select(void) {
    gpio_put(SD_CS_PIN, 0);
}

static void sd_deselect(void) {
    gpio_put(SD_CS_PIN, 1);
    spi_txrx(0xFF);
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    sd_deselect();
    sleep_ms(1);

    sd_select();
    spi_txrx(0xFF);

    spi_txrx(0x40 | cmd);
    spi_txrx((arg >> 24) & 0xFF);
    spi_txrx((arg >> 16) & 0xFF);
    spi_txrx((arg >> 8) & 0xFF);
    spi_txrx(arg & 0xFF);
    spi_txrx(crc);

    for (int i = 0; i < 10; i++) {
        uint8_t r = spi_txrx(0xFF);
        if ((r & 0x80) == 0) {
            return r;
        }
    }

    return 0xFF;
}

void sd_spi_probe_run(void) {
    printf("\n=== RAW SD SPI PROBE ===\n");
    printf("SPI1 pins: MISO=GP%d CS=GP%d SCK=GP%d MOSI=GP%d\n",
           SD_MISO_PIN, SD_CS_PIN, SD_SCK_PIN, SD_MOSI_PIN);

    spi_init(SD_SPI_PORT, 100 * 1000);  // slow 100 kHz for SD init

    gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_SCK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(SD_CS_PIN);
    gpio_set_dir(SD_CS_PIN, GPIO_OUT);
    gpio_put(SD_CS_PIN, 1);

    sleep_ms(100);

    // Send 80+ clocks with CS high.
    for (int i = 0; i < 20; i++) {
        spi_txrx(0xFF);
    }

    printf("Sending CMD0...\n");
    uint8_t r0 = sd_send_cmd(0, 0x00000000, 0x95);
    printf("RAW CMD0 response = 0x%02x, expected 0x01\n", r0);

    printf("Sending CMD8...\n");
    uint8_t r8 = sd_send_cmd(8, 0x000001AA, 0x87);
    printf("RAW CMD8 response = 0x%02x, expected 0x01 or 0x05\n", r8);

    sd_deselect();

    printf("=== END RAW SD SPI PROBE ===\n\n");
}