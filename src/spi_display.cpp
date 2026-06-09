/************************************************************************/
/*																		*/
/*	IOShieldOled.c	--	OLED Display Driver for Basic I/O Shield		*/
/*																		*/
/************************************************************************/
/*	Author: 	Allan Knight											*/
/*	Copyright 2025, University of California Riverside					*/
/************************************************************************/

/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*	This module contains the implementation of the object class that	*/
/*	forms the chipKIT interface to the graphics driver functions for	*/
/*	the OLED display on the Digilent Basic I/O Shield.					*/
/*																		*/
/************************************************************************/


/* ------------------------------------------------------------ */
/*				Include File Definitions						*/
/* ------------------------------------------------------------ */

#include "spi_display.h"
#include "lv_conf.h"

#include <lvgl.h>

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <pico/time.h>
#include <hardware/spi.h>
#include <pico/cyw43_arch.h>

/* ------------------------------------------------------------ */
/*				Oledrgb Definitions					*/
/* ------------------------------------------------------------ */

/* ------------------------------------------------------------ */
/***	void Oledrgb::Oledrgb()
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Default constructor.
*/

ucr::bcoe::SPIDisplay::SPIDisplay(uint16_t w, uint16_t h, uint32_t baudrate, uint8_t dc_pin, SPI_Instance spi_inst) :
width(w), height(h), baudrate(baudrate), data_cmd_pin(dc_pin) {
    switch(spi_inst) {
        case SPI0:
            spi_SCK_pin = PICO_DEFAULT_SPI_SCK_PIN;
            spi_TX_pin  = PICO_DEFAULT_SPI_TX_PIN;
            spi_CSN_pin = PICO_DEFAULT_SPI_CSN_PIN;
        break;

        default:
        break;
    }
}

/* ------------------------------------------------------------ */
/***	void Oledrgb::begin(void)
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Initialize the OLED display controller and turn the display on.
*/
void ucr::bcoe::SPIDisplay::begin(void) {
	hostInitialize();
	deviceInitialize();
}

/* ------------------------------------------------------------ */
/***	void Oledrgb::end(void)
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Shut down the OLED display. This turns the power off to the
**		OLED display, and releases all of the PIC32 resources used
**		by the OLED display driver.
*/

void ucr::bcoe::SPIDisplay::end() {
	hostTerminate();
}


/* ------------------------------------------------------------ */
/***	OledrgbHostInit
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Perform PIC32 device initialization to prepare for use
**		of the OLEDrgb display.

*/
void ucr::bcoe::SPIDisplay::hostInitialize() {

	uint result = spi_init(spi_default, baudrate);

    gpio_set_function(spi_SCK_pin, GPIO_FUNC_SPI);
    gpio_set_function(spi_TX_pin, GPIO_FUNC_SPI);
	gpio_set_function(spi_CSN_pin, GPIO_FUNC_SPI);

	gpio_init(data_cmd_pin);
	gpio_set_dir(data_cmd_pin, GPIO_OUT);
		
	gpio_put(data_cmd_pin, 0);
}

/* ------------------------------------------------------------ */
/***	OledrgbHostTerm
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Release processor resources used by the library
*/
void ucr::bcoe::SPIDisplay::hostTerminate() {

	// Make the signal pins be inputs.
	gpio_put(data_cmd_pin, 1);
	gpio_set_dir(data_cmd_pin, GPIO_IN);

	// Make power control pins be inputs. 
	spi_deinit(spi_default);
}

void ucr::bcoe::SPIDisplay::deviceInitialize() {	
}


/* ------------------------------------------------------------ */
/***	void Oledrgb::clear(void)
**
**	Parameters:
**		none
**
**	Return Value:
**		none
**
**	Errors:
**		none
**
**	Description:
**		Clear the display. This clears the memory buffer and then
**		updates the display.
*/
void ucr::bcoe::SPIDisplay::clear() {
	uint16_t w = 2 * width - 1;
	uint16_t h = height - 1;

	cmds[0] = CMD_CLEARWINDOW; 		// Enter the “clear mode”
	cmds[1] = 0x00;					// Set the starting column coordinates HB
	cmds[2] = 0x00;					// Set the starting column coordinates LB
	cmds[3] = 0x00;					// Set the starting row coordinates HB
	cmds[4] = 0x00;					// Set the starting row coordinates LB
	cmds[5] = w >> 8;	// Set the finishing column coordinates HB
	cmds[6] = w & 0xff;	// Set the finishing column coordinates LB
	cmds[7] = h >> 8;	// Set the finishing row coordinates HB
	cmds[8] = h & 0xff;	// Set the finishing row coordinates LB
	writeSPI(cmds, 9, NULL);
}

