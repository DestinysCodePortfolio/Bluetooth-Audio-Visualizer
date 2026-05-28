/************************************************************************/
/*	Author:		Allan Knight											*/
/*	Copyright 2025, University of California Riverside					*/
/************************************************************************/
/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/************************************************************************/
/*  File Description:													*/
/*																		*/
/*	This header file contains the object class declarations and other	*/
/*	interface declarations need to use the OLED graphics display driver	*/
/*	for the Digilent Basic I/O Shield.									*/
/*																		*/

#ifndef _SPI_DISPLAY_H_
#define _SPI_DSIPAY_H_

/* ------------------------------------------------------------ */
/*					Miscellaneous Declarations					*/
/* ------------------------------------------------------------ */

#include <hardware/spi.h>
#include <cstdlib>

#define OLEDRGB_WIDTH                      480
#define OLEDRGB_HEIGHT                     272




/* ------------------------------------------------------------ */
/*					Global Variable Declarations				*/
/* ------------------------------------------------------------ */

/* ------------------------------------------------------------ */
/*					Object Class Declarations					*/
/* ------------------------------------------------------------ */


namespace ucr { namespace bcoe {
class SPIDisplay {
public:
	enum SPI_Instance {
		SPI0,
		SPI1
	};

	enum Commands {
		CMD_CLEARWINDOW      = 0x25,
		CMD_SETCOLUMNADDRESS = 0x15,
		CMD_SETROWADDRESS    = 0x75,
		CMD_DISPLAYOFF       = 0xAE,
		CMD_DISPLAYON        = 0xAF
	};

private: 
	uint8_t cmds[13];

	uint16_t width;
	uint16_t height;
	uint32_t baudrate;

	uint8_t spi_SCK_pin;
	uint8_t spi_TX_pin;
	uint8_t spi_CSN_pin;
	uint8_t data_cmd_pin;

private:
	uint8_t writeSPI(uint8_t val);
	uint8_t writeSPI(uint8_t val1, uint8_t val2);	
	uint8_t writeSPI(uint8_t *cmd, int nCmd, uint8_t *data, int nData = -1);
	
	void deviceInitialize();
	void deviceTerminate();
	void hostInitialize();
	void hostTerminate();
	
public:
	uint16_t buildRGB(uint8_t R, uint8_t G, uint8_t B) {return ((R>>3)<<11) | ((G>>2)<<5) | (B>>3);}
	uint8_t extractRFromRGB(uint16_t wRGB){return (uint8_t)((wRGB>>11)&0x1F);}
	uint8_t extractGFromRGB(uint16_t wRGB){return (uint8_t)((wRGB>>5)&0x3F);}
	uint8_t extractBFromRGB(uint16_t wRGB){return (uint8_t)(wRGB&0x1F);}	

	uint16_t getWidth() const { return width; }
	uint16_t getHeight() const { return height; }

	SPIDisplay(uint16_t w, uint16_t h, uint32_t baudrate, uint8_t dc_pin, SPI_Instance spi_inst = SPI0);

	/* Basic device control functions.
	*/
    void begin(void);
	void end(void);

	void clear();

	void drawBitmap(uint16_t c1, uint16_t r1, uint16_t c2, uint16_t r2, uint8_t *pBmp);
};

}}
/* ------------------------------------------------------------ */

#endif

/************************************************************************/
