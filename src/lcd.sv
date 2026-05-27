module lcd
(
    input logic CLK,

    output logic LCD_CLK,      // LCD clk
    output logic LCD_DE,      // Display Enable

    output logic [4:0] LCD_B, // 5-bit blue color data
    output logic [5:0] LCD_G, // 6-bit green color data
    output logic [4:0] LCD_R,  // 5-bit red color data
   // input  logic [15:0] pixel,
    //output logic [7:0] pixel_address
);

/*logic [7:0]  waddr;
logic [15:0] wdata;
logic we;
*/

logic [9:0] x; //324 count
logic [8:0] y; //285 count
//logic [3:0] yMod;
//logic [3:0] xMod;

logic rst;
assign rst = 1'b0;
assign LCD_CLK = CLK; 

 always_ff @(posedge CLK) begin
        if (rst) begin
            x <= 0;
            y <= 0;
        end
        else begin
            if (x == 525 - 1) begin
                x <= 0;

                if (y == 480 - 1)
                    y <= 0;
                else
                    y <= y + 1;
            end
            else begin
                x <= x + 1;
            end
        end
    end
 always_comb begin
        /*LCD_DE = (x < 480) && (y < 272);

        if (LCD_DE) begin
             yMod = y % 16;
             xMod = x % 16;
            // yMod * 16 is needed to skip the rows that we already wrote to
            pixel_address = (yMod * 16) + xMod;
            LCD_R = pixel[15:11];
            LCD_G = pixel[10:5];
            LCD_B = pixel[4:0];

            end
        
        else begin
            pixel_address = 8'd0;
            LCD_R = 5'd0;
            LCD_G = 6'd0;
            LCD_B = 5'd0;
            yMod = 4'b0;
            xMod = 4'b0;
    end*/
    LCD_DE  = 1'b1;

    LCD_R = 5'b11111;
    LCD_G = 6'b000000;
    LCD_B = 5'b00000;
 end
endmodule