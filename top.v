`include "async_fifo.v"
`include "lcd_fb.v"
`include "lcd_timing.v"
`include "pll_clocks.v"
`include "sdram_controller.v"

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
    output wire [4:0]  lcd_r,
    output wire [5:0]  lcd_g,
    output wire [4:0]  lcd_b,
    output wire [4:0]  dbg
);

    reg        wr_en;
    reg [23:0] wr_addr;       
    reg [15:0] wr_data;

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
        .lcd_r(lcd_r),
        .lcd_g(lcd_g),
        .lcd_b(lcd_b),
        
        .wr_en(wr_en),
        .wr_addr(wr_addr),
        .wr_data(wr_data)
    );

    assign dbg = {lcd_hsync, lcd_vsync, lcd_de};

    reg [16:0] pixel_addr = 17'h00000;
    integer clk_count = 0;

    always @(posedge clk_25m) begin
        if (clk_count < 10000) begin
            clk_count <= clk_count + 1;
        end else
        if (pixel_addr <= 17'h1FFFF) begin
            wr_addr <= pixel_addr;
            wr_data <= pixel_addr[4] ? 16'h07E0 : 16'h001F;
            wr_en <= 1;
            pixel_addr <= pixel_addr + 1;
        end else begin
            wr_en <= 0;
        end
    end

endmodule