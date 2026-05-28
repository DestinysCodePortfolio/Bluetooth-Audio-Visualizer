`include "src/lcd.sv"
`include "src/spi.sv"
module top(
    input logic CLK,

    // LCD signals
    output logic LCD_CLK,      // LCD clk
    output logic LCD_DE,      // Display Enable

    output logic [4:0] LCD_B, // 5-bit blue color data
    output logic [5:0] LCD_G, // 6-bit green color data
    output logic [4:0] LCD_R,  // 5-bit red color data

    // SPI signals
    input  logic SCK,
    input  logic MOSI,
    input  logic CS
);
    //logic [15:0] pixel;
    //logic [7:0]  pixel_address;

lcd lcd_dut (
    .CLK(CLK),
    .LCD_CLK(LCD_CLK),
    .LCD_DE(LCD_DE),
    .LCD_B(LCD_B),
    .LCD_G(LCD_G),
    .LCD_R(LCD_R),
    //.pixel(pixel),
    //.pixel_address(pixel_address)
);

/*spi spi_dut (
    .CLK(CLK),
    .SCK(SCK),
    .MOSI(MOSI),
    .CS(CS),
    .waddr(waddr),
    .wdata(wdata),
    .we(we)
);*/

endmodule