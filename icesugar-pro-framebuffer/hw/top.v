`include "async_fifo.v"
`include "lcd_fb.v"
`include "lcd_timing.v"
`include "pll_clocks.v"
`include "sdram_controller.v"
`include "display_controller.v"

module top (
    // iCESugar-Pro 25MHz onboard clock (Pin P6)
    input  wire        clk_25m,       

    // IS42S16160B SDRAM Interface
    output wire        sdram_clk,
    output wire        sdram_cke,
    output wire        sdram_cs_n,
    output wire        sdram_ras_n,
    output wire        sdram_cas_n,
    output wire        sdram_we_n,
    output wire [1:0]  sdram_ba,
    output wire [12:0] sdram_a,
    output wire [1:0]  sdram_dqm,
    inout  wire [15:0] sdram_dq,

    // RGB LCD Interface (480x272)
    output wire        lcd_clk,
    output wire        lcd_hsync,
    output wire        lcd_vsync,
    output wire        lcd_de,
    output wire [7:0]  lcd_r,
    output wire [7:0]  lcd_g,
    output wire [7:0]  lcd_b,

    // SPI Peripheral for Display Commands from MCU
    input wire         sclk,
    input wire         pico,
    input wire         cs_n,
    input wire         data_cmd,

    output wire [7:0]  dbg
);

    reg        wr_en = 0;
    wire       wr_ack;
    reg [23:0] wr_addr = 24'h000000;       
    reg [15:0] wr_data = 16'h0000;

    icesugar_pro_lcd_fb fb_inst (
        .clk_25m(clk_25m),
        
        .sdram_clk(sdram_clk),
        .sdram_cke(sdram_cke),
        .sdram_cs_n(sdram_cs_n),
        .sdram_ras_n(sdram_ras_n),
        .sdram_cas_n(sdram_cas_n),
        .sdram_we_n(sdram_we_n),
        .sdram_ba(sdram_ba),
        .sdram_a(sdram_a),
        .sdram_dqm(sdram_dqm),
        .sdram_dq(sdram_dq),

        .lcd_clk(lcd_clk),
        .lcd_hsync(lcd_hsync),
        .lcd_vsync(lcd_vsync),
        .lcd_de(lcd_de),
        .lcd_r(lcd_r[7:3]),
        .lcd_g(lcd_g[7:2]),
        .lcd_b(lcd_b[7:3]),
        
        .wr_en(wr_en),
        .wr_addr(wr_addr),
        .wr_data(wr_data),
        .wr_ack(wr_ack),
        .clk_100m(clk_100m),
        .locked(locked)
    );

    assign lcd_r[0:2] = {3{lcd_r[3]}};
    assign lcd_g[0:1] = {2{lcd_g[2]}};
    assign lcd_b[0:2] = {3{lcd_b[3]}};

    wire locked;
    wire reset = ~locked;
    wire clk_100m;
    wire [7:0]  pixel_half;
    wire [23:0] pixel_addr;
    wire        pixel_wr_en;

    display_controller # (.DISPLAY_WIDTH(960), .DISPLAY_HEIGHT(272)) controller_inst (
        .clk(clk_25m),
        .reset(reset),
        .disp_CS_n(cs_n),
        .disp_SCLK(sclk),
        .disp_PICO(pico),
        .data_cmd(data_cmd),
        .framebuffer_addr_out(pixel_addr),
        .framebuffer_data_out(pixel_half),
        .framebuffer_wren(pixel_wr_en)
    );

    assign dbg = {data_cmd, pico, sclk, cs_n};

    integer clock_count = 0;
    wire reset;

    always @(posedge clk_100m) begin
        if (~reset) begin
            wr_en <= wr_en & ~wr_ack;
            if (pixel_wr_en) begin
                if (~pixel_addr[0]) begin
                    wr_data[7:0] <= pixel_half;
                end else begin
                    wr_data[15:8] <= pixel_half;
                    wr_addr <= {0, pixel_addr[22:1]};
                    wr_en <= 1;
                end 
            end 
        end
    end

    // integer clk_count = 0;
    // always @(posedge clk_25m) begin
    //     if (~reset) begin
    //         if (wr_addr <= 24'h01FE00) begin
    //             wr_data <= wr_addr[4] ? 16'hF800 : 16'h001F;
    //             wr_en <= 1;
    //             if (wr_ack) wr_addr <= wr_addr + 1;
    //         end else begin
    //             wr_en <= 0;
    //         end
    //     end
    // end

endmodule