uint8_t ucr::bcoe::SPIDisplay::writeSPI(uint8_t val) {
	// write to SPI
	uint8_t rx = spi_write_blocking(spi_default, &val, 1);

	return rx;
}

uint8_t ucr::bcoe::SPIDisplay::writeSPI(uint8_t val1, uint8_t val2) {
	// write to SPI
	uint8_t rx = spi_write_blocking(spi_default, &val1, 1);
	
	// write to SPI
	rx = spi_write_blocking(spi_default, &val2, 1);

    return rx;
}

uint8_t ucr::bcoe::SPIDisplay::writeSPI(uint8_t *cmd, int nCmd, uint8_t *data, int nData) {
	
	uint8_t rx = spi_write_blocking(spi_default, cmd, nCmd);
	if(data != nullptr) {
		gpio_put(data_cmd_pin, 1);
		rx = spi_write_blocking(spi_default, data, nData);
		gpio_put(data_cmd_pin, 0);
	}

	return rx;
}

void ucr::bcoe::SPIDisplay::drawBitmap(uint16_t c1, uint16_t r1, uint16_t c2, uint16_t r2, uint8_t *pBmp)
{
	//set column start and end
	cmds[0] = CMD_SETCOLUMNADDRESS; 		
	cmds[1] = c1 >> 8;				// Set the starting column coordinates
	cmds[2] = c1 & 0xFF;			// Set the starting column coordinates
	cmds[3] = c2 >> 8;	 			// Set the finishing column coordinates
	cmds[4] = c2 & 0xFF;			// Set the finishing column coordinates

	//set row start and end
	cmds[5] = CMD_SETROWADDRESS; 		
	cmds[6] = r1 >> 8;				// Set the starting row coordinates
	cmds[7] = r1 & 0xFF;			// Set the starting row coordinates
	cmds[8] = r2 >> 8;				// Set the finishing row coordinates
	cmds[9] = r2 & 0xFF;			// Set the finishing row coordinates

	writeSPI(cmds, 10, pBmp, ((c2 - c1 + 1) * (r2 - r1 + 1)));
}

/* ------------------------------------------------------------ */

/************************************************************************/
uint8_t bmp[OLEDRGB_WIDTH*OLEDRGB_HEIGHT];
#define COUNT_DOWN 3

// int main(void)
// {
//     // Init drivers
// 	stdio_init_all();
// 	cyw43_arch_init();

//     ucr::bcoe::SPIDisplay spi_display(480, 272, 6000000, 20);
// 	spi_display.begin();
// 	spi_display.clear();
    
// 	lv_init();

//     lv_tick_set_cb(cs122_get_millis);

//     lv_display_t *display = lv_display_create(OLEDRGB_WIDTH, OLEDRGB_HEIGHT);

//     /*LVGL will render to this 1/10 screen sized buffer for 2 bytes/pixel*/
//     static uint8_t buf[OLEDRGB_WIDTH * OLEDRGB_HEIGHT * 2 / 10];
//     lv_display_set_buffers(display, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
//     lv_display_set_user_data(display, &spi_display);

//     /*This callback will display the rendered image*/
//     lv_display_set_flush_cb(display, cs122_flush_cb_partial);

//    /*Change the active screen's background color*/
//    //lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);

// 	// Call UI Generation function
// 	lv_example_scale_6();
	
// 	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
//     /*Make LVGL periodically execute its tasks*/
//     while(1) {
//         lv_timer_handler();
//         sleep_ms(5);  /*Wait 5 milliseconds before processing LVGL timer again*/
//     }
// }
